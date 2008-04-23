//===----------- JavaObject.h - Java object definition -------------------===//
//
//                              JnJVM
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef JNJVM_JAVA_OBJECT_H
#define JNJVM_JAVA_OBJECT_H

#include <vector>

#include "llvm/Constants.h"
#include "llvm/Type.h"
#include "llvm/ExecutionEngine/GenericValue.h"

#include "mvm/Object.h"
#include "mvm/Threads/Locks.h"

#include "types.h"

namespace jnjvm {

class CommonClass;
class JavaField;
class JavaObject;
class JavaThread;
class UTF8;

class JavaCond : public mvm::Object {
public:
  static VirtualTable* VT;
  std::vector<JavaThread*> threads;
  
  static JavaCond* allocate();

  void notify();
  void notifyAll();
  void wait(JavaThread* th);
  void remove(JavaThread* th);

  virtual void TRACER;
};


class LockObj : public mvm::Object {
public:
  static VirtualTable* VT;
  mvm::Lock *lock;
  JavaCond* varcond;

  virtual void print(mvm::PrintBuffer* buf) const;
  virtual void TRACER;

  static LockObj* allocate();
  void aquire();
  void release();
  bool owner();
};

class JavaObject : public mvm::Object {
public:
  static VirtualTable* VT;
  CommonClass* classOf;
  LockObj* lockObj;

  static mvm::Lock* globalLock;
  static const llvm::Type* llvmType;
  
  virtual void print(mvm::PrintBuffer* buf) const;
  virtual void TRACER;
  
  void aquire();
  void unlock();
  void waitIntern(struct timeval *info, bool timed);
  void wait();
  void timedWait(struct timeval &info);
  void notify();
  void notifyAll();
  void initialise(CommonClass* cl) {
    this->classOf = cl; 
    this->lockObj = 0;
  }

  bool checkCast(const UTF8* name);
  bool instanceOfString(const UTF8* name);
  bool instanceOf(CommonClass* cl);

  static llvm::ConstantInt* classOffset();

#ifdef SIGSEGV_THROW_NULL
  #define verifyNull(obj) {}
#else
  #define verifyNull(obj) \
    if (obj == 0) JavaThread::get()->isolate->nullPointerException("");
#endif
  
  llvm::GenericValue operator()(JavaField* field);
  void operator()(JavaField* field, float val);
  void operator()(JavaField* field, double val);
  void operator()(JavaField* field, sint64 val);
  void operator()(JavaField* field, uint32 val);
  void operator()(JavaField* field, JavaObject* val);
 
#ifndef WITHOUT_VTABLE
  // Some of these are final. I could probably remove them from the list.
  virtual void JavaInit() {}
  virtual void JavaEquals() {}
  virtual void JavaHashCode() {}
  virtual void JavaToString() {}
  virtual void JavaFinalize() {}
  virtual void JavaClone() {}
  virtual void JavaGetClass() {}
  virtual void JavaNotify() {}
  virtual void JavaNotifyAll() {}
  virtual void JavaWait() {}
  virtual void JavaWait(sint64) {}
  virtual void JavaWait(sint64, sint32) {}
#endif
};


} // end namespace jnjvm

#endif
