#ifndef prism_compiler_internal_h
#define prism_compiler_internal_h

#include "common.h"
#include "scanner.h"
#include "obj.h"
#include "chunk.h"

typedef struct {
    Token current;
    Token previous;
    bool hadError;
    bool panicMode;
} Parser;

typedef enum {
    PREC_NONE,
    PREC_ASSIGNMENT,
    PREC_OR,
    PREC_AND,
    PREC_EQUALITY,
    PREC_COMPARISON,
    PREC_TERM,
    PREC_FACTOR,
    PREC_UNARY,
    PREC_CALL,
    PREC_PRIMARY
} Precedence;

typedef void (*ParseFn)(bool canAssign);

typedef struct {
    ParseFn prefix;
    ParseFn infix;
    Precedence precedence;
} ParseRule;

typedef struct {
    Token name;
    int depth;
    bool isCaptured;
} Local;

typedef struct {
    uint8_t index;
    bool isLocal;
} Upvalue;

typedef enum {
    TYPE_FUNCTION,
    TYPE_INITIALIZER,
    TYPE_SCRIPT,
    TYPE_METHOD
} FunctionType;

typedef struct Compiler {
    struct Compiler* enclosing;
    ObjFunction* function;
    FunctionType type;
    Local locals[UINT8_COUNT];
    int localCount;
    Upvalue upvalues[UINT8_COUNT];
    int scopeDepth;
} Compiler;

typedef struct ClassCompiler {
    bool hasSuperclass;
    struct ClassCompiler* enclosing;
} ClassCompiler;

extern Parser parser;
extern Compiler* current;
extern ClassCompiler* currentClass;

Chunk* currentChunk();
void errorAt(Token* token, const char* message);
void error(const char* message);
void errorAtCurrent(const char* message);
void advance();
void consume(TokenType type, const char* message);
bool check(TokenType type);
bool match(TokenType type);
void emitByte(uint8_t byte);
void emitBytes(uint8_t byte1, uint8_t byte2);
void emitLoop(int loopStart);
int emitJump(uint8_t instruction);
void emitReturn();
uint8_t makeConstant(Value value);
void emitConstant(Value value);
void patchJump(int offset);
void initCompiler(Compiler* compiler, FunctionType type);
ObjFunction* endCompiler();
void beginScope();
void endScope();
uint8_t identifierConstant(Token* name);
bool identifiersEqual(Token* a, Token* b);
int resolveLocal(Compiler* compiler, Token* name);
int resolveUpvalue(Compiler* compiler, Token* name);
int addUpvalue(Compiler* compiler, uint8_t index, bool isLocal);
void addLocal(Token name);
void declareVariable();
uint8_t parseVariable(const char* errorMessage);
void markInitialized();
void defineVariable(uint8_t global);
uint8_t argumentList();
Token syntheticToken(const char* text);
void namedVariable(Token name, bool canAssign);

void parsePrecedence(Precedence precedence);
ParseRule* getRule(TokenType type);
void expression();
void and_(bool canAssign);
void or_(bool canAssign);

void statement();
void declaration();
void block();

#endif