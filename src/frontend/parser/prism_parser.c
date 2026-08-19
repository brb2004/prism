#define C_STD_INCLUDE_ALL
#include "util/c_std.h"
#include "util/throw.h"

#include "ast/ast.h"
#include "ast/front_ast.h"
#include "ast/front_symt.h"

#include "prism_lexer.h"
#include "prism_parser.h"

typedef struct {
    Token tok;
    size_t info_at;   
} PToken;

typedef struct {
    PToken current;
    PToken previous;
    bool hadError;
    bool panicMode;
    const char* filename;
    ErrorsContext* errors;
    IdentifierContext* identifiers;
} PrismParser;

static PrismParser P;

static void errorAt(const PToken* ptok, const char* message) {
    if (P.panicMode) return;
    P.panicMode = true;
    P.hadError = true;

    fprintf(stderr, "%s:%d:%d:\n", P.filename, ptok->tok.line, ptok->tok.col);
    if (ptok->tok.type == TOKEN_EOF) {
        fprintf(stderr, "error: %s (at end of file)\n", message);
    }
    else if (ptok->tok.type == TOKEN_ERROR) {
        fprintf(stderr, "error: %.*s\n", ptok->tok.length, ptok->tok.start);
    }
    else {
        fprintf(stderr, "error: %s, found '%.*s'\n", message, ptok->tok.length, ptok->tok.start);
    }
}

static void error(const char* message) { errorAt(&P.previous, message); }
static void errorAtCurrent(const char* message) { errorAt(&P.current, message); }

static size_t pushTokenInfo(const Token* tok) {
    TokenInfo info;
    info.tok_pos = tok->col - 1;
    info.tok_len = tok->length;
    info.total_linenum = (size_t)tok->line;
    vec_push_back(P.errors->token_infos, info);
    return vec_size(P.errors->token_infos) - 1;
}

static void advance(void) {
    P.previous = P.current;
    for (;;) {
        P.current.tok = scanToken();
        P.current.info_at = pushTokenInfo(&P.current.tok);
        if (P.current.tok.type != TOKEN_ERROR) break;
        errorAtCurrent(NULL);
    }
}

static bool check(TokenType type) { return P.current.tok.type == type; }

static bool match(TokenType type) {
    if (!check(type)) return false;
    advance();
    return true;
}

static bool consume(TokenType type, const char* message) {
    if (P.current.tok.type == type) {
        advance();
        return true;
    }
    errorAtCurrent(message);
    return false;
}

static TIdentifier internPrevious(void) {
    string_t name = sdsnewlen(P.previous.tok.start, (size_t)P.previous.tok.length);
    return make_string_identifier(P.identifiers, &name);
}

static shared_ptr_t(Type) parseType(void);
static bool parseIntLiteral(const Token* tok, TULong* out);

static shared_ptr_t(Type) primitiveType(const Token* tok) {
    struct {
        const char* text;
        int len;
    } t = {tok->start, tok->length};

#define PRIM(S, MAKE)                                            \
    if (t.len == (int)sizeof(S) - 1 && memcmp(t.text, S, t.len) == 0) { \
        return MAKE();                                           \
    }

    PRIM("void", make_Void)
    PRIM("i8", make_SChar)
    PRIM("u8", make_UChar)
    PRIM("char", make_Char)
    PRIM("i16", make_Short)
    PRIM("u16", make_UShort)
    PRIM("i32", make_Int)
    PRIM("u32", make_UInt)
    PRIM("i64", make_Long)
    PRIM("u64", make_ULong)
    // No bool in the AST; i32 semantics match C's truthiness rules.
    PRIM("bool", make_Int)
#undef PRIM

    // i16 / u16 have no AST node. wheelcc has Char, Int, Long and their
    // unsigned forms - there is no Short. Fail loudly rather than silently
    // widening, because a 16-bit MMIO write is not the same as a 32-bit one.
    if ((t.len == 3 && memcmp(t.text, "i16", 3) == 0) || (t.len == 3 && memcmp(t.text, "u16", 3) == 0)) {
        error("16-bit types need AST_Short_t added to the AST first");
        return sptr_new();
    }

    return sptr_new();   // not a primitive: caller treats it as a struct tag
}

static shared_ptr_t(Type) parseType(void) {
    if (match(TOKEN_STAR)) {
        shared_ptr_t(Type) ref = parseType();
        if (!ref) return sptr_new();
        return make_Pointer(&ref);
    }

    if (match(TOKEN_LEFT_BRACKET)) {
        if (!consume(TOKEN_NUMBER, "expect array length")) return sptr_new();
        TULong size = 0;
        if (!parseIntLiteral(&P.previous.tok, &size)) return sptr_new();
        if (!consume(TOKEN_RIGHT_BRACKET, "expect ']' after array length")) return sptr_new();
        shared_ptr_t(Type) elem = parseType();
        if (!elem) return sptr_new();
        return make_Array((TLong)size, &elem);
    }

    if (match(TOKEN_STRUCT) || match(TOKEN_UNION)) {
        bool is_union = P.previous.tok.type == TOKEN_UNION;
        if (!consume(TOKEN_IDENTIFIER, "expect struct or union tag")) return sptr_new();
        return make_Structure(internPrevious(), is_union);
    }

    if (!consume(TOKEN_IDENTIFIER, "expect type name")) return sptr_new();

    shared_ptr_t(Type) prim = primitiveType(&P.previous.tok);
    if (prim) return prim;
    if (P.hadError) return sptr_new();

    // A bare identifier that is not a primitive is a struct tag.
    return make_Structure(internPrevious(), false);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Literals

static bool parseIntLiteral(const Token* tok, TULong* out) {
    const char* s = tok->start;
    int n = tok->length;
    TULong value = 0;

    // Strip the u/U l/L suffix the lexer attached.
    while (n > 0 && (s[n - 1] == 'u' || s[n - 1] == 'U' || s[n - 1] == 'l' || s[n - 1] == 'L')) {
        n--;
    }

    if (n > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        for (int i = 2; i < n; i++) {
            char c = s[i];
            int d = (c >= '0' && c <= '9') ? c - '0' : (c >= 'a' && c <= 'f') ? c - 'a' + 10 : c - 'A' + 10;
            value = value * 16u + (TULong)d;
        }
    }
    else if (n > 2 && s[0] == '0' && (s[1] == 'b' || s[1] == 'B')) {
        for (int i = 2; i < n; i++) {
            value = value * 2u + (TULong)(s[i] - '0');
        }
    }
    else {
        for (int i = 0; i < n; i++) {
            value = value * 10u + (TULong)(s[i] - '0');
        }
    }

    *out = value;
    return true;
}

// Picks between CConstInt / CConstUInt / CConstLong / CConstULong the way C
// does: the suffix constrains signedness and minimum width, the magnitude
// promotes from there.
static shared_ptr_t(CConst) numberConst(const Token* tok) {
    TULong value = 0;
    if (!parseIntLiteral(tok, &value)) return sptr_new();

    bool isU = false, isL = false;
    for (int i = 0; i < tok->length; i++) {
        char c = tok->start[i];
        if (c == 'u' || c == 'U') isU = true;
        if (c == 'l' || c == 'L') isL = true;
    }
    // 0x... digits include letters; only trailing suffix chars count.
    isU = isL = false;
    for (int i = tok->length; i-- > 0;) {
        char c = tok->start[i];
        if (c == 'u' || c == 'U') { isU = true; continue; }
        if (c == 'l' || c == 'L') { isL = true; continue; }
        break;
    }

    if (isU && isL) return make_CConstULong(value);
    if (isU) return value <= 4294967295ull ? make_CConstUInt((TUInt)value) : make_CConstULong(value);
    if (isL) return make_CConstLong((TLong)value);
    if (value <= 2147483647ull) return make_CConstInt((TInt)value);
    if (value <= 9223372036854775807ull) return make_CConstLong((TLong)value);
    return make_CConstULong(value);
}

// Decodes one escape sequence or plain character out of a 'x' literal.
static TChar decodeCharLiteral(const Token* tok) {
    const char* s = tok->start + 1;   // skip opening quote
    if (s[0] != '\\') return (TChar)s[0];
    switch (s[1]) {
        case '\'': return 39;
        case '"':  return 34;
        case '?':  return 63;
        case '\\': return 92;
        case 'a':  return 7;
        case 'b':  return 8;
        case 'f':  return 12;
        case 'n':  return 10;
        case 'r':  return 13;
        case 't':  return 9;
        case 'v':  return 11;
        case '0':  return 0;
        default:   return (TChar)s[1];
    }
}

// Decodes a "..." literal into the byte vector CStringLiteral wants.
static shared_ptr_t(CStringLiteral) decodeStringLiteral(const Token* tok) {
    vector_t(TChar) value = vec_new();
    for (int i = 1; i < tok->length - 1; i++) {
        char c = tok->start[i];
        if (c != '\\') {
            vec_push_back(value, (TChar)c);
            continue;
        }
        i++;
        if (i >= tok->length - 1) break;
        switch (tok->start[i]) {
            case '\'': vec_push_back(value, 39); break;
            case '"':  vec_push_back(value, 34); break;
            case '?':  vec_push_back(value, 63); break;
            case '\\': vec_push_back(value, 92); break;
            case 'a':  vec_push_back(value, 7);  break;
            case 'b':  vec_push_back(value, 8);  break;
            case 'f':  vec_push_back(value, 12); break;
            case 'n':  vec_push_back(value, 10); break;
            case 'r':  vec_push_back(value, 13); break;
            case 't':  vec_push_back(value, 9);  break;
            case 'v':  vec_push_back(value, 11); break;
            case '0':  vec_push_back(value, 0);  break;
            default:   vec_push_back(value, (TChar)tok->start[i]); break;
        }
    }
    return make_CStringLiteral(&value);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Expressions - Pratt parser

typedef enum {
    PREC_NONE,
    PREC_ASSIGNMENT,   // =  +=  <<=  ...
    PREC_CONDITIONAL,  // ?:
    PREC_OR,           // or  ||
    PREC_AND,          // and &&
    PREC_BITOR,        // |
    PREC_BITXOR,       // ^
    PREC_BITAND,       // &
    PREC_EQUALITY,     // ==  !=
    PREC_COMPARISON,   // <  >  <=  >=
    PREC_SHIFT,        // <<  >>
    PREC_TERM,         // +  -
    PREC_FACTOR,       // *  /  %
    PREC_CAST,         // as
    PREC_UNARY,        // !  -  ~  &  *  ++  --  sizeof
    PREC_CALL,         // .  ->  []  ()  ++  --
    PREC_PRIMARY
} Precedence;

typedef unique_ptr_t(CExp) (*PrefixFn)(void);
typedef unique_ptr_t(CExp) (*InfixFn)(unique_ptr_t(CExp) left);

typedef struct {
    PrefixFn prefix;
    InfixFn infix;
    Precedence precedence;
} ParseRule;

static const ParseRule* getRule(TokenType type);
static unique_ptr_t(CExp) parsePrecedence(Precedence precedence);
static unique_ptr_t(CExp) expression(void);
static unique_ptr_t(CExp) numberExp(void) {
    shared_ptr_t(CConst) constant = numberConst(&P.previous.tok);
    if (!constant) return uptr_new();
    return make_CConstant(&constant, P.previous.info_at);
}

static unique_ptr_t(CExp) charExp(void) {
    shared_ptr_t(CConst) constant = make_CConstChar(decodeCharLiteral(&P.previous.tok));
    return make_CConstant(&constant, P.previous.info_at);
}

static unique_ptr_t(CExp) stringExp(void) {
    shared_ptr_t(CStringLiteral) literal = decodeStringLiteral(&P.previous.tok);
    return make_CString(&literal, P.previous.info_at);
}

// nil / true / false have no AST node. C has no bool or null literal either -
// they are integer constants, and semantic analysis converts 0 to a null
// pointer where the context calls for one.
static unique_ptr_t(CExp) literalExp(void) {
    TInt value = 0;
    if (P.previous.tok.type == TOKEN_TRUE) value = 1;
    shared_ptr_t(CConst) constant = make_CConstInt(value);
    return make_CConstant(&constant, P.previous.info_at);
}

static unique_ptr_t(CExp) variableExp(void) {
    size_t info_at = P.previous.info_at;
    TIdentifier name = internPrevious();

    // A call is the only place wheelcc's AST names a callee directly:
    // CFunctionCall takes a TIdentifier, not an arbitrary expression. So the
    // call form is recognised here rather than as an infix rule.
    if (check(TOKEN_LEFT_PAREN)) {
        advance();
        vector_t(unique_ptr_t(CExp)) args = vec_new();
        if (!check(TOKEN_RIGHT_PAREN)) {
            do {
                unique_ptr_t(CExp) arg = expression();
                if (!arg) return uptr_new();
                vec_move_back(args, arg);
            }
            while (match(TOKEN_COMMA));
        }
        if (!consume(TOKEN_RIGHT_PAREN, "expect ')' after arguments")) return uptr_new();
        return make_CFunctionCall(name, &args, info_at);
    }

    return make_CVar(name, info_at);
}

static unique_ptr_t(CExp) groupingExp(void) {
    unique_ptr_t(CExp) inner = expression();
    if (!inner) return uptr_new();
    if (!consume(TOKEN_RIGHT_PAREN, "expect ')' after expression")) return uptr_new();
    return inner;
}

static unique_ptr_t(CExp) unaryExp(void) {
    TokenType op = P.previous.tok.type;
    size_t info_at = P.previous.info_at;

    // sizeof(type) and sizeof expr are different nodes.
    if (op == TOKEN_SIZEOF) {
        if (check(TOKEN_LEFT_PAREN)) {
            // Look past '(' - if a type follows, this is CSizeOfT.
            advance();
            if (check(TOKEN_STAR) || check(TOKEN_LEFT_BRACKET) || check(TOKEN_STRUCT) || check(TOKEN_UNION)
                || check(TOKEN_IDENTIFIER)) {
                shared_ptr_t(Type) target = parseType();
                if (!target) return uptr_new();
                if (!consume(TOKEN_RIGHT_PAREN, "expect ')' after sizeof type")) return uptr_new();
                return make_CSizeOfT(&target, info_at);
            }
            unique_ptr_t(CExp) inner = expression();
            if (!inner) return uptr_new();
            if (!consume(TOKEN_RIGHT_PAREN, "expect ')' after expression")) return uptr_new();
            return make_CSizeOf(&inner, info_at);
        }
        unique_ptr_t(CExp) operand = parsePrecedence(PREC_UNARY);
        if (!operand) return uptr_new();
        return make_CSizeOf(&operand, info_at);
    }

    unique_ptr_t(CExp) operand = parsePrecedence(PREC_UNARY);
    if (!operand) return uptr_new();

    switch (op) {
        case TOKEN_MINUS: {
            CUnaryOp unop = init_CNegate();
            return make_CUnary(&unop, &operand, info_at);
        }
        case TOKEN_BANG: {
            CUnaryOp unop = init_CNot();
            return make_CUnary(&unop, &operand, info_at);
        }
        case TOKEN_TILDE: {
            CUnaryOp unop = init_CComplement();
            return make_CUnary(&unop, &operand, info_at);
        }
        case TOKEN_AMP:
            return make_CAddrOf(&operand, info_at);
        case TOKEN_STAR:
            return make_CDereference(&operand, info_at);
        case TOKEN_PLUS_PLUS:
        case TOKEN_MINUS_MINUS: {
            // ++a  ->  Assignment(CPrefix, NULL, CBinary(CAdd, a, 1))
            // The left operand is deliberately null: CExp is a unique_ptr, so
            // the target cannot be duplicated. Semantic analysis recovers it
            // from inside the binary node. Same shape wheelcc's parser uses.
            CUnaryOp unop = init_CPrefix();
            CBinaryOp binop = op == TOKEN_PLUS_PLUS ? init_CAdd() : init_CSubtract();
            shared_ptr_t(CConst) one_c = make_CConstInt(1);
            unique_ptr_t(CExp) one = make_CConstant(&one_c, info_at);
            unique_ptr_t(CExp) exp_null = uptr_new();
            unique_ptr_t(CExp) sum = make_CBinary(&binop, &operand, &one, info_at);
            return make_CAssignment(&unop, &exp_null, &sum, info_at);
        }
        default:
            return operand;
    }
}

static unique_ptr_t(CExp) binaryExp(unique_ptr_t(CExp) left) {
    TokenType op = P.previous.tok.type;
    size_t info_at = P.previous.info_at;

    unique_ptr_t(CExp) right = parsePrecedence((Precedence)(getRule(op)->precedence + 1));
    if (!right) return uptr_new();

    CBinaryOp binop = init_CBinaryOp();
    switch (op) {
        case TOKEN_PLUS:          binop = init_CAdd(); break;
        case TOKEN_MINUS:         binop = init_CSubtract(); break;
        case TOKEN_STAR:          binop = init_CMultiply(); break;
        case TOKEN_SLASH:         binop = init_CDivide(); break;
        case TOKEN_PERCENT:       binop = init_CRemainder(); break;
        case TOKEN_AMP:           binop = init_CBitAnd(); break;
        case TOKEN_PIPE:          binop = init_CBitOr(); break;
        case TOKEN_CARET:         binop = init_CBitXor(); break;
        case TOKEN_SHIFT_LEFT:    binop = init_CBitShiftLeft(); break;
        case TOKEN_SHIFT_RIGHT:   binop = init_CBitShiftRight(); break;
        case TOKEN_EQUAL_EQUAL:   binop = init_CEqual(); break;
        case TOKEN_BANG_EQUAL:    binop = init_CNotEqual(); break;
        case TOKEN_LESS:          binop = init_CLessThan(); break;
        case TOKEN_LESS_EQUAL:    binop = init_CLessOrEqual(); break;
        case TOKEN_GREATER:       binop = init_CGreaterThan(); break;
        case TOKEN_GREATER_EQUAL: binop = init_CGreaterOrEqual(); break;
        case TOKEN_AND:
        case TOKEN_AMP_AMP:       binop = init_CAnd(); break;
        case TOKEN_OR:
        case TOKEN_PIPE_PIPE:     binop = init_COr(); break;
        default:                  return left;
    }

    // No jump patching. CAnd / COr are plain nodes; short-circuiting is the
    // TAC pass's job, not the parser's.
    return make_CBinary(&binop, &left, &right, info_at);
}

static unique_ptr_t(CExp) subscriptExp(unique_ptr_t(CExp) left) {
    size_t info_at = P.previous.info_at;
    unique_ptr_t(CExp) index = expression();
    if (!index) return uptr_new();
    if (!consume(TOKEN_RIGHT_BRACKET, "expect ']' after subscript")) return uptr_new();
    return make_CSubscript(&left, &index, info_at);
}

static unique_ptr_t(CExp) memberExp(unique_ptr_t(CExp) left) {
    bool isArrow = P.previous.tok.type == TOKEN_ARROW;
    size_t info_at = P.previous.info_at;
    if (!consume(TOKEN_IDENTIFIER, "expect member name")) return uptr_new();
    TIdentifier member = internPrevious();
    return isArrow ? make_CArrow(member, &left, info_at) : make_CDot(member, &left, info_at);
}

// a++  ->  Assignment(CPostfix, a, a + 1)
static unique_ptr_t(CExp) postfixExp(unique_ptr_t(CExp) left) {
    size_t info_at = P.previous.info_at;
    CUnaryOp unop = init_CPostfix();
    CBinaryOp binop = P.previous.tok.type == TOKEN_PLUS_PLUS ? init_CAdd() : init_CSubtract();
    shared_ptr_t(CConst) one_c = make_CConstInt(1);
    unique_ptr_t(CExp) one = make_CConstant(&one_c, info_at);
    unique_ptr_t(CExp) exp_null = uptr_new();
    unique_ptr_t(CExp) sum = make_CBinary(&binop, &left, &one, info_at);
    return make_CAssignment(&unop, &exp_null, &sum, info_at);
}

// `expr as *u8`. A postfix operator rather than C's (T)e, which is ambiguous
// with grouping once type names are ordinary identifiers.
static unique_ptr_t(CExp) castExp(unique_ptr_t(CExp) left) {
    size_t info_at = P.previous.info_at;
    shared_ptr_t(Type) target = parseType();
    if (!target) return uptr_new();
    return make_CCast(&left, &target, info_at);
}

static const ParseRule rules[] = {
    [TOKEN_LEFT_PAREN]         = {groupingExp,   NULL,         PREC_NONE},
    [TOKEN_RIGHT_PAREN]        = {NULL,          NULL,         PREC_NONE},
    [TOKEN_LEFT_BRACE]         = {NULL,          NULL,         PREC_NONE},
    [TOKEN_RIGHT_BRACE]        = {NULL,          NULL,         PREC_NONE},
    [TOKEN_LEFT_BRACKET]       = {NULL,          subscriptExp, PREC_CALL},
    [TOKEN_RIGHT_BRACKET]      = {NULL,          NULL,         PREC_NONE},
    [TOKEN_COMMA]              = {NULL,          NULL,         PREC_NONE},
    [TOKEN_DOT]                = {NULL,          memberExp,    PREC_CALL},
    [TOKEN_ARROW]              = {NULL,          memberExp,    PREC_CALL},
    [TOKEN_MINUS]              = {unaryExp,      binaryExp,    PREC_TERM},
    [TOKEN_PLUS]               = {NULL,          binaryExp,    PREC_TERM},
    [TOKEN_SEMICOLON]          = {NULL,          NULL,         PREC_NONE},
    [TOKEN_SLASH]              = {NULL,          binaryExp,    PREC_FACTOR},
    [TOKEN_STAR]               = {unaryExp,      binaryExp,    PREC_FACTOR},
    [TOKEN_PERCENT]            = {NULL,          binaryExp,    PREC_FACTOR},
    [TOKEN_AMP]                = {unaryExp,      binaryExp,    PREC_BITAND},
    [TOKEN_PIPE]               = {NULL,          binaryExp,    PREC_BITOR},
    [TOKEN_CARET]              = {NULL,          binaryExp,    PREC_BITXOR},
    [TOKEN_TILDE]              = {unaryExp,      NULL,         PREC_NONE},
    [TOKEN_SHIFT_LEFT]         = {NULL,          binaryExp,    PREC_SHIFT},
    [TOKEN_SHIFT_RIGHT]        = {NULL,          binaryExp,    PREC_SHIFT},
    [TOKEN_COLON]              = {NULL,          NULL,         PREC_NONE},
    [TOKEN_QUESTION]           = {NULL,          NULL,         PREC_CONDITIONAL},
    [TOKEN_PLUS_PLUS]          = {unaryExp,      postfixExp,   PREC_CALL},
    [TOKEN_MINUS_MINUS]        = {unaryExp,      postfixExp,   PREC_CALL},
    [TOKEN_AMP_AMP]            = {NULL,          binaryExp,    PREC_AND},
    [TOKEN_PIPE_PIPE]          = {NULL,          binaryExp,    PREC_OR},
    [TOKEN_PLUS_EQUAL]         = {NULL,          NULL,         PREC_NONE},
    [TOKEN_MINUS_EQUAL]        = {NULL,          NULL,         PREC_NONE},
    [TOKEN_STAR_EQUAL]         = {NULL,          NULL,         PREC_NONE},
    [TOKEN_SLASH_EQUAL]        = {NULL,          NULL,         PREC_NONE},
    [TOKEN_PERCENT_EQUAL]      = {NULL,          NULL,         PREC_NONE},
    [TOKEN_AMP_EQUAL]          = {NULL,          NULL,         PREC_NONE},
    [TOKEN_PIPE_EQUAL]         = {NULL,          NULL,         PREC_NONE},
    [TOKEN_CARET_EQUAL]        = {NULL,          NULL,         PREC_NONE},
    [TOKEN_SHIFT_LEFT_EQUAL]   = {NULL,          NULL,         PREC_NONE},
    [TOKEN_SHIFT_RIGHT_EQUAL]  = {NULL,          NULL,         PREC_NONE},
    [TOKEN_BANG]               = {unaryExp,      NULL,         PREC_NONE},
    [TOKEN_BANG_EQUAL]         = {NULL,          binaryExp,    PREC_EQUALITY},
    [TOKEN_EQUAL]              = {NULL,          NULL,         PREC_NONE},
    [TOKEN_EQUAL_EQUAL]        = {NULL,          binaryExp,    PREC_EQUALITY},
    [TOKEN_GREATER]            = {NULL,          binaryExp,    PREC_COMPARISON},
    [TOKEN_GREATER_EQUAL]      = {NULL,          binaryExp,    PREC_COMPARISON},
    [TOKEN_LESS]               = {NULL,          binaryExp,    PREC_COMPARISON},
    [TOKEN_LESS_EQUAL]         = {NULL,          binaryExp,    PREC_COMPARISON},
    [TOKEN_IDENTIFIER]         = {variableExp,   NULL,         PREC_NONE},
    [TOKEN_STRING]             = {stringExp,     NULL,         PREC_NONE},
    [TOKEN_NUMBER]             = {numberExp,     NULL,         PREC_NONE},
    [TOKEN_CHAR]               = {charExp,       NULL,         PREC_NONE},
    [TOKEN_AND]                = {NULL,          binaryExp,    PREC_AND},
    [TOKEN_OR]                 = {NULL,          binaryExp,    PREC_OR},
    [TOKEN_PACKED]             = {NULL,          NULL,         PREC_NONE},
    [TOKEN_ELSE]               = {NULL,          NULL,         PREC_NONE},
    [TOKEN_FALSE]              = {literalExp,    NULL,         PREC_NONE},
    [TOKEN_TRUE]               = {literalExp,    NULL,         PREC_NONE},
    [TOKEN_NIL]                = {literalExp,    NULL,         PREC_NONE},
    [TOKEN_FOR]                = {NULL,          NULL,         PREC_NONE},
    [TOKEN_FUN]                = {NULL,          NULL,         PREC_NONE},
    [TOKEN_IF]                 = {NULL,          NULL,         PREC_NONE},
    [TOKEN_RETURN]             = {NULL,          NULL,         PREC_NONE},
    [TOKEN_VAR]                = {NULL,          NULL,         PREC_NONE},
    [TOKEN_WHILE]              = {NULL,          NULL,         PREC_NONE},
    [TOKEN_STRUCT]             = {NULL,          NULL,         PREC_NONE},
    [TOKEN_UNION]              = {NULL,          NULL,         PREC_NONE},
    [TOKEN_DO]                 = {NULL,          NULL,         PREC_NONE},
    [TOKEN_SWITCH]             = {NULL,          NULL,         PREC_NONE},
    [TOKEN_CASE]               = {NULL,          NULL,         PREC_NONE},
    [TOKEN_DEFAULT]            = {NULL,          NULL,         PREC_NONE},
    [TOKEN_BREAK]              = {NULL,          NULL,         PREC_NONE},
    [TOKEN_CONTINUE]           = {NULL,          NULL,         PREC_NONE},
    [TOKEN_GOTO]               = {NULL,          NULL,         PREC_NONE},
    [TOKEN_SIZEOF]             = {unaryExp,      NULL,         PREC_NONE},
    [TOKEN_STATIC]             = {NULL,          NULL,         PREC_NONE},
    [TOKEN_EXTERN]             = {NULL,          NULL,         PREC_NONE},
    [TOKEN_AS]                 = {NULL,          castExp,      PREC_CAST},
    [TOKEN_ERROR]              = {NULL,          NULL,         PREC_NONE},
    [TOKEN_EOF]                = {NULL,          NULL,         PREC_NONE},
    [TOKEN_SECTION]            = {NULL,           NULL,         PREC_NONE}
};

static const ParseRule* getRule(TokenType type) { return &rules[type]; }

static bool isAssignable(const CExp* exp) {
    if (!exp) return false;
    switch (exp->type) {
        case AST_CVar_t:
        case AST_CSubscript_t:
        case AST_CDot_t:
        case AST_CArrow_t:
        case AST_CDereference_t:
            return true;
        default:
            return false;
    }
}

static bool compoundBinop(TokenType type, CBinaryOp* out) {
    switch (type) {
        case TOKEN_PLUS_EQUAL:        *out = init_CAdd(); return true;
        case TOKEN_MINUS_EQUAL:       *out = init_CSubtract(); return true;
        case TOKEN_STAR_EQUAL:        *out = init_CMultiply(); return true;
        case TOKEN_SLASH_EQUAL:       *out = init_CDivide(); return true;
        case TOKEN_PERCENT_EQUAL:     *out = init_CRemainder(); return true;
        case TOKEN_AMP_EQUAL:         *out = init_CBitAnd(); return true;
        case TOKEN_PIPE_EQUAL:        *out = init_CBitOr(); return true;
        case TOKEN_CARET_EQUAL:       *out = init_CBitXor(); return true;
        case TOKEN_SHIFT_LEFT_EQUAL:  *out = init_CBitShiftLeft(); return true;
        case TOKEN_SHIFT_RIGHT_EQUAL: *out = init_CBitShiftRight(); return true;
        default: return false;
    }
}

static unique_ptr_t(CExp) parsePrecedence(Precedence precedence) {
    advance();

    PrefixFn prefixRule = getRule(P.previous.tok.type)->prefix;
    if (prefixRule == NULL) {
        error("expect expression");
        return uptr_new();
    }

    unique_ptr_t(CExp) left = prefixRule();
    if (!left) return uptr_new();

    while (precedence <= getRule(P.current.tok.type)->precedence) {
        // '?' and '=' are handled below, not as infix rules.
        if (check(TOKEN_QUESTION)) break;
        advance();
        InfixFn infixRule = getRule(P.previous.tok.type)->infix;
        if (infixRule == NULL) break;
        left = infixRule(left);
        if (!left) return uptr_new();
    }

    // Ternary, right-associative.
    if (precedence <= PREC_CONDITIONAL && match(TOKEN_QUESTION)) {
        size_t info_at = P.previous.info_at;
        unique_ptr_t(CExp) middle = expression();
        if (!middle) return uptr_new();
        if (!consume(TOKEN_COLON, "expect ':' in conditional expression")) return uptr_new();
        unique_ptr_t(CExp) right = parsePrecedence(PREC_CONDITIONAL);
        if (!right) return uptr_new();
        return make_CConditional(&left, &middle, &right, info_at);
    }

    // Assignment is resolved here, after the infix loop, so the whole left-hand
    // node is already in hand. p->x = v, buf[i] = v and *q = v all work with no
    // per-rule special casing, and recursing at PREC_ASSIGNMENT makes
    // a = b = c right-associative.
    if (precedence <= PREC_ASSIGNMENT) {
        CBinaryOp binop = init_CBinaryOp();

        if (match(TOKEN_EQUAL)) {
            size_t info_at = P.previous.info_at;
            if (!isAssignable(left)) {
                error("invalid assignment target");
                return uptr_new();
            }
            unique_ptr_t(CExp) value = parsePrecedence(PREC_ASSIGNMENT);
            if (!value) return uptr_new();
            CUnaryOp unop = init_CUnaryOp();
            return make_CAssignment(&unop, &left, &value, info_at);
        }

        if (compoundBinop(P.current.tok.type, &binop)) {
            advance();
            size_t info_at = P.previous.info_at;
            if (!isAssignable(left)) {
                error("invalid assignment target");
                return uptr_new();
            }
            unique_ptr_t(CExp) value = parsePrecedence(PREC_ASSIGNMENT);
            if (!value) return uptr_new();

            // a |= b  ->  Assignment(_, NULL, CBinary(CBitOr, a, b)).
            // The null left operand is how wheelcc marks a compound assignment;
            // semantic analysis recovers the target from inside the binary node.
            unique_ptr_t(CExp) exp_null = uptr_new();
            unique_ptr_t(CExp) combined = make_CBinary(&binop, &left, &value, info_at);
            CUnaryOp unop = init_CUnaryOp();
            return make_CAssignment(&unop, &exp_null, &combined, info_at);
        }
    }

    return left;
}

static unique_ptr_t(CExp) expression(void) { return parsePrecedence(PREC_ASSIGNMENT); }

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Statements

static unique_ptr_t(CStatement) statement(void);
static unique_ptr_t(CBlock) block(void);
static unique_ptr_t(CVariableDeclaration) varDeclaration(CStorageClass storage_class, bool has_section, TIdentifier section); 
static unique_ptr_t(CInitializer) initializer(void);
static unique_ptr_t(CStatement) returnStatement(void) {
    size_t info_at = P.previous.info_at;
    unique_ptr_t(CExp) value = uptr_new();
    if (!check(TOKEN_SEMICOLON)) {
        value = expression();
        if (!value) return uptr_new();
    }
    if (!consume(TOKEN_SEMICOLON, "expect ';' after return")) return uptr_new();
    return make_CReturn(&value, info_at);
}

static unique_ptr_t(CStatement) ifStatement(void) {
    if (!consume(TOKEN_LEFT_PAREN, "expect '(' after 'if'")) return uptr_new();
    unique_ptr_t(CExp) condition = expression();
    if (!condition) return uptr_new();
    if (!consume(TOKEN_RIGHT_PAREN, "expect ')' after condition")) return uptr_new();

    unique_ptr_t(CStatement) then = statement();
    if (!then) return uptr_new();

    unique_ptr_t(CStatement) else_fi = uptr_new();
    if (match(TOKEN_ELSE)) {
        else_fi = statement();
        if (!else_fi) return uptr_new();
    }
    return make_CIf(&condition, &then, &else_fi);
}

static unique_ptr_t(CStatement) whileStatement(void) {
    if (!consume(TOKEN_LEFT_PAREN, "expect '(' after 'while'")) return uptr_new();
    unique_ptr_t(CExp) condition = expression();
    if (!condition) return uptr_new();
    if (!consume(TOKEN_RIGHT_PAREN, "expect ')' after condition")) return uptr_new();
    unique_ptr_t(CStatement) body = statement();
    if (!body) return uptr_new();
    return make_CWhile(&condition, &body);
}

static unique_ptr_t(CStatement) doWhileStatement(void) {
    unique_ptr_t(CStatement) body = statement();
    if (!body) return uptr_new();
    if (!consume(TOKEN_WHILE, "expect 'while' after do body")) return uptr_new();
    if (!consume(TOKEN_LEFT_PAREN, "expect '(' after 'while'")) return uptr_new();
    unique_ptr_t(CExp) condition = expression();
    if (!condition) return uptr_new();
    if (!consume(TOKEN_RIGHT_PAREN, "expect ')' after condition")) return uptr_new();
    if (!consume(TOKEN_SEMICOLON, "expect ';' after do-while")) return uptr_new();
    return make_CDoWhile(&condition, &body);
}

static unique_ptr_t(CStatement) forStatement(void) {
    if (!consume(TOKEN_LEFT_PAREN, "expect '(' after 'for'")) return uptr_new();

    unique_ptr_t(CForInit) init = uptr_new();
    if (match(TOKEN_SEMICOLON)) {
        unique_ptr_t(CExp) none = uptr_new();
        init = make_CInitExp(&none);
    }
    else if (match(TOKEN_VAR)) {
        CStorageClass sc = init_CStorageClass();
        unique_ptr_t(CVariableDeclaration) decl = varDeclaration(sc, false, 0);
        if (!decl) return uptr_new();
        init = make_CInitDecl(&decl);
    }
    else {
        unique_ptr_t(CExp) exp = expression();
        if (!exp) return uptr_new();
        if (!consume(TOKEN_SEMICOLON, "expect ';' after for initializer")) return uptr_new();
        init = make_CInitExp(&exp);
    }

    unique_ptr_t(CExp) condition = uptr_new();
    if (!check(TOKEN_SEMICOLON)) {
        condition = expression();
        if (!condition) return uptr_new();
    }
    if (!consume(TOKEN_SEMICOLON, "expect ';' after for condition")) return uptr_new();

    unique_ptr_t(CExp) post = uptr_new();
    if (!check(TOKEN_RIGHT_PAREN)) {
        post = expression();
        if (!post) return uptr_new();
    }
    if (!consume(TOKEN_RIGHT_PAREN, "expect ')' after for clauses")) return uptr_new();

    unique_ptr_t(CStatement) body = statement();
    if (!body) return uptr_new();
    return make_CFor(&init, &condition, &post, &body);
}

static unique_ptr_t(CStatement) switchStatement(void) {
    if (!consume(TOKEN_LEFT_PAREN, "expect '(' after 'switch'")) return uptr_new();
    unique_ptr_t(CExp) match_exp = expression();
    if (!match_exp) return uptr_new();
    if (!consume(TOKEN_RIGHT_PAREN, "expect ')' after switch subject")) return uptr_new();
    unique_ptr_t(CStatement) body = statement();
    if (!body) return uptr_new();
    return make_CSwitch(&match_exp, &body);
}

static unique_ptr_t(CStatement) caseStatement(void) {
    unique_ptr_t(CExp) value = expression();
    if (!value) return uptr_new();
    if (!consume(TOKEN_COLON, "expect ':' after case value")) return uptr_new();
    unique_ptr_t(CStatement) jump_to = statement();
    if (!jump_to) return uptr_new();
    return make_CCase(&value, &jump_to);
}

static unique_ptr_t(CStatement) defaultStatement(void) {
    size_t info_at = P.previous.info_at;
    if (!consume(TOKEN_COLON, "expect ':' after 'default'")) return uptr_new();
    unique_ptr_t(CStatement) jump_to = statement();
    if (!jump_to) return uptr_new();
    return make_CDefault(&jump_to, info_at);
}

static unique_ptr_t(CStatement) gotoStatement(void) {
    size_t info_at = P.previous.info_at;
    if (!consume(TOKEN_IDENTIFIER, "expect label after 'goto'")) return uptr_new();
    TIdentifier target = internPrevious();
    if (!consume(TOKEN_SEMICOLON, "expect ';' after goto")) return uptr_new();
    return make_CGoto(target, info_at);
}

static unique_ptr_t(CStatement) expressionStatement(void) {
    unique_ptr_t(CExp) exp = expression();
    if (!exp) return uptr_new();
    if (!consume(TOKEN_SEMICOLON, "expect ';' after expression")) return uptr_new();
    return make_CExpression(&exp);
}

static unique_ptr_t(CStatement) statement(void) {
    if (match(TOKEN_RETURN))   return returnStatement();
    if (match(TOKEN_IF))       return ifStatement();
    if (match(TOKEN_WHILE))    return whileStatement();
    if (match(TOKEN_DO))       return doWhileStatement();
    if (match(TOKEN_FOR))      return forStatement();
    if (match(TOKEN_SWITCH))   return switchStatement();
    if (match(TOKEN_CASE))     return caseStatement();
    if (match(TOKEN_DEFAULT))  return defaultStatement();
    if (match(TOKEN_GOTO))     return gotoStatement();

    if (match(TOKEN_BREAK)) {
        size_t info_at = P.previous.info_at;
        if (!consume(TOKEN_SEMICOLON, "expect ';' after 'break'")) return uptr_new();
        return make_CBreak(info_at);
    }
    if (match(TOKEN_CONTINUE)) {
        size_t info_at = P.previous.info_at;
        if (!consume(TOKEN_SEMICOLON, "expect ';' after 'continue'")) return uptr_new();
        return make_CContinue(info_at);
    }
    if (match(TOKEN_SEMICOLON)) return make_CNull();

    if (match(TOKEN_LEFT_BRACE)) {
        unique_ptr_t(CBlock) b = block();
        if (!b) return uptr_new();
        return make_CCompound(&b);
    }

    // `ident :` is a label; anything else starting with an identifier is an
    // expression. One token of lookahead settles it.
    if (check(TOKEN_IDENTIFIER)) {
        const char* save_start = lexer.start;
        const char* save_current = lexer.current;
        const char* save_lineStart = lexer.lineStart;
        int save_line = lexer.line;
        PToken save_prev = P.previous;
        PToken save_cur = P.current;

        advance();
        if (check(TOKEN_COLON)) {
            size_t info_at = P.previous.info_at;
            TIdentifier target = internPrevious();
            advance();   // consume ':'
            unique_ptr_t(CStatement) jump_to = statement();
            if (!jump_to) return uptr_new();
            return make_CLabel(target, &jump_to, info_at);
        }

        lexer.start = save_start;
        lexer.current = save_current;
        lexer.lineStart = save_lineStart;
        lexer.line = save_line;
        P.previous = save_prev;
        P.current = save_cur;
    }

    return expressionStatement();
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Declarations

//  '{' initializer { ',' initializer } [ ',' ] '}'  |  expression
static unique_ptr_t(CInitializer) initializer(void) {
    if (match(TOKEN_LEFT_BRACE)) {
        vector_t(unique_ptr_t(CInitializer)) inits = vec_new();
        if (!check(TOKEN_RIGHT_BRACE)) {
            do {
                if (check(TOKEN_RIGHT_BRACE)) break;   // trailing comma
                unique_ptr_t(CInitializer) item = initializer();
                if (!item) return uptr_new();
                vec_move_back(inits, item);
            }
            while (match(TOKEN_COMMA));
        }
        if (!consume(TOKEN_RIGHT_BRACE, "expect '}' after initializer list")) return uptr_new();
        return make_CCompoundInit(&inits);
    }

    unique_ptr_t(CExp) exp = expression();
    if (!exp) return uptr_new();
    return make_CSingleInit(&exp);
}

//  var NAME ':' type [ '=' initializer ] ';'
static unique_ptr_t(CVariableDeclaration) varDeclaration(CStorageClass storage_class, bool has_section, TIdentifier section) {    
    if (!consume(TOKEN_IDENTIFIER, "expect variable name")) return uptr_new();
    size_t info_at = P.previous.info_at;
    TIdentifier name = internPrevious();

    if (!consume(TOKEN_COLON, "expect ':' and a type after variable name")) return uptr_new();
    shared_ptr_t(Type) var_type = parseType();
    if (!var_type) return uptr_new();

    unique_ptr_t(CInitializer) init = uptr_new();
    if (match(TOKEN_EQUAL)) {
        init = initializer();
        if (!init) return uptr_new();
    }
    if (!consume(TOKEN_SEMICOLON, "expect ';' after variable declaration")) return uptr_new();

    return make_CVariableDeclaration(name, &init, &var_type, &storage_class, has_section, section, info_at);
}

static unique_ptr_t(CBlockItem) blockItem(void) {
    CStorageClass sc = init_CStorageClass();
    bool hasStorage = false;

    if (match(TOKEN_STATIC)) { sc = init_CStatic(); hasStorage = true; }
    else if (match(TOKEN_EXTERN)) { sc = init_CExtern(); hasStorage = true; }

    if (match(TOKEN_VAR)) {
        unique_ptr_t(CVariableDeclaration) var_decl = varDeclaration(sc, false, 0);
        if (!var_decl) return uptr_new();
        unique_ptr_t(CDeclaration) decl = make_CVarDecl(&var_decl);
        return make_CD(&decl);
    }

    if (hasStorage) {
        error("expect 'var' after storage class");
        return uptr_new();
    }

    unique_ptr_t(CStatement) stmt = statement();
    if (!stmt) return uptr_new();
    return make_CS(&stmt);
}

static unique_ptr_t(CBlock) block(void) {
    vector_t(unique_ptr_t(CBlockItem)) items = vec_new();
    while (!check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF)) {
        unique_ptr_t(CBlockItem) item = blockItem();
        if (!item) return uptr_new();
        vec_move_back(items, item);
    }
    if (!consume(TOKEN_RIGHT_BRACE, "expect '}' after block")) return uptr_new();
    return make_CB(&items);
}

//  fun NAME '(' [ NAME ':' type { ',' NAME ':' type } ] ')' [ '->' type ] ( block | ';' )
static unique_ptr_t(CDeclaration) funDeclaration(CStorageClass storage_class, bool has_section, TIdentifier section) {    
    if (!consume(TOKEN_IDENTIFIER, "expect function name")) return uptr_new();
    size_t info_at = P.previous.info_at;
    TIdentifier name = internPrevious();

    if (!consume(TOKEN_LEFT_PAREN, "expect '(' after function name")) return uptr_new();

    vector_t(TIdentifier) params = vec_new();
    vector_t(shared_ptr_t(Type)) param_types = vec_new();

    if (!check(TOKEN_RIGHT_PAREN)) {
        do {
            if (!consume(TOKEN_IDENTIFIER, "expect parameter name")) return uptr_new();
            TIdentifier param = internPrevious();
            if (!consume(TOKEN_COLON, "expect ':' and a type after parameter name")) return uptr_new();
            shared_ptr_t(Type) param_type = parseType();
            if (!param_type) return uptr_new();
            vec_push_back(params, param);
            vec_move_back(param_types, param_type);
        }
        while (match(TOKEN_COMMA));
    }
    if (!consume(TOKEN_RIGHT_PAREN, "expect ')' after parameters")) return uptr_new();

    // No ambiguity with the member-access arrow: a return arrow only ever
    // follows ')', a member arrow only ever follows an expression.
    shared_ptr_t(Type) ret_type = sptr_new();
    if (match(TOKEN_ARROW)) {
        ret_type = parseType();
        if (!ret_type) return uptr_new();
    }
    else {
        ret_type = make_Void();
    }

    shared_ptr_t(Type) fun_type = make_FunType(&param_types, &ret_type);

    unique_ptr_t(CBlock) body = uptr_new();
    if (match(TOKEN_LEFT_BRACE)) {
        body = block();
        if (!body) return uptr_new();
    }
    else if (!consume(TOKEN_SEMICOLON, "expect '{' or ';' after function signature")) {
        return uptr_new();
    }

    unique_ptr_t(CFunctionDeclaration) fun_decl
        = make_CFunctionDeclaration(name, &params, &body, &fun_type, &storage_class, has_section, section, info_at);
    return make_CFunDecl(&fun_decl);
}

//  ( struct | union ) NAME [ '{' { NAME ':' type ',' } '}' ] ';'
static unique_ptr_t(CDeclaration) structDeclaration(bool is_union, bool is_packed) {
    if (!consume(TOKEN_IDENTIFIER, "expect struct or union name")) return uptr_new();
    size_t info_at = P.previous.info_at;
    TIdentifier tag = internPrevious();

    vector_t(unique_ptr_t(CMemberDeclaration)) members = vec_new();

    if (match(TOKEN_LEFT_BRACE)) {
        while (!check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF)) {
            if (!consume(TOKEN_IDENTIFIER, "expect member name")) return uptr_new();
            size_t member_info = P.previous.info_at;
            TIdentifier member_name = internPrevious();
            if (!consume(TOKEN_COLON, "expect ':' and a type after member name")) return uptr_new();
            shared_ptr_t(Type) member_type = parseType();
            if (!member_type) return uptr_new();

            unique_ptr_t(CMemberDeclaration) member
                = make_CMemberDeclaration(member_name, &member_type, member_info);
            vec_move_back(members, member);

            if (!match(TOKEN_COMMA)) break;
        }
        if (!consume(TOKEN_RIGHT_BRACE, "expect '}' after struct body")) return uptr_new();
    }

    if (!consume(TOKEN_SEMICOLON, "expect ';' after struct declaration")) return uptr_new();

    unique_ptr_t(CStructDeclaration) struct_decl = make_CStructDeclaration(tag, is_union, is_packed, &members, info_at);    return make_CStructDecl(&struct_decl);
}

static void synchronize(void) {
    P.panicMode = false;
    while (!check(TOKEN_EOF)) {
        switch (P.current.tok.type) {
            case TOKEN_FUN:
            case TOKEN_VAR:
            case TOKEN_STRUCT:
            case TOKEN_UNION:
            case TOKEN_STATIC:
            case TOKEN_EXTERN:
                return;
            default:
                break;
        }
        advance();
    }
}
static unique_ptr_t(CDeclaration) declaration(void) {
    CStorageClass sc = init_CStorageClass();
    if (match(TOKEN_STATIC)) sc = init_CStatic();
    else if (match(TOKEN_EXTERN)) sc = init_CExtern();

    bool is_packed = match(TOKEN_PACKED);
    bool has_section = false;
    TIdentifier section = 0;
    if (match(TOKEN_SECTION)) {
        if (!consume(TOKEN_STRING, "expect section name string after 'section'")) return uptr_new();
        has_section = true;
        section = internPrevious();
    }
    if (match(TOKEN_FUN))    return funDeclaration(sc, has_section, section);    
    if (match(TOKEN_STRUCT)) return structDeclaration(false, is_packed);
    if (match(TOKEN_UNION))  return structDeclaration(true, is_packed);
    if (match(TOKEN_VAR)) {
        unique_ptr_t(CVariableDeclaration) var_decl = varDeclaration(sc, has_section, section);
        if (!var_decl) return uptr_new();
        return make_CVarDecl(&var_decl);
    }

    errorAtCurrent("expect 'fun', 'var', 'struct' or 'union' at top level");
    advance();
    return uptr_new();
}

error_t parse_prism(const char* source, const char* filename, ErrorsContext* errors,
    IdentifierContext* identifiers, unique_ptr_t(CProgram) * c_ast) {

    P.errors = errors;
    P.identifiers = identifiers;
    P.filename = filename;
    P.hadError = false;
    P.panicMode = false;

    // raise_error_at_token walks fopen_lines to map a running line number back
    // to a file; with an empty vector its `vec_size(...) - 1` underflows.
    {
        FileOpenLine fopen_line = {1, 1, str_new(filename)};
        vec_push_back(errors->fopen_lines, fopen_line);
    }

    initLexer(source);
    advance();

    vector_t(unique_ptr_t(CDeclaration)) declarations = vec_new();
    while (!check(TOKEN_EOF)) {
        unique_ptr_t(CDeclaration) decl = declaration();
        if (decl) {
            vec_move_back(declarations, decl);
        }
        if (P.panicMode) synchronize();
    }

    *c_ast = make_CProgram(&declarations);
    return P.hadError ? 1 : 0;
}
