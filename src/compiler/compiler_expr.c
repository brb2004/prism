#include <stdlib.h>
#include <string.h>
#include "common.h"
#include "compiler_internal.h"
#include "chunk.h"

static void binary(bool canAssign) {
    UNUSED(canAssign);
    TokenType operatorType = parser.previous.type;
    ParseRule* rule = getRule(operatorType);
    parsePrecedence((Precedence)(rule->precedence + 1));
    switch (operatorType) {
        case TOKEN_BANG_EQUAL:    emitBytes(OP_EQUAL, OP_NOT); break;
        case TOKEN_EQUAL_EQUAL:   emitByte(OP_EQUAL);          break;
        case TOKEN_GREATER:       emitByte(OP_GREATER);        break;
        case TOKEN_GREATER_EQUAL: emitBytes(OP_LESS, OP_NOT);  break;
        case TOKEN_LESS:          emitByte(OP_LESS);           break;
        case TOKEN_LESS_EQUAL:    emitBytes(OP_GREATER, OP_NOT); break;
        case TOKEN_PLUS:          emitByte(OP_ADD);            break;
        case TOKEN_MINUS:         emitByte(OP_SUBTRACT);       break;
        case TOKEN_PERCENT:       emitByte(OP_MODULO);         break;
        case TOKEN_STAR:          emitByte(OP_MULTIPLY);       break;
        case TOKEN_SLASH:         emitByte(OP_DIVIDE);         break;
        default: return;
    }
}

static void callExpr(bool canAssign) {
    UNUSED(canAssign);
    uint8_t argCount = argumentList();
    emitBytes(OP_CALL, argCount);
}

static void dot(bool canAssign) {
    consume(TOKEN_IDENTIFIER, "Expect property name after '.'.");
    uint8_t name = identifierConstant(&parser.previous);
    if (canAssign && match(TOKEN_EQUAL)) {
        expression();
        emitBytes(OP_SET_PROPERTY, name);
    } else if (match(TOKEN_LEFT_PAREN)) {
        uint8_t argCount = argumentList();
        emitBytes(OP_INVOKE, name);
        emitByte(argCount);
    } else {
        emitBytes(OP_GET_PROPERTY, name);
    }
}

static void literal(bool canAssign) {
    UNUSED(canAssign);
    switch (parser.previous.type) {
        case TOKEN_FALSE: emitByte(OP_FALSE); break;
        case TOKEN_NIL:   emitByte(OP_NIL);   break;
        case TOKEN_TRUE:  emitByte(OP_TRUE);  break;
        default: return;
    }
}

static void grouping(bool canAssign) {
    UNUSED(canAssign);
    expression();
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after expression.");
}

static void number(bool canAssign) {
    UNUSED(canAssign);
    double value = strtod(parser.previous.start, NULL);
    emitConstant(NUMBER_VAL(value));
}

static void string(bool canAssign) {
    UNUSED(canAssign);
    emitConstant(OBJ_VAL(copyString(parser.previous.start + 1, parser.previous.length - 2)));
}

static void variable(bool canAssign) {
    namedVariable(parser.previous, canAssign);
}

static void super_(bool canAssign) {
    UNUSED(canAssign);
    if (currentClass == NULL) {
        error("Can't use 'super' outside of a class.");
    } else if (!currentClass->hasSuperclass) {
        error("Can't use 'super' in a class with no superclass.");
    }
    consume(TOKEN_DOT, "Expect '.' after 'super'.");
    consume(TOKEN_IDENTIFIER, "Expect superclass method name.");
    uint8_t name = identifierConstant(&parser.previous);
    namedVariable(syntheticToken("this"), false);
    if (match(TOKEN_LEFT_PAREN)) {
        uint8_t argCount = argumentList();
        namedVariable(syntheticToken("super"), false);
        emitBytes(OP_SUPER_INVOKE, name);
        emitByte(argCount);
    } else {
        namedVariable(syntheticToken("super"), false);
        emitBytes(OP_GET_SUPER, name);
    }
}

static void this_(bool canAssign) {
    UNUSED(canAssign);
    if (currentClass == NULL) { error("Can't use 'this' outside of a class."); return; }
    variable(false);
}

static void unary(bool canAssign) {
    UNUSED(canAssign);
    TokenType operatorType = parser.previous.type;
    parsePrecedence(PREC_UNARY);
    switch (operatorType) {
        case TOKEN_BANG:  emitByte(OP_NOT);    break;
        case TOKEN_MINUS: emitByte(OP_NEGATE); break;
        default: return;
    }
}

static void array(bool canAssign) {
    UNUSED(canAssign);
    int count = 0;
    if (!check(TOKEN_RIGHT_BRACKET)) {
        do { expression(); count++; } while (match(TOKEN_COMMA));
    }
    consume(TOKEN_RIGHT_BRACKET, "Expect ']' after array elements.");
    emitBytes(OP_BUILD_ARRAY, (uint8_t)count);
}

static void subscript(bool canAssign) {
    expression();
    consume(TOKEN_RIGHT_BRACKET, "Expect ']' after index.");
    if (canAssign && match(TOKEN_EQUAL)) {
        expression();
        emitByte(OP_SET_INDEX);
    } else {
        emitByte(OP_GET_INDEX);
    }
}

ParseRule rules[] = {
    [TOKEN_LEFT_PAREN]    = {grouping,  callExpr,  PREC_CALL},
    [TOKEN_RIGHT_PAREN]   = {NULL,      NULL,      PREC_NONE},
    [TOKEN_LEFT_BRACE]    = {NULL,      NULL,      PREC_NONE},
    [TOKEN_RIGHT_BRACE]   = {NULL,      NULL,      PREC_NONE},
    [TOKEN_COMMA]         = {NULL,      NULL,      PREC_NONE},
    [TOKEN_DOT]           = {NULL,      dot,       PREC_CALL},
    [TOKEN_MINUS]         = {unary,     binary,    PREC_TERM},
    [TOKEN_PLUS]          = {NULL,      binary,    PREC_TERM},
    [TOKEN_SEMICOLON]     = {NULL,      NULL,      PREC_NONE},
    [TOKEN_SLASH]         = {NULL,      binary,    PREC_FACTOR},
    [TOKEN_PERCENT]       = {NULL,      binary,    PREC_FACTOR},
    [TOKEN_STAR]          = {NULL,      binary,    PREC_FACTOR},
    [TOKEN_BANG]          = {unary,     NULL,      PREC_NONE},
    [TOKEN_BANG_EQUAL]    = {NULL,      binary,    PREC_EQUALITY},
    [TOKEN_EQUAL_EQUAL]   = {NULL,      binary,    PREC_EQUALITY},
    [TOKEN_GREATER]       = {NULL,      binary,    PREC_COMPARISON},
    [TOKEN_GREATER_EQUAL] = {NULL,      binary,    PREC_COMPARISON},
    [TOKEN_LESS]          = {NULL,      binary,    PREC_COMPARISON},
    [TOKEN_LESS_EQUAL]    = {NULL,      binary,    PREC_COMPARISON},
    [TOKEN_IDENTIFIER]    = {variable,  NULL,      PREC_NONE},
    [TOKEN_STRING]        = {string,    NULL,      PREC_NONE},
    [TOKEN_NUMBER]        = {number,    NULL,      PREC_NONE},
    [TOKEN_AND]           = {NULL,      and_,      PREC_AND},
    [TOKEN_CLASS]         = {NULL,      NULL,      PREC_NONE},
    [TOKEN_ELSE]          = {NULL,      NULL,      PREC_NONE},
    [TOKEN_FALSE]         = {literal,   NULL,      PREC_NONE},
    [TOKEN_FOR]           = {NULL,      NULL,      PREC_NONE},
    [TOKEN_FUN]           = {NULL,      NULL,      PREC_NONE},
    [TOKEN_IF]            = {NULL,      NULL,      PREC_NONE},
    [TOKEN_NIL]           = {literal,   NULL,      PREC_NONE},
    [TOKEN_OR]            = {NULL,      or_,       PREC_OR},
    [TOKEN_PRINT]         = {NULL,      NULL,      PREC_NONE},
    [TOKEN_RETURN]        = {NULL,      NULL,      PREC_NONE},
    [TOKEN_SUPER]         = {super_,    NULL,      PREC_NONE},
    [TOKEN_THIS]          = {this_,     NULL,      PREC_NONE},
    [TOKEN_TRUE]          = {literal,   NULL,      PREC_NONE},
    [TOKEN_VAR]           = {NULL,      NULL,      PREC_NONE},
    [TOKEN_LEFT_BRACKET]  = {array,     subscript, PREC_CALL},
    [TOKEN_RIGHT_BRACKET] = {NULL,      NULL,      PREC_NONE},
    [TOKEN_WHILE]         = {NULL,      NULL,      PREC_NONE},
    [TOKEN_ERROR]         = {NULL,      NULL,      PREC_NONE},
    [TOKEN_EOF]           = {NULL,      NULL,      PREC_NONE},
};

void parsePrecedence(Precedence precedence) {
    advance();
    ParseFn prefixRule = getRule(parser.previous.type)->prefix;
    if (prefixRule == NULL) { error("Expect expression."); return; }
    bool canAssign = precedence <= PREC_ASSIGNMENT;
    prefixRule(canAssign);
    while (precedence <= getRule(parser.current.type)->precedence) {
        advance();
        ParseFn infixRule = getRule(parser.previous.type)->infix;
        infixRule(canAssign);
    }
    if (canAssign && match(TOKEN_EQUAL)) error("Invalid assignment target.");
}

ParseRule* getRule(TokenType type) {
    return &rules[type];
}

void expression() {
    parsePrecedence(PREC_ASSIGNMENT);
}

void and_(bool canAssign) {
    UNUSED(canAssign);
    int endJump = emitJump(OP_JUMP_IF_FALSE);
    emitByte(OP_POP);
    parsePrecedence(PREC_AND);
    patchJump(endJump);
}

void or_(bool canAssign) {
    UNUSED(canAssign);
    int elseJump = emitJump(OP_JUMP_IF_FALSE);
    int endJump  = emitJump(OP_JUMP);
    patchJump(elseJump);
    emitByte(OP_POP);
    parsePrecedence(PREC_OR);
    patchJump(endJump);
}