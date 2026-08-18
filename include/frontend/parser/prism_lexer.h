#ifndef prism_lexer_h
#define prism_lexer_h

typedef enum {
  // punctuation
  TOKEN_LEFT_PAREN, TOKEN_RIGHT_PAREN,
  TOKEN_LEFT_BRACE, TOKEN_RIGHT_BRACE,
  TOKEN_LEFT_BRACKET, TOKEN_RIGHT_BRACKET,
  TOKEN_COMMA, TOKEN_DOT, TOKEN_MINUS, TOKEN_PLUS,
  TOKEN_SEMICOLON, TOKEN_SLASH, TOKEN_STAR, TOKEN_PERCENT,
  TOKEN_AMP, TOKEN_PIPE, TOKEN_CARET, TOKEN_TILDE,
  TOKEN_SHIFT_LEFT, TOKEN_SHIFT_RIGHT,
  TOKEN_ARROW, TOKEN_COLON, TOKEN_QUESTION,

  // ADDED: ++ and -- feed CPrefix / CPostfix
  TOKEN_PLUS_PLUS, TOKEN_MINUS_MINUS,

  // ADDED: && and || alongside the word forms; both feed CAnd / COr
  TOKEN_AMP_AMP, TOKEN_PIPE_PIPE,

  // ADDED: compound assignment. No AST node - the parser desugars
  // `a += b` into CAssignment(CBinary(CAdd, a, b)), same as wheelcc does.
  TOKEN_PLUS_EQUAL, TOKEN_MINUS_EQUAL, TOKEN_STAR_EQUAL,
  TOKEN_SLASH_EQUAL, TOKEN_PERCENT_EQUAL, TOKEN_AMP_EQUAL,
  TOKEN_PIPE_EQUAL, TOKEN_CARET_EQUAL,
  TOKEN_SHIFT_LEFT_EQUAL, TOKEN_SHIFT_RIGHT_EQUAL,

  TOKEN_BANG, TOKEN_BANG_EQUAL,
  TOKEN_EQUAL, TOKEN_EQUAL_EQUAL,
  TOKEN_GREATER, TOKEN_GREATER_EQUAL,
  TOKEN_LESS, TOKEN_LESS_EQUAL,

  TOKEN_IDENTIFIER, TOKEN_STRING, TOKEN_NUMBER,

  // ADDED: 'a' literals feed CConstChar / CConstUChar
  TOKEN_CHAR,

  TOKEN_AND, TOKEN_OR, TOKEN_ELSE, TOKEN_FALSE, TOKEN_TRUE, TOKEN_NIL,
  TOKEN_FOR, TOKEN_FUN, TOKEN_IF, TOKEN_RETURN, TOKEN_VAR, TOKEN_WHILE,
  TOKEN_STRUCT,

  // ADDED: every one of these has an AST node that was unreachable before
  TOKEN_UNION,      // Structure(is_union = true)
  TOKEN_DO,         // CDoWhile
  TOKEN_SWITCH,     // CSwitch
  TOKEN_CASE,       // CCase
  TOKEN_DEFAULT,    // CDefault
  TOKEN_BREAK,      // CBreak
  TOKEN_CONTINUE,   // CContinue
  TOKEN_GOTO,       // CGoto  (labels are `ident :`, no new token needed)
  TOKEN_SIZEOF,     // CSizeOf / CSizeOfT
  TOKEN_STATIC,     // CStatic
  TOKEN_EXTERN,     // CExtern
  TOKEN_AS,         // CCast  -- `expr as *u8`, avoids the (T)e ambiguity

  // REMOVED: TOKEN_IMPORT. No AST node exists for it; wheelcc resolves
  // includes in the preprocessor, before the parser sees anything.

  TOKEN_ERROR, TOKEN_EOF
} TokenType;

typedef struct {
  TokenType type;
  const char* start;
  int length;
  int line;
  int col;      // ADDED: 1-based column. ErrorsContext.token_infos stores a
                // column, and start is an absolute pointer - without tracking
                // where the line began there is no way to recover it.
} Token;

typedef struct {
  const char* start;
  const char* current;
  const char* lineStart;   // ADDED: for col
  int line;
} Lexer;

extern Lexer lexer;

void initLexer(const char* source);   // was `initlexer` in the .c, `initLexer`
                                      // in the .h - they never agreed
Token scanToken(void);

#endif
