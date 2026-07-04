#include "common.h"
#include "debug.h"
#include "vm.h"
#include "compiler.h"
#include "obj.h"
#include "memory.h"
#include "package.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>

extern uint8_t readByte(CallFrame* frame);
extern void normalizePath(char* path);
bool callValue(Value callee, int argCount);
bool invoke(ObjString* name, int argCount);
bool invokeFromClass(ObjClass* klass, ObjString* name, int argCount);
bool bindMethod(ObjClass* klass, ObjString* name);
ObjUpvalue* captureUpvalue(Value* local);
void closeUpvalues(Value* last);
void defineMethod(ObjString* name);
bool isFalsey(Value value);
void concatenate();
Value peek(int distance);

InterpretResult run() {
#define READ_BYTE()      readByte(frame)
#define READ_SHORT() \
(frame->ip += 2, (uint16_t)((frame->ip[-2] << 8) | frame->ip[-1]))
#define READ_CONSTANT() \
(frame->closure->function->chunk.constants.values[READ_BYTE()])
#define READ_STRING()   AS_STRING(READ_CONSTANT())
#define BINARY_OP(valueType, op) \
do { \
if (!IS_NUMBER(peek(0)) || !IS_NUMBER(peek(1))) { \
runtimeError("Operands must be numbers."); \
return INTERPRET_RUNTIME_ERROR; \
} \
double b = AS_NUMBER(pop()); \
double a = AS_NUMBER(pop()); \
push(valueType(a op b)); \
} while (false)

CallFrame* frame = &vm.frames[vm.frameCount - 1];

for (;;) {
#ifdef DEBUG_TRACE_EXECUTION
    printf("          ");
    for (Value* slot = vm.stack; slot < vm.stackTop; slot++) {
        printf("[ "); printValue(*slot); printf(" ]");
    }
    printf("\n");
    disassembleInstruction(&frame->closure->function->chunk,
        (int)(frame->ip - frame->closure->function->chunk.code));
#endif

    uint8_t instruction;
    switch (instruction = READ_BYTE()) {

        case OP_CONSTANT: { Value constant = READ_CONSTANT(); push(constant); break; }
        case OP_NIL:   push(NIL_VAL);         break;
        case OP_TRUE:  push(BOOL_VAL(true));  break;
        case OP_FALSE: push(BOOL_VAL(false)); break;
        case OP_POP:   pop();                  break;

        case OP_GET_LOCAL: { uint8_t slot = READ_BYTE(); push(frame->slots[slot]); break; }
        case OP_SET_LOCAL: { uint8_t slot = READ_BYTE(); frame->slots[slot] = peek(0); break; }

        case OP_GET_GLOBAL: {
            ObjString* name = READ_STRING();
            Value value;
            if (!tableGet(&vm.globals, name, &value)) {
                runtimeError("Undefined variable '%s'.", name->chars);
                return INTERPRET_RUNTIME_ERROR;
            }
            push(value);
            break;
        }
        case OP_DEFINE_GLOBAL: {
            ObjString* name = READ_STRING();
            tableSet(&vm.globals, name, peek(0));
            pop();
            break;
        }
        case OP_SET_GLOBAL: {
            ObjString* name = READ_STRING();
            if (tableSet(&vm.globals, name, peek(0))) {
                tableDelete(&vm.globals, name);
                runtimeError("Undefined variable '%s'.", name->chars);
                return INTERPRET_RUNTIME_ERROR;
            }
            break;
        }

        case OP_GET_UPVALUE: { uint8_t slot = READ_BYTE(); push(*frame->closure->upvalues[slot]->location); break; }
        case OP_SET_UPVALUE: { uint8_t slot = READ_BYTE(); *frame->closure->upvalues[slot]->location = peek(0); break; }
        case OP_CLOSE_UPVALUE: closeUpvalues(vm.stackTop - 1); pop(); break;

        case OP_GET_PROPERTY: {
            if (!IS_INSTANCE(peek(0))) { runtimeError("Only instances have properties."); return INTERPRET_RUNTIME_ERROR; }
            ObjInstance* instance = AS_INSTANCE(peek(0));
            ObjString*   name     = READ_STRING();
            Value value;
            if (tableGet(&instance->fields, name, &value)) { pop(); push(value); break; }
            if (!bindMethod(instance->klass, name)) return INTERPRET_RUNTIME_ERROR;
            break;
        }
        case OP_SET_PROPERTY: {
            if (!IS_INSTANCE(peek(1))) { runtimeError("Only instances have fields."); return INTERPRET_RUNTIME_ERROR; }
            ObjInstance* instance = AS_INSTANCE(peek(1));
            tableSet(&instance->fields, READ_STRING(), peek(0));
            Value value = pop(); pop(); push(value);
            break;
        }

        case OP_EQUAL: { Value b = pop(); Value a = pop(); push(BOOL_VAL(valuesEqual(a, b))); break; }
        case OP_GREATER:  BINARY_OP(BOOL_VAL,   >); break;
        case OP_LESS:     BINARY_OP(BOOL_VAL,   <); break;
        case OP_ADD: {
            if (IS_STRING(peek(0)) && IS_STRING(peek(1))) {
                concatenate();
            } else if (IS_NUMBER(peek(0)) && IS_NUMBER(peek(1))) {
                double b = AS_NUMBER(pop()); double a = AS_NUMBER(pop());
                push(NUMBER_VAL(a + b));
            } else {
                runtimeError("Operands must be two numbers or two strings.");
                return INTERPRET_RUNTIME_ERROR;
            }
            break;
        }
        case OP_SUBTRACT: BINARY_OP(NUMBER_VAL, -); break;
        case OP_MULTIPLY: BINARY_OP(NUMBER_VAL, *); break;
        case OP_DIVIDE:   BINARY_OP(NUMBER_VAL, /); break;
        case OP_MODULO: {
            double b = AS_NUMBER(pop()); double a = AS_NUMBER(pop());
            push(NUMBER_VAL(fmod(a, b)));
            break;
        }
        case OP_NOT:    push(BOOL_VAL(isFalsey(pop()))); break;
        case OP_NEGATE:
            if (!IS_NUMBER(peek(0))) { runtimeError("Operand must be a number."); return INTERPRET_RUNTIME_ERROR; }
            push(NUMBER_VAL(-AS_NUMBER(pop())));
            break;

        case OP_PRINT: printValue(pop()); printf("\n"); break;

        case OP_JUMP:          { uint16_t offset = READ_SHORT(); frame->ip += offset; break; }
        case OP_JUMP_IF_FALSE: { uint16_t offset = READ_SHORT(); if (isFalsey(peek(0))) frame->ip += offset; break; }
        case OP_LOOP:          { uint16_t offset = READ_SHORT(); frame->ip -= offset; break; }

        case OP_CALL: {
            int argCount = READ_BYTE();
            if (!callValue(peek(argCount), argCount)) return INTERPRET_RUNTIME_ERROR;
            frame = &vm.frames[vm.frameCount - 1];
            break;
        }
        case OP_CLOSURE: {
            ObjFunction* function = AS_FUNCTION(READ_CONSTANT());
            ObjClosure*  closure  = newClosure(function);
            push(OBJ_VAL(closure));
            for (int i = 0; i < closure->upvalueCount; i++) {
                uint8_t isLocal = READ_BYTE();
                uint8_t index   = READ_BYTE();
                if (isLocal) closure->upvalues[i] = captureUpvalue(frame->slots + index);
                else         closure->upvalues[i] = frame->closure->upvalues[index];
            }
            break;
        }
        case OP_RETURN: {
            Value result = pop();
            closeUpvalues(frame->slots);
            vm.frameCount--;
            if (vm.frameCount == 0) { pop(); return INTERPRET_OK; }
            vm.stackTop = frame->slots;
            push(result);
            frame = &vm.frames[vm.frameCount - 1];
            vm.currentFilePath = frame->filePath;
            break;
        }

        case OP_IMPORT: {
            ObjString* path = AS_STRING(READ_CONSTANT());
            char fullPath[1024];
            int isPackageName = (strchr(path->chars, '/') == NULL &&
                                strchr(path->chars, '\\') == NULL &&
                                strstr(path->chars, ".pr") == NULL);
            if (isPackageName) {
                char* resolved = resolvePackage(path->chars);
                if (!resolved) { runtimeError("Could not resolve package '%s'.", path->chars); return INTERPRET_RUNTIME_ERROR; }
                snprintf(fullPath, sizeof(fullPath), "%s", resolved);
                free(resolved);
            } else if (path->chars[0] == '/' || (path->chars[1] == ':' && path->chars[2] == '\\')) {
                snprintf(fullPath, sizeof(fullPath), "%s", path->chars);
            } else {
                const char* slash     = strrchr(vm.currentFilePath, '/');
                const char* backslash = strrchr(vm.currentFilePath, '\\');
                if (backslash > slash) slash = backslash;
                if (slash != NULL) {
                    int dirLen = (int)(slash - vm.currentFilePath) + 1;
                    snprintf(fullPath, sizeof(fullPath), "%.*s%s", dirLen, vm.currentFilePath, path->chars);
                } else {
                    snprintf(fullPath, sizeof(fullPath), "%s", path->chars);
                }
            }
            normalizePath(fullPath);
            ObjString* pathKey = copyString(fullPath, strlen(fullPath));
            Value dummy;
            if (tableGet(&vm.importedModules, pathKey, &dummy)) break;
            tableSet(&vm.importedModules, pathKey, BOOL_VAL(true));
            FILE* file = fopen(fullPath, "rb");
            if (file == NULL) { runtimeError("Could not open module '%s'.", fullPath); return INTERPRET_RUNTIME_ERROR; }
            fseek(file, 0L, SEEK_END);
            size_t size = ftell(file);
            rewind(file);
            char* source = (char*)malloc(size + 1);
            fread(source, sizeof(char), size, file);
            source[size] = '\0';
            fclose(file);
            vm.currentFilePath = strdup(fullPath);
            ObjFunction* function = compile(source);
            free(source);
            if (function == NULL) return INTERPRET_COMPILE_ERROR;
            ObjClosure* closure = newClosure(function);
            push(OBJ_VAL(closure));
            callValue(OBJ_VAL(closure), 0);
            frame = &vm.frames[vm.frameCount - 1];
            frame->filePath    = strdup(fullPath);
            vm.currentFilePath = frame->filePath;
            break;
        }

        case OP_CLASS:   push(OBJ_VAL(newClass(READ_STRING()))); break;
        case OP_INHERIT: {
            Value superclass = peek(1);
            if (!IS_CLASS(superclass)) { runtimeError("Superclass must be a class."); return INTERPRET_RUNTIME_ERROR; }
            ObjClass* subclass = AS_CLASS(peek(0));
            tableAddAll(&AS_CLASS(superclass)->methods, &subclass->methods);
            pop();
            break;
        }
        case OP_METHOD: defineMethod(READ_STRING()); break;
        case OP_GET_SUPER: {
            ObjString* name      = READ_STRING();
            ObjClass*  superclass = AS_CLASS(pop());
            if (!bindMethod(superclass, name)) return INTERPRET_RUNTIME_ERROR;
            break;
        }
        case OP_INVOKE: {
            ObjString* method = READ_STRING();
            int argCount = READ_BYTE();
            if (!invoke(method, argCount)) return INTERPRET_RUNTIME_ERROR;
            frame = &vm.frames[vm.frameCount - 1];
            break;
        }
        case OP_SUPER_INVOKE: {
            ObjString* method     = READ_STRING();
            int        argCount   = READ_BYTE();
            ObjClass*  superclass = AS_CLASS(pop());
            if (!invokeFromClass(superclass, method, argCount)) return INTERPRET_RUNTIME_ERROR;
            frame = &vm.frames[vm.frameCount - 1];
            break;
        }

        case OP_BUILD_ARRAY: {
            int       count = READ_BYTE();
            ObjArray* array = newArray();
            push(OBJ_VAL(array));
            if (count > 0) { array->items = GROW_ARRAY(Value, array->items, 0, count); array->capacity = count; }
            for (int i = count - 1; i >= 0; i--) { array->items[array->count++] = vm.stackTop[-2 - i]; }
            vm.stackTop[-1 - count] = vm.stackTop[-1];
            vm.stackTop -= count;
            break;
        }
        case OP_GET_INDEX: {
            Value index = pop(); Value target = pop();
            if (!IS_ARRAY(target)) { runtimeError("Only arrays can be indexed."); return INTERPRET_RUNTIME_ERROR; }
            if (!IS_NUMBER(index)) { runtimeError("Array index must be a number."); return INTERPRET_RUNTIME_ERROR; }
            ObjArray* array = AS_ARRAY(target);
            int i = (int)AS_NUMBER(index);
            if (i < 0 || i >= array->count) { runtimeError("Array index out of bounds."); return INTERPRET_RUNTIME_ERROR; }
            push(array->items[i]);
            break;
        }
        case OP_SET_INDEX: {
            Value value = pop(); Value index = pop(); Value target = pop();
            if (!IS_ARRAY(target)) { runtimeError("Only arrays can be indexed."); return INTERPRET_RUNTIME_ERROR; }
            if (!IS_NUMBER(index)) { runtimeError("Array index must be a number."); return INTERPRET_RUNTIME_ERROR; }
            ObjArray* array = AS_ARRAY(target);
            int i = (int)AS_NUMBER(index);
            if (i < 0 || i >= array->count) { runtimeError("Array index out of bounds."); return INTERPRET_RUNTIME_ERROR; }
            array->items[i] = value;
            push(value);
            break;
        }

        default:
            fprintf(stderr, "Unknown opcode %d\n", instruction);
            return INTERPRET_RUNTIME_ERROR;
    }
}

#undef READ_BYTE
#undef READ_SHORT
#undef READ_CONSTANT
#undef READ_STRING
#undef BINARY_OP
}