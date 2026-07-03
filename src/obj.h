#ifndef prism_obj_h
#define prism_obj_h
#include <stdio.h>
#include "common.h"
#include "value.h"
#include "chunk.h"
#include "table.h"

typedef struct Obj Obj;
typedef struct ObjString ObjString;
typedef struct ObjFunction ObjFunction;
typedef struct ObjUpvalue ObjUpvalue;
typedef struct ObjClosure ObjClosure;
typedef struct ObjNative ObjNative;
typedef struct ObjClass ObjClass;
typedef struct ObjInstance ObjInstance;
typedef struct ObjBoundMethod ObjBoundMethod;
typedef struct ObjMatrix ObjMatrix;

#define OBJ_TYPE(value)        (AS_OBJ(value)->type)

#define IS_BOUND_METHOD(value) isObjType(value, OBJ_BOUND_METHOD)
#define IS_INSTANCE(value)     isObjType(value, OBJ_INSTANCE)
#define IS_CLASS(value)        isObjType(value, OBJ_CLASS)
#define IS_CLOSURE(value)      isObjType(value, OBJ_CLOSURE)
#define IS_FUNCTION(value)     isObjType(value, OBJ_FUNCTION)
#define IS_NATIVE(value)       isObjType(value, OBJ_NATIVE)
#define IS_STRING(value)       isObjType(value, OBJ_STRING)

#define AS_BOUND_METHOD(value) ((ObjBoundMethod*)AS_OBJ(value))
#define AS_INSTANCE(value)     ((ObjInstance*)AS_OBJ(value))
#define AS_CLASS(value)        ((ObjClass*)AS_OBJ(value))
#define AS_CLOSURE(value)      ((ObjClosure*)AS_OBJ(value))
#define AS_FUNCTION(value)     ((ObjFunction*)AS_OBJ(value))
#define AS_NATIVE(value)       (((ObjNative*)AS_OBJ(value))->function)
#define AS_STRING(value)       ((ObjString*)AS_OBJ(value))
#define AS_CSTRING(value)      (((ObjString*)AS_OBJ(value))->chars)
#define IS_ARRAY(value)        isObjType(value, OBJ_ARRAY)
#define AS_ARRAY(value)        ((ObjArray*)AS_OBJ(value))
#define IS_FILE(value)        isObjType(value, OBJ_FILE)
#define AS_FILE(value)        ((ObjFile*)AS_OBJ(value))
#define IS_MATRIX(value)     isObjType(value, OBJ_MATRIX)
#define AS_MATRIX(value)     ((ObjMatrix*)AS_OBJ(value))

typedef enum {
  OBJ_BOUND_METHOD,
  OBJ_INSTANCE,
  OBJ_CLASS,
  OBJ_CLOSURE,
  OBJ_NATIVE,
  OBJ_ARRAY,
  OBJ_FUNCTION,
  OBJ_STRING,
  OBJ_UPVALUE,
  OBJ_FILE,
  OBJ_MATRIX
} ObjType;



struct Obj {
  ObjType type;
  bool isMarked;
  struct Obj* next;
};
typedef struct {
  Obj obj;
  FILE* file;
  char* mode;
  bool is_Open;
} ObjFile;

struct ObjMatrix 
{
    Obj obj;
    int rows;
    int cols;
    int count;
    Value* data;
};


struct ObjFunction {
  Obj obj;
  int arity;
  int upvalueCount;
  Chunk chunk;
  ObjString* name;
};



struct ObjUpvalue {
  Obj obj;
  Value* location;
  Value closed;
  struct ObjUpvalue* next;
};



struct ObjClosure {
  Obj obj;
  ObjFunction* function;
  ObjUpvalue** upvalues;
  int upvalueCount;
};


typedef Value (*NativeFn)(int argCount, Value* args);

struct ObjNative {
  Obj obj;
  NativeFn function;
};


struct ObjString {
  Obj obj;
  int length;
  char* chars;
  uint32_t hash;
};


struct ObjClass {
  Obj obj;
  ObjString* name;
  Table methods;
};



struct ObjInstance {
  Obj obj;
  ObjClass* klass;
  Table fields;
};


struct ObjBoundMethod {
  Obj obj;
  Value receiver;
  ObjClosure* method;
};

typedef struct {
    Obj obj;
    int count;
    int capacity;
    Value* items;
} ObjArray;

ObjClass* newClass(ObjString* name);
ObjClosure* newClosure(ObjFunction* function);
ObjFunction* newFunction();
ObjNative* newNative(NativeFn function);
ObjString* takeString(char* chars, int length);
ObjString* copyString(const char* chars, int length);
ObjUpvalue* newUpvalue(Value* slot);
ObjInstance* newInstance(ObjClass* klass);
ObjBoundMethod* newBoundMethod(Value receiver, ObjClosure* method);
ObjArray* newArray();
ObjFile* newFile(FILE* c_file, const char* mode);
ObjMatrix* newMatrix(int rows, int cols);
void printObject(Value value);

static inline bool isObjType(Value value, ObjType type) {
  return IS_OBJ(value) && AS_OBJ(value)->type == type;
}

#endif