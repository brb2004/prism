#include <stdio.h>
#include <string.h>
#include "common.h"
#include "compiler.h"
#include "compiler_internal.h"
#include "scanner.h"
#include "memory.h"


static void varDeclaration();
static void expressionStatement();
static void printStatement() {
    expression();
    consume(TOKEN_SEMICOLON, "Expect ';' after value.");
    emitByte(OP_PRINT);
}

static void returnStatement() {
    if (current->type == TYPE_SCRIPT) error("Can't return from top-level code.");
    if (match(TOKEN_SEMICOLON)) {
        emitReturn();
    } else {
        expression();
        consume(TOKEN_SEMICOLON, "Expect ';' after return value.");
        emitByte(OP_RETURN);
    }
}

static void ifStatement() {
    consume(TOKEN_LEFT_PAREN, "Expect '(' after 'if'.");
    expression();
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after condition.");
    int thenJump = emitJump(OP_JUMP_IF_FALSE);
    emitByte(OP_POP);
    statement();
    int elseJump = emitJump(OP_JUMP);
    patchJump(thenJump);
    emitByte(OP_POP);
    if (match(TOKEN_ELSE)) statement();
    patchJump(elseJump);
}

static void whileStatement() {
    int loopStart = currentChunk()->count;
    consume(TOKEN_LEFT_PAREN, "Expect '(' after 'while'.");
    expression();
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after condition.");
    int exitJump = emitJump(OP_JUMP_IF_FALSE);
    emitByte(OP_POP);
    statement();
    emitLoop(loopStart);
    patchJump(exitJump);
    emitByte(OP_POP);
}

static void forStatement() {
    beginScope();
    consume(TOKEN_LEFT_PAREN, "Expect '(' after 'for'.");
    if (match(TOKEN_SEMICOLON)) {
        // no initializer
    } else if (match(TOKEN_VAR)) {
        varDeclaration();
    } else {
        expressionStatement();
    }
    int loopStart = currentChunk()->count;
    int exitJump = -1;
    if (!match(TOKEN_SEMICOLON)) {
        expression();
        consume(TOKEN_SEMICOLON, "Expect ';' after loop condition.");
        exitJump = emitJump(OP_JUMP_IF_FALSE);
        emitByte(OP_POP);
    }
    if (!match(TOKEN_RIGHT_PAREN)) {
        int bodyJump       = emitJump(OP_JUMP);
        int incrementStart = currentChunk()->count;
        expression();
        emitByte(OP_POP);
        consume(TOKEN_RIGHT_PAREN, "Expect ')' after for clauses.");
        emitLoop(loopStart);
        loopStart = incrementStart;
        patchJump(bodyJump);
    }
    statement();
    emitLoop(loopStart);
    if (exitJump != -1) {
        patchJump(exitJump);
        emitByte(OP_POP);
    }
    endScope();
}

static void expressionStatement() {
    expression();
    consume(TOKEN_SEMICOLON, "Expect ';' after expression.");
    emitByte(OP_POP);
}

static void importStatement() {
    consume(TOKEN_STRING, "Expect filename after 'import'.");
    uint8_t pathConstant = makeConstant(OBJ_VAL(copyString(
        parser.previous.start + 1, parser.previous.length - 2)));
    emitBytes(OP_IMPORT, pathConstant);
    emitByte(OP_POP);
    consume(TOKEN_SEMICOLON, "Expect ';' after import.");
}

void block() {
    while (!check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF)) {
        declaration();
    }
    consume(TOKEN_RIGHT_BRACE, "Expect '}' after block.");
}

static void function(FunctionType type) {
    Compiler compiler;
    initCompiler(&compiler, type);
    beginScope();
    consume(TOKEN_LEFT_PAREN, "Expect '(' after function name.");
    if (!check(TOKEN_RIGHT_PAREN)) {
        do {
            current->function->arity++;
            if (current->function->arity > 255) errorAtCurrent("Can't have more than 255 parameters.");
            uint8_t constant = parseVariable("Expect parameter name.");
            defineVariable(constant);
        } while (match(TOKEN_COMMA));
    }
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after parameters.");
    consume(TOKEN_LEFT_BRACE,  "Expect '{' before function body.");
    block();
    ObjFunction* func = endCompiler();
    emitBytes(OP_CLOSURE, makeConstant(OBJ_VAL(func)));
    for (int i = 0; i < func->upvalueCount; i++) {
        emitByte(compiler.upvalues[i].isLocal ? 1 : 0);
        emitByte(compiler.upvalues[i].index);
    }
}

static void method() {
    consume(TOKEN_IDENTIFIER, "Expect method name.");
    uint8_t constant = identifierConstant(&parser.previous);
    FunctionType type = TYPE_METHOD;
    if (parser.previous.length == 4 && memcmp(parser.previous.start, "init", 4) == 0) {
        type = TYPE_INITIALIZER;
    }
    function(type);
    emitBytes(OP_METHOD, constant);
}

static void classDeclaration() {
    consume(TOKEN_IDENTIFIER, "Expect class name.");
    Token className    = parser.previous;
    uint8_t nameConstant = identifierConstant(&parser.previous);
    declareVariable();
    emitBytes(OP_CLASS, nameConstant);
    defineVariable(nameConstant);
    ClassCompiler classCompiler;
    classCompiler.hasSuperclass = false;
    classCompiler.enclosing     = currentClass;
    currentClass = &classCompiler;
    if (match(TOKEN_LESS)) {
        consume(TOKEN_IDENTIFIER, "Expect superclass name.");
        namedVariable(parser.previous, false);
        if (identifiersEqual(&className, &parser.previous)) error("A class can't inherit from itself.");
        beginScope();
        addLocal(syntheticToken("super"));
        defineVariable(0);
        namedVariable(className, false);
        emitByte(OP_INHERIT);
        classCompiler.hasSuperclass = true;
    }
    namedVariable(className, false);
    consume(TOKEN_LEFT_BRACE, "Expect '{' before class body.");
    while (!check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF)) { method(); }
    consume(TOKEN_RIGHT_BRACE, "Expect '}' after class body.");
    emitByte(OP_POP);
    if (classCompiler.hasSuperclass) endScope();
    currentClass = currentClass->enclosing;
}

static void funDeclaration() {
    uint8_t global = parseVariable("Expect function name.");
    markInitialized();
    function(TYPE_FUNCTION);
    defineVariable(global);
}

static void varDeclaration() {
    uint8_t global = parseVariable("Expect variable name.");
    if (match(TOKEN_EQUAL)) { expression(); } else { emitByte(OP_NIL); }
    consume(TOKEN_SEMICOLON, "Expect ';' after variable declaration.");
    defineVariable(global);
}

static void synchronize() {
    parser.panicMode = false;
    while (parser.current.type != TOKEN_EOF) {
        if (parser.previous.type == TOKEN_SEMICOLON) return;
        switch (parser.current.type) {
            case TOKEN_CLASS: case TOKEN_FUN: case TOKEN_VAR:
            case TOKEN_FOR:   case TOKEN_IF:  case TOKEN_WHILE:
            case TOKEN_PRINT: case TOKEN_RETURN: return;
            default: ;
        }
        advance();
    }
}

void statement() {
    if      (match(TOKEN_IMPORT)) importStatement();
    else if (match(TOKEN_PRINT))  printStatement();
    else if (match(TOKEN_FOR))    forStatement();
    else if (match(TOKEN_IF))     ifStatement();
    else if (match(TOKEN_RETURN)) returnStatement();
    else if (match(TOKEN_WHILE))  whileStatement();
    else if (match(TOKEN_LEFT_BRACE)) { beginScope(); block(); endScope(); }
    else expressionStatement();
}

void declaration() {
    if      (match(TOKEN_CLASS)) classDeclaration();
    else if (match(TOKEN_FUN))   funDeclaration();
    else if (match(TOKEN_VAR))   varDeclaration();
    else statement();
    if (parser.panicMode) synchronize();
}

ObjFunction* compile(const char* source) {
    initScanner(source);
    Compiler compiler;
    initCompiler(&compiler, TYPE_SCRIPT);
    parser.hadError  = false;
    parser.panicMode = false;
    advance();
    while (!match(TOKEN_EOF)) { declaration(); }
    ObjFunction* function = endCompiler();
    return parser.hadError ? NULL : function;
}