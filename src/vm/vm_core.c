#include "common.h"
#include "debug.h"
#include "vm.h"
#include "compiler.h"
#include "obj.h"
#include "memory.h"
#include "package.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>

VM vm;

static Value clockNative(int argCount, Value* args) {
    (void)argCount; (void)args;
    return NUMBER_VAL((double)clock() / CLOCKS_PER_SEC);
}

static void resetStack() {
    vm.stackTop     = vm.stack;
    vm.frameCount   = 0;
    vm.openUpvalues = NULL;
}

void runtimeError(const char* format, ...) {
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fputs("\n", stderr);
    for (int i = vm.frameCount - 1; i >= 0; i--) {
        CallFrame*   frame       = &vm.frames[i];
        ObjFunction* function    = frame->closure->function;
        size_t       instruction = frame->ip - function->chunk.code - 1;
        fprintf(stderr, "[line %d] in ", function->chunk.lines[instruction]);
        if (function->name == NULL) fprintf(stderr, "script\n");
        else fprintf(stderr, "%s()\n", function->name->chars);
    }
    resetStack();
}

void defineNative(const char* name, NativeFn function) {
    push(OBJ_VAL(copyString(name, (int)strlen(name))));
    push(OBJ_VAL(newNative(function)));
    tableSet(&vm.globals, AS_STRING(vm.stack[0]), vm.stack[1]);
    pop();
    pop();
}

void initVM() {
    resetStack();
    vm.objects        = NULL;
    vm.bytesAllocated = 0;
    vm.nextGC         = 1024 * 1024;
    vm.grayCount      = 0;
    vm.grayCapacity   = 0;
    vm.grayStack      = NULL;
    initTable(&vm.globals);
    initTable(&vm.strings);
    initTable(&vm.importedModules);
    vm.initString = copyString("init", 4);
    defineNative("clock", clockNative);
}

void freeVM() {
    vm.initString = NULL;
    freeTable(&vm.globals);
    freeTable(&vm.strings);
    freeTable(&vm.importedModules);
    freeObjects();
}

void push(Value value) {
    *vm.stackTop = value;
    vm.stackTop++;
}

Value pop() {
    vm.stackTop--;
    return *vm.stackTop;
}

Value peek(int distance) {
    return vm.stackTop[-1 - distance];
}

bool call(ObjClosure* closure, int argCount) {
    if (argCount != closure->function->arity) {
        runtimeError("Expected %d arguments but got %d.", closure->function->arity, argCount);
        return false;
    }
    if (vm.frameCount == FRAMES_MAX) { runtimeError("Stack overflow."); return false; }
    CallFrame* frame = &vm.frames[vm.frameCount++];
    frame->closure  = closure;
    frame->ip       = closure->function->chunk.code;
    frame->slots    = vm.stackTop - argCount - 1;
    frame->filePath = vm.currentFilePath;
    return true;
}

bool callValue(Value callee, int argCount) {
    if (IS_OBJ(callee)) {
        switch (OBJ_TYPE(callee)) {
            case OBJ_BOUND_METHOD: {
                ObjBoundMethod* bound = AS_BOUND_METHOD(callee);
                vm.stackTop[-argCount - 1] = bound->receiver;
                return call(bound->method, argCount);
            }
            case OBJ_CLASS: {
                ObjClass* klass = AS_CLASS(callee);
                vm.stackTop[-argCount - 1] = OBJ_VAL(newInstance(klass));
                Value initializer;
                if (tableGet(&klass->methods, vm.initString, &initializer)) {
                    return call(AS_CLOSURE(initializer), argCount);
                } else if (argCount != 0) {
                    runtimeError("Expected 0 arguments but got %d.", argCount);
                    return false;
                }
                return true;
            }
            case OBJ_CLOSURE: return call(AS_CLOSURE(callee), argCount);
            case OBJ_NATIVE: {
                NativeFn native = AS_NATIVE(callee);
                Value result = native(argCount, vm.stackTop - argCount);
                vm.stackTop -= argCount + 1;
                push(result);
                return true;
            }
            default: break;
        }
    }
    runtimeError("Can only call functions and classes.");
    return false;
}

bool invokeFromClass(ObjClass* klass, ObjString* name, int argCount) {
    Value method;
    if (!tableGet(&klass->methods, name, &method)) {
        runtimeError("Undefined property '%s'.", name->chars);
        return false;
    }
    return call(AS_CLOSURE(method), argCount);
}

bool invoke(ObjString* name, int argCount) {
    Value receiver = peek(argCount);
    if (!IS_INSTANCE(receiver)) { runtimeError("Only instances have methods."); return false; }
    ObjInstance* instance = AS_INSTANCE(receiver);
    Value value;
    if (tableGet(&instance->fields, name, &value)) {
        vm.stackTop[-argCount - 1] = value;
        return callValue(value, argCount);
    }
    return invokeFromClass(instance->klass, name, argCount);
}

bool bindMethod(ObjClass* klass, ObjString* name) {
    Value method;
    if (!tableGet(&klass->methods, name, &method)) {
        runtimeError("Undefined property '%s'.", name->chars);
        return false;
    }
    ObjBoundMethod* bound = newBoundMethod(peek(0), AS_CLOSURE(method));
    pop();
    push(OBJ_VAL(bound));
    return true;
}

ObjUpvalue* captureUpvalue(Value* local) {
    ObjUpvalue* prevUpvalue = NULL;
    ObjUpvalue* upvalue     = vm.openUpvalues;
    while (upvalue != NULL && upvalue->location > local) {
        prevUpvalue = upvalue;
        upvalue     = upvalue->next;
    }
    if (upvalue != NULL && upvalue->location == local) return upvalue;
    ObjUpvalue* createdUpvalue = newUpvalue(local);
    createdUpvalue->next = upvalue;
    if (prevUpvalue == NULL) vm.openUpvalues = createdUpvalue;
    else prevUpvalue->next = createdUpvalue;
    return createdUpvalue;
}

void closeUpvalues(Value* last) {
    while (vm.openUpvalues != NULL && vm.openUpvalues->location >= last) {
        ObjUpvalue* upvalue = vm.openUpvalues;
        upvalue->closed     = *upvalue->location;
        upvalue->location   = &upvalue->closed;
        vm.openUpvalues     = upvalue->next;
    }
}

void defineMethod(ObjString* name) {
    Value method    = peek(0);
    ObjClass* klass = AS_CLASS(peek(1));
    tableSet(&klass->methods, name, method);
    pop();
}

bool isFalsey(Value value) {
    return IS_NIL(value) || (IS_BOOL(value) && !AS_BOOL(value));
}

void concatenate() {
    ObjString* b  = AS_STRING(peek(0));
    ObjString* a  = AS_STRING(peek(1));
    int length    = a->length + b->length;
    char* chars   = ALLOCATE(char, length + 1);
    memcpy(chars, a->chars, a->length);
    memcpy(chars + a->length, b->chars, b->length);
    chars[length] = '\0';
    ObjString* result = takeString(chars, length);
    pop(); pop();
    push(OBJ_VAL(result));
}

uint8_t readByte(CallFrame* frame) {
    size_t ipIndex = frame->ip - frame->closure->function->chunk.code;
    if (ipIndex >= (size_t)frame->closure->function->chunk.count) {
        fprintf(stderr, "READ_BYTE out of bounds: ipIndex=%zu chunk.count=%d frameCount=%d\n",
            ipIndex, frame->closure->function->chunk.count, vm.frameCount);
        exit(1);
    }
    return *frame->ip++;
}

void normalizePath(char* path) {
    char result[1024] = {0};
    char temp[1024];
    strncpy(temp, path, sizeof(temp));
    int isAbsolute = (path[0] == '/');
    char* parts[100];
    int count = 0;
    char* p = temp;
    char* token;
    while ((token = strtok(p, "/")) != NULL) {
        p = NULL;
        if (strcmp(token, "..") == 0) { if (count > 0) count--; }
        else if (strcmp(token, ".") != 0 && strlen(token) > 0) { parts[count++] = token; }
    }
    result[0] = '\0';
    if (isAbsolute) strcat(result, "/");
    for (int i = 0; i < count; i++) {
        if (i > 0) strcat(result, "/");
        strcat(result, parts[i]);
    }
    strncpy(path, result, 1024);
}

InterpretResult interpret(const char* source) {
    ObjFunction* function = compile(source);
    if (function == NULL) return INTERPRET_COMPILE_ERROR;
    push(OBJ_VAL(function));
    ObjClosure* closure = newClosure(function);
    pop();
    push(OBJ_VAL(closure));
    call(closure, 0);
    return run();
}