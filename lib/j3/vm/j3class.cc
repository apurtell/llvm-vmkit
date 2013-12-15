#include <stdio.h>
#include <string.h>
#include <vector>

#include "llvm/ExecutionEngine/ExecutionEngine.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/DataLayout.h"

#include "j3/j3.h"
#include "j3/j3class.h"
#include "j3/j3classloader.h"
#include "j3/j3reader.h"
#include "j3/j3constants.h"
#include "j3/j3method.h"
#include "j3/j3mangler.h"
#include "j3/j3object.h"
#include "j3/j3thread.h"

using namespace j3;

/*  
 *  ------------ J3Type ------------
 */
J3Type::J3Type(J3ClassLoader* loader, const vmkit::Name* name) { 
	pthread_mutexattr_t attr;
	pthread_mutexattr_init(&attr);
	pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
	pthread_mutex_init(&_mutex, &attr);
	_loader = loader; 
	_name = name; 
}

uint8_t* J3Type::getSymbolAddress() {
	return (uint8_t*)this;
}

J3VirtualTable* J3Type::vt() { 
	return _vt; 
}

void J3Type::dump() {
	fprintf(stderr, "Type: %ls", name()->cStr());
}

char* J3Type::nativeName() {
	if(!_nativeName)
		llvmType();
	return _nativeName;
}

uint32_t J3Type::nativeNameLength() {
	if(!_nativeNameLength)
		llvmType();
	return _nativeNameLength;
}

J3ArrayClass* J3Type::getArray(uint32_t prof, const vmkit::Name* name) {
	if(!_array) {
		lock();
		if(!_array) {
			_array = new(loader()->allocator()) J3ArrayClass(loader(), this, name);
		}
		unlock();
	}

	return prof > 1 ? _array->getArray(prof-1) : _array;
}

bool J3Type::isAssignableTo(J3Type* parent) {
	resolve();
	parent->resolve();
	return vt()->isAssignableTo(parent->vt());
}

J3Type* J3Type::resolve() {
	if(status < RESOLVED)
		doResolve(0, 0);
	return this;
}

J3Type* J3Type::resolve(J3Field* hiddenFields, uint32_t nbHiddenFields) {
	if(status < RESOLVED)
		doResolve(hiddenFields, nbHiddenFields);
	else
		J3::internalError(L"trying to resolve class %ls with hidden fields while it is already loaded", name()->cStr());
	return this;
}

J3Type* J3Type::initialise() {
	if(status < INITED)
		doInitialise();
	return this;
}

J3Class* J3Type::asClass() {
	if(!isClass())
		J3::internalError(L"should not happen");
	return (J3Class*)this;
}

J3Layout* J3Type::asLayout() {
	if(!isLayout())
		J3::internalError(L"should not happen");
	return (J3Layout*)this;
}

J3Primitive* J3Type::asPrimitive() {
	if(!isPrimitive())
		J3::internalError(L"should not happen");
	return (J3Primitive*)this;
}

J3ArrayClass* J3Type::asArrayClass() {
	if(!isArrayClass())
		J3::internalError(L"should not happen");
	return (J3ArrayClass*)this;
}

J3ObjectType* J3Type::asObjectType() {
	if(!isObjectType())
		J3::internalError(L"should not happen");
	return (J3ObjectType*)this;
}

/*  
 *  ------------ J3ObjectType ------------
 */
J3ObjectType::J3ObjectType(J3ClassLoader* loader, const vmkit::Name* name) : J3Type(loader, name) {
}

J3Method* J3ObjectType::findVirtualMethod(const vmkit::Name* name, const vmkit::Name* sign, bool error) {
	J3::internalError(L"should not happen");
}

J3Method* J3ObjectType::findStaticMethod(const vmkit::Name* name, const vmkit::Name* sign, bool error) {
	J3::internalError(L"should not happen");
}

J3ObjectType* J3ObjectType::nativeClass(J3ObjectHandle* handle) {
	return (J3ObjectType*)(uintptr_t)handle->getLong(J3Thread::get()->vm()->classVMData);
}

J3ObjectHandle* J3ObjectType::javaClass() {
	if(!_javaClass) {
		lock();
		if(!_javaClass) {
			J3ObjectHandle* prev = J3Thread::get()->tell();
			_javaClass = J3ObjectHandle::doNewObject(loader()->vm()->classClass);
			loader()->fixedPoint()->syncPush(_javaClass);
			J3Thread::get()->restore(prev);
			_javaClass->setLong(loader()->vm()->classVMData, (int64_t)(uintptr_t)this);
			loader()->vm()->classInit->invokeSpecial(_javaClass);
		}
		unlock();
	}
	return _javaClass;
}

/*  
 *  ------------ J3Layout ------------
 */
J3Layout::J3Layout(J3ClassLoader* loader, const vmkit::Name* name) : J3ObjectType(loader, name) {
}

J3Method* J3Layout::findMethod(const vmkit::Name* name, const vmkit::Name* sign) {
	for(size_t i=0; i<nbMethods(); i++) {
		J3Method* cur = methods()[i];

		//printf("%ls - %ls\n", cur->name()->cStr(), cur->sign()->cStr());
		//printf("%ls - %ls\n", name->cStr(), sign->cStr());
		if(cur->name() == name && cur->sign() == sign) {
			return cur;
		}
	}
	return 0;
}

J3Field* J3Layout::findField(const vmkit::Name* name, const J3Type* type) {
	for(size_t i=0; i<nbFields; i++) {
		J3Field* cur = fields + i;

		//printf("%ls - %ls\n", cur->name()->cStr(), cur->sign()->cStr());
		//printf("%ls - %ls\n", name->cStr(), sign->cStr());
		if(cur->name() == name && cur->type() == type) {
			return cur;
		}
	}
	return 0;
}

/*  
 *  ------------ J3Class ------------
 */
J3Class::J3Class(J3ClassLoader* loader, const vmkit::Name* name) : J3Layout(loader, name), staticLayout(loader, name) {
	status = CITED;
}

size_t J3Class::size() { 
	return _size; 
}

llvm::GlobalValue* J3Class::llvmDescriptor(llvm::Module* module) {
	return llvm::cast<llvm::GlobalValue>(module->getOrInsertGlobal(nativeName(), loader()->vm()->typeJ3Class));
}


J3Method* J3Class::findVirtualMethod(const vmkit::Name* name, const vmkit::Name* sign, bool error) {
	//loader()->vm()->log(L"Lookup: %ls %ls in %ls (%d)", methName->cStr(), methSign->cStr(), name()->cStr(), nbVirtualMethods);
	resolve();

	J3Method* res = findMethod(name, sign);

	if(!res) {
		if(super() == this) {
			if(error)
				J3::noSuchMethodError(L"no such method", this, name, sign);
			else
				return 0;
		}
		res = super()->findVirtualMethod(name, sign, error);
	}

	return res;
}

J3Method* J3Class::findStaticMethod(const vmkit::Name* name, const vmkit::Name* sign, bool error) {
	//loader()->vm()->log(L"Lookup: %ls %ls in %ls", methName->cStr(), methSign->cStr(), name()->cStr());
	resolve();

	J3Method* res = staticLayout.findMethod(name, sign);

	if(!res)
		J3::internalError(L"implement me");

	return res;
}

J3Field* J3Class::findVirtualField(const vmkit::Name* name, const J3Type* type, bool error) {
	//loader()->vm()->log(L"Lookup: %ls %ls in %ls", methName->cStr(), methSign->cStr(), name()->cStr());
	resolve();

	J3Field* res = findField(name, type);

	if(!res)
		J3::internalError(L"implement me");

	return res;
}

J3Field* J3Class::findStaticField(const vmkit::Name* fname, const J3Type* ftype, bool error) {
	//fprintf(stderr, "Lookup static field %ls %ls::%ls\n", ftype->name()->cStr(), name()->cStr(), fname->cStr());
	resolve();

	J3Field* res = staticLayout.findField(fname, ftype);

	if(!res)
		J3::internalError(L"implement me");

	return res;
}

void J3Class::registerNative(const vmkit::Name* methName, const vmkit::Name* methSign, void* fnPtr) {
	resolve();
	J3Method* res = staticLayout.findMethod(methName, methSign);
	if(!res)
		res = findMethod(methName, methSign);
	if(!res || !J3Cst::isNative(res->access()))
		J3::noSuchMethodError(L"unable to find native method", this, methName, methSign);

	res->registerNative(fnPtr);
}

J3ObjectHandle* J3Class::staticInstance() { 
	return _staticInstance; 
}

void J3Class::doInitialise() {
	resolve();
	lock();
	if(status < INITED) {
		if(loader()->vm()->options()->debugIniting)
			fprintf(stderr, "Initing: %ls\n", name()->cStr());
		status = INITED;

		super()->initialise();

		for(size_t i=0; i<nbInterfaces(); i++)
			interfaces()[i]->initialise();

		J3ObjectHandle* prev = J3Thread::get()->tell();
		J3ObjectHandle* stacked = J3ObjectHandle::allocate(staticLayout.vt(),
																											 loader()->vm()->dataLayout()->getTypeAllocSize(staticLayout.llvmType()
																																																			->getContainedType(0)));
		_staticInstance = loader()->fixedPoint()->syncPush(stacked);
		J3Thread::get()->restore(prev);

		for(size_t i=0; i<staticLayout.nbFields; i++) {
			J3Field* cur = staticLayout.fields + i;
			J3Attribute* attr = cur->attributes()->lookup(loader()->vm()->constantValueAttr);

			if(attr) {
				J3Reader reader(bytes());
				reader.seek(attr->offset(), reader.SeekSet);

				uint32_t length = reader.readU4();
				if(length != 2)
					J3::classFormatError(this, L"bad length for ConstantAttribute");
				
				uint32_t idx = reader.readU2();

				switch(getCtpType(idx)) {
					case J3Cst::CONSTANT_Long:    staticInstance()->setLong(cur, longAt(idx)); break;
					case J3Cst::CONSTANT_Float:   staticInstance()->setFloat(cur, floatAt(idx)); break;
					case J3Cst::CONSTANT_Double:  staticInstance()->setDouble(cur, doubleAt(idx)); break;
					case J3Cst::CONSTANT_Integer: staticInstance()->setInteger(cur, integerAt(idx)); break;
					case J3Cst::CONSTANT_String:  staticInstance()->setObject(cur, stringAt(idx)); break;
					default:
						J3::classFormatError(this, L"invalid ctp entry ConstantAttribute with type %d", getCtpType(idx));
				}
			}
		}

		J3Method* clinit = staticLayout.findMethod(loader()->vm()->clinitName, loader()->vm()->clinitSign);
			
		if(clinit)
			clinit->invokeStatic();
	}
	unlock();
}

void J3Class::load() {
	if(status < LOADED) {
		lock();
		if(status < LOADED) {
			if(loader()->vm()->options()->debugLoad)
				fprintf(stderr, "Loading: %ls\n", name()->cStr());

			_bytes = loader()->lookup(name());

			if(!_bytes)
				J3::noClassDefFoundError(this);

			status = LOADED;
		}
		unlock();
	}
}

void J3Class::doResolve(J3Field* hiddenFields, size_t nbHiddenFields) {
	load();
	lock();
	if(status < RESOLVED) {
		if(loader()->vm()->options()->debugResolve)
			fprintf(stderr, "Resolving: %ls\n", name()->cStr());

		std::vector<llvm::Type*> virtualBody;
		status = RESOLVED;
		readClassBytes(&virtualBody, hiddenFields, nbHiddenFields);
		
		if(super() == this) {
			virtualBody[0] = loader()->vm()->typeJ3Object;
		} else {
			super()->resolve();
			virtualBody[0] = super()->llvmType()->getContainedType(0);
		}

		llvm::cast<llvm::StructType>(llvmType()->getContainedType(0))->setBody(virtualBody);

		_size = loader()->vm()->dataLayout()->getTypeAllocSize(llvmType()->getContainedType(0));
			
		staticLayout._vt = J3VirtualTable::create(&staticLayout);

		_vt = J3VirtualTable::create(this);

		//fprintf(stderr, "virtual part of %ls: ", name()->cStr());
		//llvmType()->getContainedType(0)->dump();
		//fprintf(stderr, "\n");
	}
	unlock();
}

#if 0
void J3Class::addHiddenField(J3Field* f, uint32_t num) {
	if(this == loader()->vm()->classClass) {
		f->_access     = J3Cst::ACC_PRIVATE;
		f->_name       = loader()->vm()->names()->get(L"vmData");
		f->_type       = (sizeof(uintptr_t) == 8) ? loader()->vm()->typeLong : loader()->vm()->typeInteger;
		f->_attributes = new (loader()->allocator(), 0) J3Attributes(0);
	}
}
#endif

void J3Class::readClassBytes(std::vector<llvm::Type*>* virtualBody, J3Field* hiddenFields, uint32_t nbHiddenFields) {
	J3Reader reader(_bytes);

	uint32_t magic = reader.readU4();
	if(magic != J3Cst::MAGIC)
		J3::classFormatError(this, L"bad magic");
			
	/* uint16_t minor = */reader.readU2();
	/* uint16_t major = */reader.readU2();
			
	nbCtp     = reader.readU2();
	
	if(nbCtp < 1)
		J3::classFormatError(this, L"zero-sized constant pool");
	
	ctpTypes    = (uint8_t*)loader()->allocator()->allocate(nbCtp * sizeof(uint8_t));
	ctpValues   = (uint32_t*)loader()->allocator()->allocate(nbCtp * sizeof(uint32_t));
	ctpResolved = (void**)loader()->allocator()->allocate(nbCtp * sizeof(void*));
	
	ctpTypes[0] = 0;

	for(uint32_t i=1; i<nbCtp; i++) {
		switch(ctpTypes[i] = reader.readU1()) {
			case J3Cst::CONSTANT_Utf8:
				ctpValues[i] = reader.tell();
				reader.seek(reader.readU2(), reader.SeekCur);
				break;
			case J3Cst::CONSTANT_MethodType:
			case J3Cst::CONSTANT_String:
			case J3Cst::CONSTANT_Class:
				ctpValues[i] = reader.readU2();
				break;
			case J3Cst::CONSTANT_InvokeDynamic:
			case J3Cst::CONSTANT_Float:
			case J3Cst::CONSTANT_Integer:
			case J3Cst::CONSTANT_Fieldref:
			case J3Cst::CONSTANT_Methodref:
			case J3Cst::CONSTANT_InterfaceMethodref:
			case J3Cst::CONSTANT_NameAndType:
				ctpValues[i] = reader.readU4();
				break;
			case J3Cst::CONSTANT_Long:
			case J3Cst::CONSTANT_Double:
				ctpValues[i] = reader.readU4();
				ctpValues[i+1] = reader.readU4();
				i++;
				break;
			case J3Cst::CONSTANT_MethodHandle:
				ctpValues[i] = reader.readU1() << 16;
				ctpValues[i] |= reader.readU2();
				break;
			default:
				J3::classFormatError(this, L"wrong constant pool entry type: %d", ctpTypes[i]);
		}
	}
	
	_access = reader.readU2();
	
	J3Class* self = classAt(reader.readU2());
	
	if(self != this)
		J3::classFormatError(this, L"wrong class file (describes class %ls)", self->name()->cStr());
	
	uint16_t superIdx = reader.readU2();

	_super = superIdx ? classAt(superIdx) : this;

	_nbInterfaces = reader.readU2();
	_interfaces = (J3Class**)loader()->allocator()->allocate(nbInterfaces()*sizeof(J3Class*));
	
	for(size_t i=0; i<nbInterfaces(); i++) {
		_interfaces[i] = classAt(reader.readU2());
	}

	size_t   n = nbHiddenFields + reader.readU2(), nbStaticFields = 0, nbVirtualFields = 0;
	fields = (J3Field*)alloca(sizeof(J3Field)*n);
	J3Field* pFields0[n]; size_t i0 = 0; /* sort fields by reverse size */
	J3Field* pFields1[n]; size_t i1 = 0;
	J3Field* pFields2[n]; size_t i2 = 0;
	J3Field* pFields4[n]; size_t i4 = 0;
	J3Field* pFields8[n]; size_t i8 = 0;

	memset(fields, 0, sizeof(J3Field)*n);
	
	for(size_t i=0; i<n; i++) {
		J3Field* f = fields + i;

		if(i < nbHiddenFields) {
			f->_cl         = this;
			f->_access     = hiddenFields[i].access();
			f->_name       = hiddenFields[i].name();
			f->_type       = hiddenFields[i].type();
			f->_attributes = new (loader()->allocator(), 0) J3Attributes(0);
		} else {
			f->_cl         = this;
			f->_access     = reader.readU2();
			f->_name       = nameAt(reader.readU2());
			f->_type       = loader()->getType(this, nameAt(reader.readU2()));
			f->_attributes = readAttributes(&reader);
		}

		if(J3Cst::isStatic(f->access()))
			nbStaticFields++;
		else
			nbVirtualFields++;

		switch(loader()->vm()->dataLayout()->getTypeSizeInBits(f->_type->llvmType())) {
			case 1:  pFields0[i0++] = f; break;
			case 8:  pFields1[i1++] = f; break;
			case 16: pFields2[i2++] = f; break;
			case 32: pFields4[i4++] = f; break;
			case 64: pFields8[i8++] = f; break;
			default: J3::internalError(L"should not happen");
		}
	}

	staticLayout.fields = new(loader()->allocator()) J3Field[nbStaticFields];
	fields = new(loader()->allocator()) J3Field[nbVirtualFields];

	std::vector<llvm::Type*> staticBody;

	staticBody.push_back(loader()->vm()->typeJ3Object);
	virtualBody->push_back(0);

	fillFields(&staticBody, virtualBody, pFields8, i8);
	fillFields(&staticBody, virtualBody, pFields4, i4);
	fillFields(&staticBody, virtualBody, pFields2, i2);
	fillFields(&staticBody, virtualBody, pFields1, i1);
	fillFields(&staticBody, virtualBody, pFields0, i0);

	staticLLVMType()->getContainedType(0);

	llvm::cast<llvm::StructType>(staticLLVMType()->getContainedType(0))->setBody(staticBody);

	//fprintf(stderr, "static part of %ls: ", name()->cStr());
	//staticLayout.llvmType()->getContainedType(0)->dump();
	//fprintf(stderr, "\n");
	
	size_t     nbVirtualMethods = 0, nbStaticMethods = 0;

	n = reader.readU2();
	J3Method** methodsTmp = (J3Method**)alloca(sizeof(J3Method*)*n);

	for(size_t i=0; i<n; i++) {
		uint16_t           access = reader.readU2();
		const vmkit::Name* name = nameAt(reader.readU2());
		const vmkit::Name* sign = nameAt(reader.readU2());
		J3Method*          method = loader()->method(access, this, name, sign);
		J3Attributes*      attributes = readAttributes(&reader);
		
		method->postInitialise(access, attributes);
		methodsTmp[i] = method;

		if(J3Cst::isStatic(access)) {
			nbStaticMethods++;
			method->setResolved(0);
		} else
			nbVirtualMethods++;
	}

	staticLayout._methods = (J3Method**)loader()->allocator()->allocate(sizeof(J3Method*)*nbStaticMethods);
	_methods = (J3Method**)loader()->allocator()->allocate(sizeof(J3Method*)*nbVirtualMethods);

	for(int i=0; i<n; i++) {
		J3Layout* layout = J3Cst::isStatic(methodsTmp[i]->access()) ? &staticLayout : this;
		layout->_methods[layout->_nbMethods++] = methodsTmp[i];
	}

	_attributes = readAttributes(&reader);
}

void J3Class::fillFields(std::vector<llvm::Type*>* staticBody, std::vector<llvm::Type*>* virtualBody, J3Field** fields, size_t n) {
	for(size_t i=0; i<n; i++) {
		J3Field*  cur = fields[i];
		J3Layout* layout;

		if(J3Cst::isStatic(fields[i]->access())) {
			//fprintf(stderr, "   adding static field: %ls %ls::%ls\n", cur->type()->name()->cStr(), name()->cStr(), cur->name()->cStr());
			cur->_num = staticBody->size();
			layout = &staticLayout;
			staticBody->push_back(cur->type()->llvmType());
		} else {
			cur->_num = virtualBody->size();
			layout = this;
			virtualBody->push_back(cur->type()->llvmType());
		}
		layout->fields[layout->nbFields++] = *fields[i];

	}
}

J3Attributes* J3Class::readAttributes(J3Reader* reader) {
	size_t nbAttributes = reader->readU2();
	J3Attributes* res = new (loader()->allocator(), nbAttributes) J3Attributes(nbAttributes);
	
	for(size_t i=0; i<nbAttributes; i++) {
		res->attribute(i)->_id     = nameAt(reader->readU2());
		res->attribute(i)->_offset = reader->tell();
		reader->seek(reader->readU4(), reader->SeekCur);
	}

	return res;
}

uint8_t J3Class::getCtpType(uint16_t idx) {
	check(idx);
	return ctpTypes[idx];
}

void* J3Class::getCtpResolved(uint16_t idx) {
	check(idx);
	return ctpResolved[idx];
}

J3ObjectHandle* J3Class::stringAt(uint16_t idx) {
	check(idx, J3Cst::CONSTANT_String);
	J3ObjectHandle* res = (J3ObjectHandle*)ctpResolved[idx];
	if(!res) {
		ctpResolved[idx] = res = loader()->vm()->nameToString(nameAt(ctpValues[idx]));
	}
	return (J3ObjectHandle*)res;
}

float J3Class::floatAt(uint16_t idx) {
	check(idx, J3Cst::CONSTANT_Float);
	J3Value v;
	v.valInteger = ctpValues[idx];
	return v.valFloat;
}

double J3Class::doubleAt(uint16_t idx) {
	check(idx, J3Cst::CONSTANT_Double);
	J3Value v;
	v.valLong = ((uint64_t)ctpValues[idx] << 32) + (uint64_t)ctpValues[idx+1];
	return v.valDouble;
}

uint32_t J3Class::integerAt(uint16_t idx) {
	check(idx, J3Cst::CONSTANT_Integer);
	return ctpValues[idx];
}

uint64_t J3Class::longAt(uint16_t idx) {
	check(idx, J3Cst::CONSTANT_Long);
	return ((uint64_t)ctpValues[idx] << 32) + (uint64_t)ctpValues[idx+1];
}

J3Method* J3Class::methodAt(uint16_t idx, uint16_t access) {
	check(idx, J3Cst::CONSTANT_Methodref);
	J3Method* res = (J3Method*)ctpResolved[idx];
	
	if(res) {
		if((res->access() & J3Cst::ACC_STATIC) != (access & J3Cst::ACC_STATIC))
			J3::classFormatError(this, L"inconstitent use of virtual and static methods"); 
		return res;
	}

	uint16_t ntIdx = ctpValues[idx] & 0xffff;
	J3Class* cl = classAt(ctpValues[idx] >> 16);

	check(ntIdx, J3Cst::CONSTANT_NameAndType);
	const vmkit::Name* name = nameAt(ctpValues[ntIdx] >> 16);
	const vmkit::Name* sign = nameAt(ctpValues[ntIdx] & 0xffff);

	res = loader()->method(access, cl, name, sign);

	return res;
}

J3Field* J3Class::fieldAt(uint16_t idx, uint16_t access) {
	check(idx, J3Cst::CONSTANT_Fieldref);
	J3Field* res = (J3Field*)ctpResolved[idx];

	if(res) {
		if((res->access() & J3Cst::ACC_STATIC) != (access & J3Cst::ACC_STATIC))
			J3::classFormatError(this, L"inconstitent use of virtual and static field"); 
		return res;
	}

	uint16_t ntIdx = ctpValues[idx] & 0xffff;
	J3Class* cl = classAt(ctpValues[idx] >> 16);

	check(ntIdx, J3Cst::CONSTANT_NameAndType);
	const vmkit::Name* name = nameAt(ctpValues[ntIdx] >> 16);
	const J3Type*      type = loader()->getType(this, nameAt(ctpValues[ntIdx] & 0xffff));
	
	res = J3Cst::isStatic(access) ? cl->findStaticField(name, type) : cl->findVirtualField(name, type);

	return res;
}

J3Class* J3Class::classAt(uint16_t idx) {
	check(idx, J3Cst::CONSTANT_Class);
	J3Class* res = (J3Class*)ctpResolved[idx];

	if(res)
		return res;
	
	const vmkit::Name* name = nameAt(ctpValues[idx]);
	if(name->cStr()[0] == J3Cst::ID_Array)
		J3::internalError(L"implement me: classAt with array");
	else
		res = loader()->getClass(name);

	ctpResolved[idx] = res;

	return res;
}

const vmkit::Name*  J3Class::nameAt(uint16_t idx) {
	check(idx, J3Cst::CONSTANT_Utf8);
	const vmkit::Name* res = (const vmkit::Name*)ctpResolved[idx];

	if(res)
		return res;

	J3Reader reader(_bytes);

	reader.seek(ctpValues[idx], reader.SeekSet);

	uint16_t len = reader.readU2(), i=0, n=0;

	res = loader()->vm()->names()->get((const char*)reader.pointer(), 0, len);

	ctpResolved[idx] = (void*)res;

	return res;
}

void J3Class::check(uint16_t idx, uint32_t id) {
	if(idx > nbCtp || (id != -1 && ctpTypes[idx] != id))
		J3::classFormatError(this, L"wrong constant pool type %d at index %d for %d", id, idx, nbCtp);
}

void J3Class::createLLVMTypes() {
	J3Mangler mangler(this);

	mangler.mangle("static_")->mangle(name());

	staticLayout._llvmType = 
		llvm::PointerType::getUnqual(llvm::StructType::create(loader()->vm()->llvmContext(), mangler.cStr()));
	_llvmType = llvm::PointerType::getUnqual(llvm::StructType::create(loader()->vm()->llvmContext(), mangler.cStr()+7));

	mangler.mangle("_2");
		
	_nativeNameLength = mangler.length() - 6;
	_nativeName = (char*)loader()->allocator()->allocate(_nativeNameLength + 1);

	_nativeName[0] = 'L';
	memcpy(_nativeName + 1, mangler.cStr()+7, _nativeNameLength); /* copy the 0 */

	loader()->addSymbol(_nativeName, this);
}

llvm::Type* J3Class::staticLLVMType() {
	llvm::Type* res = staticLayout._llvmType;

	if(!res) {
		createLLVMTypes();
		res = staticLayout._llvmType;
	}

	return res;
}

llvm::Type* J3Class::virtualLLVMType() {
	llvm::Type* res = _llvmType;

	if(!res) {
		createLLVMTypes();
		res = _llvmType;
	}

	return res;
}

llvm::Type* J3Class::llvmType() {
	return virtualLLVMType();
}

void J3Field::dump() {
	printf("Field: %ls %ls::%ls (%d)\n", type()->name()->cStr(), cl()->name()->cStr(), name()->cStr(), access());
}


J3Attribute* J3Attributes::attribute(size_t n) { 
	if(n >= _nbAttributes)
		J3::internalError(L"should not happen");
	return _attributes + n; 
}

J3Attribute* J3Attributes::lookup(const vmkit::Name* id) {
	for(size_t i=0; i<_nbAttributes; i++) {
		if(_attributes[i].id() == id)
			return _attributes+i;
	}

	return 0;
}

/*  
 *  ------------ J3ArrayClass ------------
 */
J3ArrayClass::J3ArrayClass(J3ClassLoader* loader, J3Type* component, const vmkit::Name* name) : J3ObjectType(loader, name) {
	_component = component;

	if(!name) {
		const vmkit::Name* compName = component->name();
		uint32_t           len = compName->length();
		wchar_t            buf[len + 16];
		uint32_t           pos = 0;

		//printf("     build array of %ls\n", component->name()->cStr());
		buf[pos++] = J3Cst::ID_Array;
	
		if(component->isClass())
			buf[pos++] = J3Cst::ID_Classname;
		memcpy(buf+pos, compName->cStr(), len * sizeof(wchar_t));
		pos += len;
		if(component->isClass())
			buf[pos++] = J3Cst::ID_End;
		buf[pos] = 0;

		_name = loader->vm()->names()->get(buf);
	}
}

void J3ArrayClass::doResolve(J3Field* hiddenFields, size_t nbHiddenFields) {
	lock();
	if(status < RESOLVED) {
		status = RESOLVED;
		J3Class* objectClass = loader()->vm()->objectClass;
		objectClass->resolve();
		_vt = J3VirtualTable::create(this);
	}
	unlock();
}
	
void J3ArrayClass::doInitialise() {
	resolve();
	status = INITED;
}

llvm::GlobalValue* J3ArrayClass::llvmDescriptor(llvm::Module* module) {
	return llvm::cast<llvm::GlobalValue>(module->getOrInsertGlobal(nativeName(), loader()->vm()->typeJ3ArrayClass));
}

llvm::Type* J3ArrayClass::llvmType() {
	if(!_llvmType) {
		llvm::Type* body[2] = {
			loader()->vm()->typeJ3ArrayObject,
			llvm::ArrayType::get(component()->llvmType(), 0) /* has to be called first */
		};
		uint32_t len = component()->nativeNameLength();

		_nativeNameLength = len + 2;
		_nativeName = (char*)loader()->allocator()->allocate(_nativeNameLength + 1);
		_nativeName[0] = '_';
		_nativeName[1] = '3';
		memcpy(_nativeName+2, component()->nativeName(), len);
		_nativeName[_nativeNameLength] = 0;

		_llvmType = llvm::PointerType::getUnqual(llvm::StructType::create(loader()->vm()->llvmContext(), 
																																			body,
																																			_nativeName));

		loader()->addSymbol(_nativeName, this);
	}
	return _llvmType;
}

/*  
 *  ------------ J3Primitive ------------
 */
J3Primitive::J3Primitive(J3ClassLoader* loader, char id, llvm::Type* type) : J3Type(loader, loader->vm()->names()->get(id)) {
	_llvmType = type;
	_nativeName = (char*)loader->allocator()->allocate(2);
	_nativeName[0] = id;
	_nativeName[1] = 0;
	_nativeNameLength = 1;
	_vt = J3VirtualTable::create(this);
}