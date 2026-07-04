#ifndef prism_vm_h
#define prism_vm_h
#include "common.h"
#include "value.h"
#include "chunk.h"
#include "table.h"
#include "obj.h"
#define FRAMES_MAX 64
#define STACK_MAX (FRAMES_MAX * UINT8_COUNT)
typedef struct {
  ObjClosure* closure;
  uint8_t* ip;
  Value* slots;
  const char* filePath;
} CallFrame;
typedef struct {
  CallFrame frames[FRAMES_MAX];
  int frameCount;
  Value stack[STACK_MAX];
  Value* stackTop;
  Table globals;
  ObjString* initString;
  Table strings;
  Table importedModules;
  ObjUpvalue* openUpvalues;
  size_t bytesAllocated;
  size_t nextGC;
  Obj* objects;
  int grayCount;
  int grayCapacity;
  Obj** grayStack;
  const char* currentFilePath;
} VM;
typedef enum {
  INTERPRET_OK,
  INTERPRET_COMPILE_ERROR,
  INTERPRET_RUNTIME_ERROR
} InterpretResult;
extern VM vm;
void defineNative(const char* name, NativeFn function);
void initVM();
void freeVM();
InterpretResult interpret(const char* source);
InterpretResult run();
void push(Value value);
Value pop();
bool call(ObjClosure* closure, int argCount);
void runtimeError(const char* format, ...);
#endif