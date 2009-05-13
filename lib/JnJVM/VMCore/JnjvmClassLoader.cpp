//===-- JnjvmClassLoader.cpp - Jnjvm representation of a class loader ------===//
//
//                              Jnjvm
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include <climits>
#include <cstdlib>

// for strrchr
#include <cstring>

// for dlopen and dlsym
#include <dlfcn.h> 

// for stat, S_IFMT and S_IFDIR
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <string>


#if defined(__MACH__)
#define SELF_HANDLE RTLD_DEFAULT
#define DYLD_EXTENSION ".dylib"
#else
#define SELF_HANDLE 0
#define DYLD_EXTENSION ".so"
#endif

#include "debug.h"
#include "mvm/Allocator.h"

#include "Classpath.h"
#include "ClasspathReflect.h"
#include "JavaClass.h"
#include "JavaCompiler.h"
#include "JavaConstantPool.h"
#include "JavaString.h"
#include "JavaThread.h"
#include "JavaTypes.h"
#include "JavaUpcalls.h"
#include "Jnjvm.h"
#include "JnjvmClassLoader.h"
#include "LockedMap.h"
#include "Reader.h"
#include "Zip.h"

using namespace jnjvm;

typedef void (*static_init_t)(JnjvmClassLoader*);

JnjvmBootstrapLoader::JnjvmBootstrapLoader(mvm::BumpPtrAllocator& Alloc,
                                           JavaCompiler* Comp, 
                                           bool dlLoad) : 
    JnjvmClassLoader(Alloc) {
  
	TheCompiler = Comp;
  
  hashUTF8 = new(allocator) UTF8Map(allocator, 0);
  classes = new(allocator) ClassMap();
  javaTypes = new(allocator) TypeMap(); 
  javaSignatures = new(allocator) SignMap(); 
  
  bootClasspathEnv = getenv("JNJVM_BOOTCLASSPATH");
  if (!bootClasspathEnv) {
    bootClasspathEnv = GNUClasspathGlibj;
  }
  
  libClasspathEnv = getenv("JNJVM_LIBCLASSPATH");
  if (!libClasspathEnv) {
    libClasspathEnv = GNUClasspathLibs;
  }
  
  
  upcalls = new(allocator) Classpath();
  bootstrapLoader = this;
   
  // Try to find if we have a pre-compiled rt.jar
  if (dlLoad) {
    SuperArray = (Class*)dlsym(SELF_HANDLE, "java.lang.Object");
    if (!SuperArray) {
      nativeHandle = dlopen("libvmjc"DYLD_EXTENSION, RTLD_LAZY | RTLD_GLOBAL);
      if (nativeHandle) {
        // Found it!
        SuperArray = (Class*)dlsym(nativeHandle, "java.lang.Object");
      }
    }
    
    if (SuperArray) {
      assert(TheCompiler && 
					   "Loading libvmjc"DYLD_EXTENSION" requires a compiler");
			ClassArray::SuperArray = (Class*)SuperArray->getInternal();
      
      // Get the native classes.
      upcalls->OfVoid = (ClassPrimitive*)dlsym(nativeHandle, "void");
      upcalls->OfBool = (ClassPrimitive*)dlsym(nativeHandle, "boolean");
      upcalls->OfByte = (ClassPrimitive*)dlsym(nativeHandle, "byte");
      upcalls->OfChar = (ClassPrimitive*)dlsym(nativeHandle, "char");
      upcalls->OfShort = (ClassPrimitive*)dlsym(nativeHandle, "short");
      upcalls->OfInt = (ClassPrimitive*)dlsym(nativeHandle, "int");
      upcalls->OfFloat = (ClassPrimitive*)dlsym(nativeHandle, "float");
      upcalls->OfLong = (ClassPrimitive*)dlsym(nativeHandle, "long");
      upcalls->OfDouble = (ClassPrimitive*)dlsym(nativeHandle, "double");
      
      
      // We have the java/lang/Object class, execute the static initializer.
      static_init_t init = (static_init_t)(uintptr_t)SuperArray->classLoader;
      assert(init && "Loaded the wrong boot library");
      init(this);
      
      // Get the base object arrays after the init, because init puts arrays
      // in the class loader map.
      upcalls->ArrayOfString = 
        constructArray(asciizConstructUTF8("[Ljava/lang/String;"));
  
      upcalls->ArrayOfObject = 
        constructArray(asciizConstructUTF8("[Ljava/lang/Object;"));
      
      InterfacesArray = upcalls->ArrayOfObject->interfaces;
      ClassArray::InterfacesArray = InterfacesArray;

    }
  }
   
  if (!upcalls->OfChar) {
    // Allocate interfaces.
    InterfacesArray = (Class**)allocator.Allocate(2 * sizeof(UserClass*));
    ClassArray::InterfacesArray = InterfacesArray;

    // Create the primitive classes.
    upcalls->OfChar = UPCALL_PRIMITIVE_CLASS(this, "char", 2);
    upcalls->OfBool = UPCALL_PRIMITIVE_CLASS(this, "boolean", 1);
    upcalls->OfShort = UPCALL_PRIMITIVE_CLASS(this, "short", 2);
    upcalls->OfInt = UPCALL_PRIMITIVE_CLASS(this, "int", 4);
    upcalls->OfLong = UPCALL_PRIMITIVE_CLASS(this, "long", 8);
    upcalls->OfFloat = UPCALL_PRIMITIVE_CLASS(this, "float", 4);
    upcalls->OfDouble = UPCALL_PRIMITIVE_CLASS(this, "double", 8);
    upcalls->OfVoid = UPCALL_PRIMITIVE_CLASS(this, "void", 0);
    upcalls->OfByte = UPCALL_PRIMITIVE_CLASS(this, "byte", 1);
  }
  
  // Create the primitive arrays.
  upcalls->ArrayOfChar = constructArray(asciizConstructUTF8("[C"),
                                        upcalls->OfChar);

  upcalls->ArrayOfByte = constructArray(asciizConstructUTF8("[B"),
                                        upcalls->OfByte);
  
  upcalls->ArrayOfInt = constructArray(asciizConstructUTF8("[I"),
                                       upcalls->OfInt);
  
  upcalls->ArrayOfBool = constructArray(asciizConstructUTF8("[Z"),
                                        upcalls->OfBool);
  
  upcalls->ArrayOfLong = constructArray(asciizConstructUTF8("[J"),
                                        upcalls->OfLong);
  
  upcalls->ArrayOfFloat = constructArray(asciizConstructUTF8("[F"),
                                         upcalls->OfFloat);
  
  upcalls->ArrayOfDouble = constructArray(asciizConstructUTF8("[D"),
                                          upcalls->OfDouble);
  
  upcalls->ArrayOfShort = constructArray(asciizConstructUTF8("[S"),
                                         upcalls->OfShort);
  
  // Fill the maps.
  primitiveMap[I_VOID] = upcalls->OfVoid;
  primitiveMap[I_BOOL] = upcalls->OfBool;
  primitiveMap[I_BYTE] = upcalls->OfByte;
  primitiveMap[I_CHAR] = upcalls->OfChar;
  primitiveMap[I_SHORT] = upcalls->OfShort;
  primitiveMap[I_INT] = upcalls->OfInt;
  primitiveMap[I_FLOAT] = upcalls->OfFloat;
  primitiveMap[I_LONG] = upcalls->OfLong;
  primitiveMap[I_DOUBLE] = upcalls->OfDouble;

  arrayTable[JavaArray::T_BOOLEAN - 4] = upcalls->ArrayOfBool;
  arrayTable[JavaArray::T_BYTE - 4] = upcalls->ArrayOfByte;
  arrayTable[JavaArray::T_CHAR - 4] = upcalls->ArrayOfChar;
  arrayTable[JavaArray::T_SHORT - 4] = upcalls->ArrayOfShort;
  arrayTable[JavaArray::T_INT - 4] = upcalls->ArrayOfInt;
  arrayTable[JavaArray::T_FLOAT - 4] = upcalls->ArrayOfFloat;
  arrayTable[JavaArray::T_LONG - 4] = upcalls->ArrayOfLong;
  arrayTable[JavaArray::T_DOUBLE - 4] = upcalls->ArrayOfDouble;
  
  // Analyse the boot classpath to locate java/lang/Object. Since the
  // analyseClasspathEnv function may require to create a Java byte array to
  // hold the .zip file, we call the function after creation of the
  // array classes.
  analyseClasspathEnv(bootClasspathEnv);
  
  Attribut::codeAttribut = asciizConstructUTF8("Code");
  Attribut::exceptionsAttribut = asciizConstructUTF8("Exceptions");
  Attribut::constantAttribut = asciizConstructUTF8("ConstantValue");
  Attribut::lineNumberTableAttribut = asciizConstructUTF8("LineNumberTable");
  Attribut::innerClassesAttribut = asciizConstructUTF8("InnerClasses");
  Attribut::sourceFileAttribut = asciizConstructUTF8("SourceFile");
  
  initName = asciizConstructUTF8("<init>");
  initExceptionSig = asciizConstructUTF8("(Ljava/lang/String;)V");
  clinitName = asciizConstructUTF8("<clinit>");
  clinitType = asciizConstructUTF8("()V");
  runName = asciizConstructUTF8("run");
  prelib = asciizConstructUTF8("lib");
#if defined(__MACH__)
  postlib = asciizConstructUTF8(".dylib");
#else 
  postlib = asciizConstructUTF8(".so");
#endif
  mathName = asciizConstructUTF8("java/lang/Math");
  stackWalkerName = asciizConstructUTF8("gnu/classpath/VMStackWalker");
  NoClassDefFoundError = asciizConstructUTF8("java/lang/NoClassDefFoundError");

#define DEF_UTF8(var) \
  var = asciizConstructUTF8(#var)
  
  DEF_UTF8(abs);
  DEF_UTF8(sqrt);
  DEF_UTF8(sin);
  DEF_UTF8(cos);
  DEF_UTF8(tan);
  DEF_UTF8(asin);
  DEF_UTF8(acos);
  DEF_UTF8(atan);
  DEF_UTF8(atan2);
  DEF_UTF8(exp);
  DEF_UTF8(log);
  DEF_UTF8(pow);
  DEF_UTF8(ceil);
  DEF_UTF8(floor);
  DEF_UTF8(rint);
  DEF_UTF8(cbrt);
  DEF_UTF8(cosh);
  DEF_UTF8(expm1);
  DEF_UTF8(hypot);
  DEF_UTF8(log10);
  DEF_UTF8(log1p);
  DEF_UTF8(sinh);
  DEF_UTF8(tanh);
  DEF_UTF8(finalize);

#undef DEF_UTF8
  
  
}

JnjvmClassLoader::JnjvmClassLoader(mvm::BumpPtrAllocator& Alloc,
                                   JnjvmClassLoader& JCL, JavaObject* loader,
                                   Jnjvm* I) : allocator(Alloc) {
  bootstrapLoader = JCL.bootstrapLoader;
  TheCompiler = bootstrapLoader->getCompiler()->Create("Applicative loader");
  
  hashUTF8 = new(allocator) UTF8Map(allocator,
                                    bootstrapLoader->upcalls->ArrayOfChar);
  classes = new(allocator) ClassMap();
  javaTypes = new(allocator) TypeMap();
  javaSignatures = new(allocator) SignMap();

  javaLoader = loader;
  isolate = I;

  JavaMethod* meth = bootstrapLoader->upcalls->loadInClassLoader;
  loader->getClass()->asClass()->lookupMethodDontThrow(meth->name, meth->type,
                                                       false, true, &loadClass);
  assert(loadClass && "Loader does not have a loadClass function");

#if defined(SERVICE)
  /// If the appClassLoader is already set in the isolate, then we need
  /// a new one each time a class loader is allocated.
  if (isolate->appClassLoader) {
    isolate = new Jnjvm(allocator, bootstrapLoader);
    isolate->memoryLimit = 4000000;
    isolate->threadLimit = 10;
    isolate->parent = I->parent;
    isolate->CU = this;
    mvm::Thread* th = mvm::Thread::get();
    mvm::VirtualMachine* OldVM = th->MyVM;
    th->MyVM = isolate;
    th->IsolateID = isolate->IsolateID;
    
    isolate->loadBootstrap();
    
    th->MyVM = OldVM;
    th->IsolateID = OldVM->IsolateID;
  }
#endif

}

ArrayUInt8* JnjvmBootstrapLoader::openName(const UTF8* utf8) {
  char* asciiz = utf8->UTF8ToAsciiz();
  uint32 alen = strlen(asciiz);
  ArrayUInt8* res = 0;
  
  for (std::vector<const char*>::iterator i = bootClasspath.begin(),
       e = bootClasspath.end(); i != e; ++i) {
    const char* str = *i;
    unsigned int strLen = strlen(str);
    char* buf = (char*)alloca(strLen + alen + 7);

    sprintf(buf, "%s%s.class", str, asciiz);
    res = Reader::openFile(this, buf);
    if (res) return res;
  }

  for (std::vector<ZipArchive*>::iterator i = bootArchives.begin(),
       e = bootArchives.end(); i != e; ++i) {
    
    ZipArchive* archive = *i;
    char* buf = (char*)alloca(alen + 7);
    sprintf(buf, "%s.class", asciiz);
    res = Reader::openZip(this, archive, buf);
    if (res) return res;
  }

  return 0;
}


UserClass* JnjvmBootstrapLoader::internalLoad(const UTF8* name,
                                              bool doResolve,
                                              JavaString* strName) {
  
  UserCommonClass* cl = lookupClass(name);
  
  if (!cl) {
    ArrayUInt8* bytes = openName(name);
    if (bytes) {
      cl = constructClass(name, bytes);
    }
  }
  
  if (cl) {
    assert(!cl->isArray());
    if (doResolve) cl->asClass()->resolveClass();
  }

  return (UserClass*)cl;
}

UserClass* JnjvmClassLoader::internalLoad(const UTF8* name, bool doResolve,
                                          JavaString* strName) {
  UserCommonClass* cl = lookupClass(name);
  
  if (!cl) {
    Classpath* upcalls = bootstrapLoader->upcalls;
    UserClass* forCtp = loadClass;
    if (!strName) {
      strName = JavaString::internalToJava(name, isolate);
    }
    JavaObject* obj = (JavaObject*)
      upcalls->loadInClassLoader->invokeJavaObjectVirtual(isolate, forCtp,
                                                          javaLoader, strName,
                                                          doResolve);
    cl = (UserCommonClass*)((JavaObjectClass*)obj)->getClass();
  }
  
  if (cl) {
    assert(!cl->isArray());
    if (doResolve) cl->asClass()->resolveClass();
  }

  return (UserClass*)cl;
}

UserClass* JnjvmClassLoader::loadName(const UTF8* name, bool doResolve,
                                      bool doThrow, JavaString* strName) {
 

  UserClass* cl = internalLoad(name, doResolve, strName);

  if (!cl && doThrow) {
    Jnjvm* vm = JavaThread::get()->getJVM();
    if (name->equals(bootstrapLoader->NoClassDefFoundError)) {
      vm->unknownError("Unable to load NoClassDefFoundError");
    }
    vm->noClassDefFoundError(name);
  }

  if (cl && cl->classLoader != this) {
    classes->lock.lock();
    ClassMap::iterator End = classes->map.end();
    ClassMap::iterator I = classes->map.find(cl->name);
    if (I == End)
      classes->map.insert(std::make_pair(cl->name, cl));
    classes->lock.unlock();
  }

  return cl;
}


const UTF8* JnjvmClassLoader::lookupComponentName(const UTF8* name,
                                                  bool create,
                                                  bool& prim) {
  uint32 len = name->size;
  uint32 start = 0;
  uint32 origLen = len;
  
  while (true) {
    --len;
    if (len == 0) {
      return 0;
    } else {
      ++start;
      if (name->elements[start] != I_TAB) {
        if (name->elements[start] == I_REF) {
          uint32 size = (uint32)name->size;
          if ((size == (start + 1)) || (size == (start + 2)) ||
              (name->elements[start + 1] == I_TAB) ||
              (name->elements[origLen - 1] != I_END_REF)) {
            return 0;
          } else {
            const uint16* buf = &(name->elements[start + 1]);
            uint32 bufLen = len - 2;
            const UTF8* componentName = hashUTF8->lookupReader(buf, bufLen);
            if (!componentName && create) {
              componentName = name->extract(isolate, start + 1,
                                            start + len - 1);
            }
            return componentName;
          }
        } else {
          uint16 cur = name->elements[start];
          if ((cur == I_BOOL || cur == I_BYTE ||
               cur == I_CHAR || cur == I_SHORT ||
               cur == I_INT || cur == I_FLOAT || 
               cur == I_DOUBLE || cur == I_LONG)
              && ((uint32)name->size) == start + 1) {
            prim = true;
          }
          return 0;
        }
      }
    }
  }

  return 0;
}

UserCommonClass* JnjvmClassLoader::lookupClassOrArray(const UTF8* name) {
  UserCommonClass* temp = lookupClass(name);
  if (temp) return temp;

  if (this != bootstrapLoader) {
    temp = bootstrapLoader->lookupClassOrArray(name);
    if (temp) return temp;
  }
  
  
  if (name->elements[0] == I_TAB) {
    bool prim = false;
    const UTF8* componentName = lookupComponentName(name, false, prim);
    if (prim) return constructArray(name);
    if (componentName) {
      UserCommonClass* temp = lookupClass(componentName);
      if (temp) return constructArray(name);
    }
  }

  return 0;
}

UserCommonClass* JnjvmClassLoader::loadClassFromUserUTF8(const UTF8* name,
                                                         bool doResolve,
                                                         bool doThrow,
                                                         JavaString* strName) {
  if (name->size == 0) {
    return 0;
  } else if (name->elements[0] == I_TAB) {
    bool prim = false;
    const UTF8* componentName = lookupComponentName(name, true, prim);
    if (prim) return constructArray(name);
    if (componentName) {
      UserCommonClass* temp = loadName(componentName, doResolve, doThrow);
      if (temp) return constructArray(name);
    }
  } else {
    return loadName(name, doResolve, doThrow, strName);
  }

  return 0;
}

UserCommonClass* JnjvmClassLoader::loadClassFromAsciiz(const char* asciiz,
                                                       bool doResolve,
                                                       bool doThrow) {
  const UTF8* name = hashUTF8->lookupAsciiz(asciiz);
  if (!name) name = bootstrapLoader->hashUTF8->lookupAsciiz(asciiz);
  if (!name) name = isolate->asciizToUTF8(asciiz);
  
  UserCommonClass* temp = lookupClass(name);
  if (temp) return temp;
  
  if (this != bootstrapLoader) {
    temp = bootstrapLoader->lookupClassOrArray(name);
    if (temp) return temp;
  }
 
  return loadClassFromUserUTF8(name, doResolve, doThrow);
}


UserCommonClass* 
JnjvmClassLoader::loadClassFromJavaString(JavaString* str, bool doResolve,
                                          bool doThrow) {
  
  UTF8* name = (UTF8*)alloca(sizeof(UTF8) + str->count * sizeof(uint16));
 
  if (name) {
    name->size = str->count;
    if (str->value->elements[str->offset] != I_TAB) {
      for (sint32 i = 0; i < str->count; ++i) {
        uint16 cur = str->value->elements[str->offset + i];
        if (cur == '.') name->elements[i] = '/';
        else if (cur == '/') return 0;
        else name->elements[i] = cur;
      }
    } else {
      for (sint32 i = 0; i < str->count; ++i) {
        uint16 cur = str->value->elements[str->offset + i];
        if (cur == '.') name->elements[i] = '/';
        else name->elements[i] = cur;
      }
    }
    
    return loadClassFromUserUTF8(name, doResolve, doThrow, str);
  }

  return 0;
}

UserCommonClass* JnjvmClassLoader::lookupClassFromJavaString(JavaString* str) {
  
  UTF8* name = (UTF8*)alloca(sizeof(UTF8) + str->count * sizeof(uint16));
  if (name) {
    name->size = str->count;
    for (sint32 i = 0; i < str->count; ++i) {
      uint16 cur = str->value->elements[str->offset + i];
      if (cur == '.') name->elements[i] = '/';
      else name->elements[i] = cur;
    }
    return lookupClass(name);
  }
  return 0;
}

UserCommonClass* JnjvmClassLoader::lookupClass(const UTF8* utf8) {
  return classes->lookup(utf8);
}

UserCommonClass* JnjvmClassLoader::loadBaseClass(const UTF8* name,
                                                 uint32 start, uint32 len) {
  
  if (name->elements[start] == I_TAB) {
    UserCommonClass* baseClass = loadBaseClass(name, start + 1, len - 1);
    JnjvmClassLoader* loader = baseClass->classLoader;
    const UTF8* arrayName = name->extract(loader->hashUTF8, start, start + len);
    return loader->constructArray(arrayName, baseClass);
  } else if (name->elements[start] == I_REF) {
    const UTF8* componentName = name->extract(hashUTF8,
                                              start + 1, start + len - 1);
    UserCommonClass* cl = loadName(componentName, false, true);
    return cl;
  } else {
    Classpath* upcalls = bootstrapLoader->upcalls;
    UserClassPrimitive* prim = 
      UserClassPrimitive::byteIdToPrimitive(name->elements[start], upcalls);
    assert(prim && "No primitive found");
    return prim;
  }
}


UserClassArray* JnjvmClassLoader::constructArray(const UTF8* name) {
  ClassArray* res = (ClassArray*)lookupClass(name);
  if (res) return res;

  UserCommonClass* cl = loadBaseClass(name, 1, name->size - 1);
  assert(cl && "no base class for an array");
  JnjvmClassLoader* ld = cl->classLoader;
  res = ld->constructArray(name, cl);
  
  if (res && res->classLoader != this) {
    classes->lock.lock();
    ClassMap::iterator End = classes->map.end();
    ClassMap::iterator I = classes->map.find(res->name);
    if (I == End)
      classes->map.insert(std::make_pair(res->name, res));
    classes->lock.unlock();
  }
  return res;
}

UserClass* JnjvmClassLoader::constructClass(const UTF8* name,
                                            ArrayUInt8* bytes) {
  assert(bytes && "constructing a class without bytes");
  classes->lock.lock();
  ClassMap::iterator End = classes->map.end();
  ClassMap::iterator I = classes->map.find(name);
  UserClass* res = 0;
  if (I == End) {
    const UTF8* internalName = readerConstructUTF8(name->elements, name->size);
    res = new(allocator) UserClass(this, internalName, bytes);
    classes->map.insert(std::make_pair(internalName, res));
  } else {
    res = ((UserClass*)(I->second));
  }
  classes->lock.unlock();
  return res;
}

UserClassArray* JnjvmClassLoader::constructArray(const UTF8* name,
                                                 UserCommonClass* baseClass) {
  assert(baseClass && "constructing an array class without a base class");
  assert(baseClass->classLoader == this && 
         "constructing an array with wrong loader");
  classes->lock.lock();
  ClassMap::iterator End = classes->map.end();
  ClassMap::iterator I = classes->map.find(name);
  UserClassArray* res = 0;
  if (I == End) {
    const UTF8* internalName = readerConstructUTF8(name->elements, name->size);
    res = new(allocator) UserClassArray(this, internalName, baseClass);
    classes->map.insert(std::make_pair(internalName, res));
  } else {
    res = ((UserClassArray*)(I->second));
  }
  classes->lock.unlock();
  return res;
}

Typedef* JnjvmClassLoader::internalConstructType(const UTF8* name) {
  short int cur = name->elements[0];
  Typedef* res = 0;
  switch (cur) {
    case I_TAB :
      res = new(allocator) ArrayTypedef(name);
      break;
    case I_REF :
      res = new(allocator) ObjectTypedef(name, hashUTF8);
      break;
    default :
      UserClassPrimitive* cl = 
        bootstrapLoader->getPrimitiveClass((char)name->elements[0]);
      assert(cl && "No primitive");
      bool unsign = (cl == bootstrapLoader->upcalls->OfChar || 
                     cl == bootstrapLoader->upcalls->OfBool);
      res = new(allocator) PrimitiveTypedef(name, cl, unsign, cur);
  }
  return res;
}


Typedef* JnjvmClassLoader::constructType(const UTF8* name) {
  javaTypes->lock.lock();
  Typedef* res = javaTypes->lookup(name);
  if (res == 0) {
    res = internalConstructType(name);
    javaTypes->hash(name, res);
  }
  javaTypes->lock.unlock();
  return res;
}

static void typeError(const UTF8* name, short int l) {
  if (l != 0) {
    JavaThread::get()->getJVM()->
      unknownError("wrong type %d in %s", l, name->printString());
  } else {
    JavaThread::get()->getJVM()->
      unknownError("wrong type %s", name->printString());
  }
}


static bool analyseIntern(const UTF8* name, uint32 pos, uint32 meth,
                          uint32& ret) {
  short int cur = name->elements[pos];
  switch (cur) {
    case I_PARD :
      ret = pos + 1;
      return true;
    case I_BOOL :
      ret = pos + 1;
      return false;
    case I_BYTE :
      ret = pos + 1;
      return false;
    case I_CHAR :
      ret = pos + 1;
      return false;
    case I_SHORT :
      ret = pos + 1;
      return false;
    case I_INT :
      ret = pos + 1;
      return false;
    case I_FLOAT :
      ret = pos + 1;
      return false;
    case I_DOUBLE :
      ret = pos + 1;
      return false;
    case I_LONG :
      ret = pos + 1;
      return false;
    case I_VOID :
      ret = pos + 1;
      return false;
    case I_TAB :
      if (meth == 1) {
        pos++;
      } else {
        while (name->elements[++pos] == I_TAB) {}
        analyseIntern(name, pos, 1, pos);
      }
      ret = pos;
      return false;
    case I_REF :
      if (meth != 2) {
        while (name->elements[++pos] != I_END_REF) {}
      }
      ret = pos + 1;
      return false;
    default :
      typeError(name, cur);
  }
  return false;
}

Signdef* JnjvmClassLoader::constructSign(const UTF8* name) {
  javaSignatures->lock.lock();
  Signdef* res = javaSignatures->lookup(name);
  if (res == 0) {
    std::vector<Typedef*> buf;
    uint32 len = (uint32)name->size;
    uint32 pos = 1;
    uint32 pred = 0;

    while (pos < len) {
      pred = pos;
      bool end = analyseIntern(name, pos, 0, pos);
      if (end) break;
      else {
        buf.push_back(constructType(name->extract(hashUTF8, pred, pos)));
      } 
    }
  
    if (pos == len) {
      typeError(name, 0);
    }
  
    analyseIntern(name, pos, 0, pred);

    if (pred != len) {
      typeError(name, 0);
    }
    
    Typedef* ret = constructType(name->extract(hashUTF8, pos, pred));
    
    res = new(allocator, buf.size()) Signdef(name, this, buf, ret);

    javaSignatures->hash(name, res);
  }
  javaSignatures->lock.unlock();
  return res;
}


JnjvmClassLoader*
JnjvmClassLoader::getJnjvmLoaderFromJavaObject(JavaObject* loader, Jnjvm* vm) {
  
  if (loader == 0)
    return vm->bootstrapLoader;
 
  JnjvmClassLoader* JCL = 0;
  Classpath* upcalls = vm->bootstrapLoader->upcalls;
  VMClassLoader* vmdata = 
    (VMClassLoader*)(upcalls->vmdataClassLoader->getObjectField(loader));
  
  if (!vmdata) {
    loader->acquire();
    vmdata = 
      (VMClassLoader*)(upcalls->vmdataClassLoader->getObjectField(loader));
    if (!vmdata) {
      mvm::BumpPtrAllocator* A = new mvm::BumpPtrAllocator();    
      JCL = new(*A) JnjvmClassLoader(*A, *vm->bootstrapLoader, loader, vm);
      vmdata = VMClassLoader::allocate(JCL);
      (upcalls->vmdataClassLoader->setObjectField(loader, (JavaObject*)vmdata));
    }
    loader->release();
  } else {
    JCL = vmdata->getClassLoader();
  }

  return JCL;
}

const UTF8* JnjvmClassLoader::asciizConstructUTF8(const char* asciiz) {
  return hashUTF8->lookupOrCreateAsciiz(asciiz);
}

const UTF8* JnjvmClassLoader::readerConstructUTF8(const uint16* buf,
                                                  uint32 size) {
  return hashUTF8->lookupOrCreateReader(buf, size);
}

JnjvmClassLoader::~JnjvmClassLoader() {
  
  if (isolate)
    isolate->removeMethodsInFunctionMap(this);

  if (classes) {
    classes->~ClassMap();
    allocator.Deallocate(classes);
  }

  if (hashUTF8) {
    hashUTF8->~UTF8Map();
    allocator.Deallocate(hashUTF8);
  }

  if (javaTypes) {
    javaTypes->~TypeMap();
    allocator.Deallocate(javaTypes);
  }

  if (javaSignatures) {
    javaSignatures->~SignMap();
    allocator.Deallocate(javaSignatures);
  }

  for (std::vector<void*>::iterator i = nativeLibs.begin(); 
       i < nativeLibs.end(); ++i) {
    dlclose(*i);
  }

  delete TheCompiler;
  delete &allocator;
}


JnjvmBootstrapLoader::~JnjvmBootstrapLoader() {
}

JavaString* JnjvmClassLoader::UTF8ToStr(const UTF8* val) {
  JavaString* res = isolate->internalUTF8ToStr(val);
  strings.push_back(res);
  return res;
}

JavaString* JnjvmBootstrapLoader::UTF8ToStr(const UTF8* val) {
  Jnjvm* vm = JavaThread::get()->getJVM();
  JavaString* res = vm->internalUTF8ToStr(val);
  strings.push_back(res);
  return res;
}

void JnjvmBootstrapLoader::analyseClasspathEnv(const char* str) {
  if (str != 0) {
    unsigned int len = strlen(str);
    char* buf = (char*)alloca(len + 1);
    const char* cur = str;
    int top = 0;
    char c = 1;
    while (c != 0) {
      while (((c = cur[top]) != 0) && c != Jnjvm::envSeparator[0]) {
        top++;
      }
      if (top != 0) {
        memcpy(buf, cur, top);
        buf[top] = 0;
        char* rp = (char*)alloca(PATH_MAX);
        memset(rp, 0, PATH_MAX);
        rp = realpath(buf, rp);
        if (rp && rp[PATH_MAX - 1] == 0 && strlen(rp) != 0) {
          struct stat st;
          stat(rp, &st);
          if ((st.st_mode & S_IFMT) == S_IFDIR) {
            unsigned int len = strlen(rp);
            char* temp = (char*)allocator.Allocate(len + 2);
            memcpy(temp, rp, len);
            temp[len] = Jnjvm::dirSeparator[0];
            temp[len + 1] = 0;
            bootClasspath.push_back(temp);
          } else {
            ArrayUInt8* bytes =
              Reader::openFile(this, rp);
            if (bytes) {
              ZipArchive *archive = new(allocator) ZipArchive(bytes, allocator);
              if (archive) {
                bootArchives.push_back(archive);
              }
            }
          }
        } 
      }
      cur = cur + top + 1;
      top = 0;
    }
  }
}

// constructArrayName can allocate the UTF8 directly in the classloader
// memory because it is called by safe places, ie only valid names are
// created.
const UTF8* JnjvmClassLoader::constructArrayName(uint32 steps,
                                                 const UTF8* className) {
  uint32 len = className->size;
  uint32 pos = steps;
  bool isTab = (className->elements[0] == I_TAB ? true : false);
  uint32 n = steps + len + (isTab ? 0 : 2);
  uint16* buf = (uint16*)alloca(n * sizeof(uint16));
    
  for (uint32 i = 0; i < steps; i++) {
    buf[i] = I_TAB;
  }

  if (!isTab) {
    ++pos;
    buf[steps] = I_REF;
  }

  for (uint32 i = 0; i < len; i++) {
    buf[pos + i] = className->elements[i];
  }

  if (!isTab) {
    buf[n - 1] = I_END_REF;
  }

  return readerConstructUTF8(buf, n);
}

intptr_t JnjvmClassLoader::loadInLib(const char* buf, bool& jnjvm) {
  uintptr_t res = (uintptr_t)dlsym(SELF_HANDLE, buf);
  
  if (!res) {
    for (std::vector<void*>::iterator i = nativeLibs.begin(),
              e = nativeLibs.end(); i!= e; ++i) {
      res = (uintptr_t)dlsym((*i), buf);
      if (res) break;
    }
  } else {
    jnjvm = true;
  }
  
  if (!res && this != bootstrapLoader)
    res = bootstrapLoader->loadInLib(buf, jnjvm);

  return (intptr_t)res;
}

void* JnjvmClassLoader::loadLib(const char* buf) {
  void* handle = dlopen(buf, RTLD_LAZY | RTLD_LOCAL);
  if (handle) nativeLibs.push_back(handle);
  return handle;
}

intptr_t JnjvmClassLoader::loadInLib(const char* name, void* handle) {
  return (intptr_t)dlsym(handle, name);
}

intptr_t JnjvmClassLoader::nativeLookup(JavaMethod* meth, bool& jnjvm,
                                        char* buf) {

  meth->jniConsFromMeth(buf);
  intptr_t res = loadInLib(buf, jnjvm);
  if (!res) {
    meth->jniConsFromMethOverloaded(buf);
    res = loadInLib(buf, jnjvm);
  }
  return res;
}

void JnjvmClassLoader::insertAllMethodsInVM(Jnjvm* vm) {
  JavaCompiler* M = getCompiler();
  for (ClassMap::iterator i = classes->map.begin(), e = classes->map.end();
       i != e; ++i) {
    CommonClass* cl = i->second;
    if (cl->isClass()) {
      Class* C = cl->asClass();
      
      for (uint32 i = 0; i < C->nbVirtualMethods; ++i) {
        JavaMethod& meth = C->virtualMethods[i];
        if (!isAbstract(meth.access) && meth.code) {
          vm->addMethodInFunctionMap(&meth, meth.code);
          M->setMethod(&meth, meth.code, "");
        }
      }
      
      for (uint32 i = 0; i < C->nbStaticMethods; ++i) {
        JavaMethod& meth = C->staticMethods[i];
        if (!isAbstract(meth.access) && meth.code) {
          vm->addMethodInFunctionMap(&meth, meth.code);
          M->setMethod(&meth, meth.code, "");
        }
      }
    }
  }
}

void JnjvmClassLoader::loadLibFromJar(Jnjvm* vm, const char* name,
                                      const char* file) {

  char* soName = (char*)alloca(strlen(name) + strlen(DYLD_EXTENSION));
  const char* ptr = strrchr(name, '/');
  sprintf(soName, "%s%s", ptr ? ptr + 1 : name, DYLD_EXTENSION);
  void* handle = dlopen(soName, RTLD_LAZY | RTLD_LOCAL);
  if (handle) {
    Class* cl = (Class*)dlsym(handle, file);
    if (cl) {
      static_init_t init = (static_init_t)(uintptr_t)cl->classLoader;
      assert(init && "Loaded the wrong library");
      init(this);
      insertAllMethodsInVM(vm);
    }
  }
}

void JnjvmClassLoader::loadLibFromFile(Jnjvm* vm, const char* name) {
  assert(classes->map.size() == 0);
  char* soName = (char*)alloca(strlen(name) + strlen(DYLD_EXTENSION));
  sprintf(soName, "%s%s", name, DYLD_EXTENSION);
  void* handle = dlopen(soName, RTLD_LAZY | RTLD_LOCAL);
  if (handle) {
    Class* cl = (Class*)dlsym(handle, name);
    if (cl) {
      static_init_t init = (static_init_t)(uintptr_t)cl->classLoader;
      init(this);
      insertAllMethodsInVM(vm);
    }
  }
}

Class* JnjvmClassLoader::loadClassFromSelf(Jnjvm* vm, const char* name) {
  assert(classes->map.size() == 0);
  Class* cl = (Class*)dlsym(SELF_HANDLE, name);
  if (cl) {
    static_init_t init = (static_init_t)(uintptr_t)cl->classLoader;
    init(this);
    insertAllMethodsInVM(vm);
  }
  return cl;
}


// Extern "C" functions called by the vmjc static intializer.
extern "C" void vmjcAddPreCompiledClass(JnjvmClassLoader* JCL,
                                        CommonClass* cl) {
  cl->classLoader = JCL;
  
  if (cl->isClass()) {
    Class* realCl = cl->asClass();
		// To avoid data alignment in the llvm assembly emitter, we set the
  	// staticMethods and staticFields fields here.
    realCl->staticMethods = realCl->virtualMethods + realCl->nbVirtualMethods;
    realCl->staticFields = realCl->virtualFields + realCl->nbVirtualFields;
  	cl->virtualVT->setNativeTracer(cl->virtualVT->tracer, "");
  }

	if (!cl->isPrimitive())
	  JCL->getClasses()->map.insert(std::make_pair(cl->name, cl));

}

extern "C" void vmjcGetClassArray(JnjvmClassLoader* JCL, ClassArray** ptr,
                              const UTF8* name) {
  *ptr = JCL->constructArray(name);
}

extern "C" void vmjcAddUTF8(JnjvmClassLoader* JCL, const UTF8* val) {
  JCL->hashUTF8->insert(val);
}

extern "C" void vmjcAddString(JnjvmClassLoader* JCL, JavaString* val) {
  JCL->strings.push_back(val);
}

extern "C" intptr_t vmjcNativeLoader(JavaMethod* meth) {
  bool jnjvm = false;
  const UTF8* jniConsClName = meth->classDef->name;
  const UTF8* jniConsName = meth->name;
  const UTF8* jniConsType = meth->type;
  sint32 clen = jniConsClName->size;
  sint32 mnlen = jniConsName->size;
  sint32 mtlen = jniConsType->size;

  char* buf = (char*)alloca(3 + JNI_NAME_PRE_LEN + 1 +
                            ((mnlen + clen + mtlen) << 1));
  intptr_t res = meth->classDef->classLoader->nativeLookup(meth, jnjvm, buf);
  assert(res && "Could not find required native method");
  return res;
}

extern "C" void staticCallback() {
  fprintf(stderr, "Implement me");
  abort();
}
