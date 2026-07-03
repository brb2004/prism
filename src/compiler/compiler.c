#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "common.h"
#include "compiler.h"
#include "compiler_internal.h"
#include "scanner.h"
#include "memory.h"
#ifdef DEBUG_PRINT_CODE
#include "debug.h"
#endif

Parser parser;
Compiler* current = NULL;
ClassCompiler* currentClass = NULL;

Chunk* currentChunk() {
    return &current->function->chunk;
}

void errorAt(Token* token, const char* message) {
    if (parser.panicMode) return;
    parser.panicMode = true;
    fprintf(stderr, "[line %d] Error", token->line);
    if (token->type == TOKEN_EOF) {
        fprintf(stderr, " at end");
    } else if (token->type != TOKEN_ERROR) {
        fprintf(stderr, " at '%.*s'", token->length, token->start);
    }
    fprintf(stderr, ": %s\n", message);
    parser.hadError = true;
}

void error(const char* message) {
    errorAt(&parser.previous, message);
}

void errorAtCurrent(const char* message) {
    errorAt(&parser.current, message);
}

void advance() {
    parser.previous = parser.current;
    for (;;) {
        parser.current = scanToken();
        if (parser.current.type != TOKEN_ERROR) break;
        errorAtCurrent(parser.current.start);
    }
}

void consume(TokenType type, const char* message) {
    if (parser.current.type == type) { advance(); return; }
    errorAtCurrent(message);
}

bool check(TokenType type) {
    return parser.current.type == type;
}

bool match(TokenType type) {
    if (!check(type)) return false;
    advance();
    return true;
}

void emitByte(uint8_t byte) {
    writeChunk(currentChunk(), byte, parser.previous.line);
}

void emitBytes(uint8_t byte1, uint8_t byte2) {
    emitByte(byte1);
    emitByte(byte2);
}

void emitLoop(int loopStart) {
    emitByte(OP_LOOP);
    int offset = currentChunk()->count - loopStart + 2;
    if (offset > UINT16_MAX) error("Loop body too large.");
    emitByte((offset >> 8) & 0xff);
    emitByte(offset & 0xff);
}

int emitJump(uint8_t instruction) {
    emitByte(instruction);
    emitByte(0xff);
    emitByte(0xff);
    return currentChunk()->count - 2;
}

void emitReturn() {
    if (current->type == TYPE_INITIALIZER) {
        emitBytes(OP_GET_LOCAL, 0);
    } else {
        emitByte(OP_NIL);
    }
    emitByte(OP_RETURN);
}

uint8_t makeConstant(Value value) {
    int constant = addConstant(currentChunk(), value);
    if (constant > UINT8_MAX) {
        error("Too many constants in one chunk.");
        return 0;
    }
    return (uint8_t)constant;
}

void emitConstant(Value value) {
    emitBytes(OP_CONSTANT, makeConstant(value));
}

void patchJump(int offset) {
    int jump = currentChunk()->count - offset - 2;
    if (jump > UINT16_MAX) error("Too much code to jump over.");
    currentChunk()->code[offset]     = (jump >> 8) & 0xff;
    currentChunk()->code[offset + 1] = jump & 0xff;
}

void initCompiler(Compiler* compiler, FunctionType type) {
    compiler->enclosing  = current;
    compiler->function   = NULL;
    compiler->type       = type;
    compiler->localCount = 0;
    compiler->scopeDepth = 0;
    compiler->function   = newFunction();
    current = compiler;
    if (type != TYPE_SCRIPT) {
        current->function->name = copyString(parser.previous.start, parser.previous.length);
    }
    Local* local = &current->locals[current->localCount++];
    local->depth      = 0;
    local->isCaptured = false;
    if (type != TYPE_FUNCTION) {
        local->name.start  = "this";
        local->name.length = 4;
    } else {
        local->name.start  = "";
        local->name.length = 0;
    }
}

ObjFunction* endCompiler() {
    emitReturn();
    ObjFunction* function = current->function;
#ifdef DEBUG_PRINT_CODE
    if (!parser.hadError) {
        disassembleChunk(&function->chunk,
            function->name != NULL ? function->name->chars : "<script>");
    }
#endif
    current = current->enclosing;
    return function;
}

void beginScope() { current->scopeDepth++; }

void endScope() {
    current->scopeDepth--;
    while (current->localCount > 0 &&
           current->locals[current->localCount - 1].depth > current->scopeDepth) {
        if (current->locals[current->localCount - 1].isCaptured) {
            emitByte(OP_CLOSE_UPVALUE);
        } else {
            emitByte(OP_POP);
        }
        current->localCount--;
    }
}

uint8_t identifierConstant(Token* name) {
    return makeConstant(OBJ_VAL(copyString(name->start, name->length)));
}

bool identifiersEqual(Token* a, Token* b) {
    if (a->length != b->length) return false;
    return memcmp(a->start, b->start, a->length) == 0;
}

int resolveLocal(Compiler* compiler, Token* name) {
    for (int i = compiler->localCount - 1; i >= 0; i--) {
        Local* local = &compiler->locals[i];
        if (identifiersEqual(name, &local->name)) {
            if (local->depth == -1) error("Can't read local variable in its own initializer.");
            return i;
        }
    }
    return -1;
}

int addUpvalue(Compiler* compiler, uint8_t index, bool isLocal) {
    int upvalueCount = compiler->function->upvalueCount;
    for (int i = 0; i < upvalueCount; i++) {
        Upvalue* upvalue = &compiler->upvalues[i];
        if (upvalue->index == index && upvalue->isLocal == isLocal) return i;
    }
    if (upvalueCount == UINT8_COUNT) { error("Too many closure variables in function."); return 0; }
    compiler->upvalues[upvalueCount].isLocal = isLocal;
    compiler->upvalues[upvalueCount].index   = index;
    return compiler->function->upvalueCount++;
}

int resolveUpvalue(Compiler* compiler, Token* name) {
    if (compiler->enclosing == NULL) return -1;
    int local = resolveLocal(compiler->enclosing, name);
    if (local != -1) {
        compiler->enclosing->locals[local].isCaptured = true;
        return addUpvalue(compiler, (uint8_t)local, true);
    }
    int upvalue = resolveUpvalue(compiler->enclosing, name);
    if (upvalue != -1) return addUpvalue(compiler, (uint8_t)upvalue, false);
    return -1;
}

void addLocal(Token name) {
    if (current->localCount == UINT8_COUNT) { error("Too many local variables in function."); return; }
    Local* local      = &current->locals[current->localCount++];
    local->name       = name;
    local->depth      = -1;
    local->isCaptured = false;
}

void declareVariable() {
    if (current->scopeDepth == 0) return;
    Token* name = &parser.previous;
    for (int i = current->localCount - 1; i >= 0; i--) {
        Local* local = &current->locals[i];
        if (local->depth != -1 && local->depth < current->scopeDepth) break;
        if (identifiersEqual(name, &local->name)) error("Already a variable with this name in this scope.");
    }
    addLocal(*name);
}

uint8_t parseVariable(const char* errorMessage) {
    consume(TOKEN_IDENTIFIER, errorMessage);
    declareVariable();
    if (current->scopeDepth > 0) return 0;
    return identifierConstant(&parser.previous);
}

void markInitialized() {
    if (current->scopeDepth == 0) return;
    current->locals[current->localCount - 1].depth = current->scopeDepth;
}

void defineVariable(uint8_t global) {
    if (current->scopeDepth > 0) { markInitialized(); return; }
    emitBytes(OP_DEFINE_GLOBAL, global);
}

uint8_t argumentList() {
    uint8_t argCount = 0;
    if (!check(TOKEN_RIGHT_PAREN)) {
        do {
            expression();
            if (argCount == 255) error("Can't have more than 255 arguments.");
            argCount++;
        } while (match(TOKEN_COMMA));
    }
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after arguments.");
    return argCount;
}

Token syntheticToken(const char* text) {
    Token token;
    token.start  = text;
    token.length = (int)strlen(text);
    return token;
}

void namedVariable(Token name, bool canAssign) {
    uint8_t getOp, setOp;
    int arg = resolveLocal(current, &name);
    if (arg != -1) {
        getOp = OP_GET_LOCAL; setOp = OP_SET_LOCAL;
    } else if ((arg = resolveUpvalue(current, &name)) != -1) {
        getOp = OP_GET_UPVALUE; setOp = OP_SET_UPVALUE;
    } else {
        arg   = identifierConstant(&name);
        getOp = OP_GET_GLOBAL; setOp = OP_SET_GLOBAL;
    }
    if (canAssign && match(TOKEN_EQUAL)) {
        expression();
        emitBytes(setOp, (uint8_t)arg);
    } else {
        emitBytes(getOp, (uint8_t)arg);
    }
}

void markCompilerRoots() {
    Compiler* compiler = current;
    while (compiler != NULL) {
        markObject((Obj*)compiler->function);
        compiler = compiler->enclosing;
    }
}