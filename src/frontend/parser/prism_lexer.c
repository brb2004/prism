#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "prism_lexer.h"

Lexer lexer;

// Set by skipWhitespace() when a block comment runs off the end of the source.
// scanToken() checks it and emits an error token, since skipWhitespace() is void.
static bool hadCommentError;

void initLexer(const char* source) {
  lexer.start = source;
  lexer.current = source;
  lexer.lineStart = source;
  lexer.line = 1;
}

static bool isAlpha(char c) {
  return (c >= 'a' && c <= 'z') ||
         (c >= 'A' && c <= 'Z') ||
          c == '_';
}

static bool isDigit(char c) {
  return c >= '0' && c <= '9';
}

static bool isHexDigit(char c) {
  return (c >= '0' && c <= '9') ||
         (c >= 'a' && c <= 'f') ||
         (c >= 'A' && c <= 'F');
}

static bool isAtEnd(void) {
  return *lexer.current == '\0';
}

static Token makeToken(TokenType type) {
  Token token;
  token.type = type;
  token.start = lexer.start;
  token.length = (int)(lexer.current - lexer.start);
  token.line = lexer.line;
  token.col = (int)(lexer.start - lexer.lineStart) + 1;
  return token;
}

static char advance(void) {
  lexer.current++;
  return lexer.current[-1];
}

static char peek(void) {
  return *lexer.current;
}

static char peekNext(void) {
  if (isAtEnd()) return '\0';
  return lexer.current[1];
}

static bool match(char expected) {
  if (isAtEnd()) return false;
  if (*lexer.current != expected) return false;
  lexer.current++;
  return true;
}

static Token errorToken(const char* message) {
  Token token;
  token.type = TOKEN_ERROR;
  token.start = message;
  token.length = (int)strlen(message);
  token.line = lexer.line;
  token.col = (int)(lexer.start - lexer.lineStart) + 1;
  return token;
}

// Every place the line counter bumps must also reset lineStart, or columns
// drift for the rest of the file.
static void newline(void) {
  lexer.line++;
  advance();
  lexer.lineStart = lexer.current;
}

static void skipWhitespace(void) {
  for (;;) {
    char c = peek();
    switch (c) {
      case ' ':
      case '\r':
      case '\t':
        advance();
        break;
      case '\n':
        newline();
        break;
      case '/':
        if (peekNext() == '/') {
          // line comment: run to end of line
          while (peek() != '\n' && !isAtEnd()) advance();
        } else if (peekNext() == '*') {
          // block comment: run to closing */
          advance(); // consume '/'
          advance(); // consume '*'
          while (!isAtEnd() && !(peek() == '*' && peekNext() == '/')) {
            if (peek() == '\n') { newline(); continue; }
            advance();
          }
          if (isAtEnd()) {
            hadCommentError = true;
            return;
          }
          advance(); // consume '*'
          advance(); // consume '/'
        } else {
          return;
        }
        break;
      default:
        return;
    }
  }
}

static TokenType checkKeyword(int start, int length,
    const char* rest, TokenType type) {
  if (lexer.current - lexer.start == start + length &&
      memcmp(lexer.start + start, rest, length) == 0) {
    return type;
  }

  return TOKEN_IDENTIFIER;
}

static TokenType identifierType(void) {
  int len = (int)(lexer.current - lexer.start);

  switch (lexer.start[0]) {
    case 'a':
      if (len > 1) {
        switch (lexer.start[1]) {
          case 'n': return checkKeyword(2, 1, "d", TOKEN_AND);
          case 's': return checkKeyword(2, 0, "", TOKEN_AS);
        }
      }
      break;
    case 'b': return checkKeyword(1, 4, "reak", TOKEN_BREAK);
    case 'c':
      if (len > 1) {
        switch (lexer.start[1]) {
          case 'a': return checkKeyword(2, 2, "se", TOKEN_CASE);
          case 'o': return checkKeyword(2, 6, "ntinue", TOKEN_CONTINUE);
        }
      }
      break;
    case 'd':
      if (len > 1) {
        switch (lexer.start[1]) {
          case 'e': return checkKeyword(2, 5, "fault", TOKEN_DEFAULT);
          case 'o': return checkKeyword(2, 0, "", TOKEN_DO);
        }
      }
      break;
    case 'e':
      if (len > 1) {
        switch (lexer.start[1]) {
          case 'l': return checkKeyword(2, 2, "se", TOKEN_ELSE);
          case 'x': return checkKeyword(2, 4, "tern", TOKEN_EXTERN);
        }
      }
      break;
    case 'f':
      if (len > 1) {
        switch (lexer.start[1]) {
          case 'a': return checkKeyword(2, 3, "lse", TOKEN_FALSE);
          case 'o': return checkKeyword(2, 1, "r", TOKEN_FOR);
          case 'u': return checkKeyword(2, 1, "n", TOKEN_FUN);
        }
      }
      break;
    case 'g': return checkKeyword(1, 3, "oto", TOKEN_GOTO);
    case 'i': return checkKeyword(1, 1, "f", TOKEN_IF);
    case 'n': return checkKeyword(1, 2, "il", TOKEN_NIL);
    case 'o': return checkKeyword(1, 1, "r", TOKEN_OR);
    case 'p': return checkKeyword(1,5, "acked", TOKEN_PACKED);
    case 'r': return checkKeyword(1, 5, "eturn", TOKEN_RETURN);
    case 's':
      if (len > 1) {
        switch (lexer.start[1]) {
          case 'i': return checkKeyword(2, 4, "zeof", TOKEN_SIZEOF);
          case 'w': return checkKeyword(2, 4, "itch", TOKEN_SWITCH);
          case 'e': return checkKeyword(2, 5, "ction", TOKEN_SECTION);
          case 't':
            if (len > 2) {
              switch (lexer.start[2]) {
                case 'r': return checkKeyword(3, 3, "uct", TOKEN_STRUCT);
                case 'a': return checkKeyword(3, 3, "tic", TOKEN_STATIC);
              }
            }
            break;
        }
      }
      break;
    case 't': return checkKeyword(1, 3, "rue", TOKEN_TRUE);
    case 'u': return checkKeyword(1, 4, "nion", TOKEN_UNION);
    case 'v': return checkKeyword(1, 2, "ar", TOKEN_VAR);
    case 'w': return checkKeyword(1, 4, "hile", TOKEN_WHILE);
  }
  return TOKEN_IDENTIFIER;
}

static Token identifier(void) {
  while (isAlpha(peek()) || isDigit(peek())) advance();
  return makeToken(identifierType());
}

static void intSuffix(void) {
  bool sawU = false, sawL = false;
  for (;;) {
    char c = peek();
    if ((c == 'u' || c == 'U') && !sawU) { sawU = true; advance(); }
    else if ((c == 'l' || c == 'L') && !sawL) { sawL = true; advance(); }
    else break;
  }
}

static Token number(void) {
  // hex literal: 0x...
  if (lexer.current[-1] == '0' && (peek() == 'x' || peek() == 'X')) {
    advance(); // consume 'x'
    if (!isHexDigit(peek())) return errorToken("Expect hex digits after '0x'.");
    while (isHexDigit(peek())) advance();
    intSuffix();
    return makeToken(TOKEN_NUMBER);
  }

  // binary literal: 0b...
  if (lexer.current[-1] == '0' && (peek() == 'b' || peek() == 'B')) {
    advance(); // consume 'b'
    if (peek() != '0' && peek() != '1') return errorToken("Expect binary digits after '0b'.");
    while (peek() == '0' || peek() == '1') advance();
    intSuffix();
    return makeToken(TOKEN_NUMBER);
  }

  while (isDigit(peek())) advance();

  // REMOVED: the fractional branch. A float literal reaches CConstDouble,
  // which means xmm registers, which fault in kernel context before SSE is
  // enabled in CR0/CR4. Reject it at the source instead of at link time.
  if (peek() == '.' && isDigit(peekNext())) {
    return errorToken("Floating-point literals are not supported.");
  }

  intSuffix();
  return makeToken(TOKEN_NUMBER);
}

// ADDED: char literals. The slice keeps its quotes and escapes; the parser
// decodes, exactly as wheelcc's string_to_char_ascii expects.
static Token charLiteral(void) {
  if (isAtEnd()) return errorToken("Unterminated character literal.");
  if (peek() == '\\') {
    advance();
    if (isAtEnd()) return errorToken("Unterminated character literal.");
    advance();
  } else if (peek() == '\'') {
    return errorToken("Empty character literal.");
  } else {
    advance();
  }
  if (peek() != '\'') return errorToken("Expect ''' after character literal.");
  advance();
  return makeToken(TOKEN_CHAR);
}

static Token string(void) {
  while (peek() != '"' && !isAtEnd()) {
    if (peek() == '\\') { advance(); if (!isAtEnd()) advance(); continue; }
    if (peek() == '\n') { newline(); continue; }
    advance();
  }

  if (isAtEnd()) return errorToken("Unterminated string.");

  advance(); // closing quote
  return makeToken(TOKEN_STRING);
}

Token scanToken(void) {
  hadCommentError = false;
  skipWhitespace();
  if (hadCommentError) return errorToken("Unterminated block comment.");
  lexer.start = lexer.current;

  if (isAtEnd()) return makeToken(TOKEN_EOF);

  char c = advance();
  if (isAlpha(c)) return identifier();
  if (isDigit(c)) return number();

  switch (c) {
    case '(': return makeToken(TOKEN_LEFT_PAREN);
    case ')': return makeToken(TOKEN_RIGHT_PAREN);
    case '{': return makeToken(TOKEN_LEFT_BRACE);
    case '}': return makeToken(TOKEN_RIGHT_BRACE);
    case '[': return makeToken(TOKEN_LEFT_BRACKET);
    case ']': return makeToken(TOKEN_RIGHT_BRACKET);
    case ';': return makeToken(TOKEN_SEMICOLON);
    case ',': return makeToken(TOKEN_COMMA);
    case '.': return makeToken(TOKEN_DOT);
    case ':': return makeToken(TOKEN_COLON);
    case '?': return makeToken(TOKEN_QUESTION);
    case '~': return makeToken(TOKEN_TILDE);
    case '\'': return charLiteral();
    case '"':  return string();

    case '+':
      if (match('+')) return makeToken(TOKEN_PLUS_PLUS);
      if (match('=')) return makeToken(TOKEN_PLUS_EQUAL);
      return makeToken(TOKEN_PLUS);
    case '-':
      if (match('>')) return makeToken(TOKEN_ARROW);
      if (match('-')) return makeToken(TOKEN_MINUS_MINUS);
      if (match('=')) return makeToken(TOKEN_MINUS_EQUAL);
      return makeToken(TOKEN_MINUS);
    case '*':
      return makeToken(match('=') ? TOKEN_STAR_EQUAL : TOKEN_STAR);
    case '/':
      return makeToken(match('=') ? TOKEN_SLASH_EQUAL : TOKEN_SLASH);
    case '%':
      return makeToken(match('=') ? TOKEN_PERCENT_EQUAL : TOKEN_PERCENT);
    case '^':
      return makeToken(match('=') ? TOKEN_CARET_EQUAL : TOKEN_CARET);
    case '&':
      if (match('&')) return makeToken(TOKEN_AMP_AMP);
      if (match('=')) return makeToken(TOKEN_AMP_EQUAL);
      return makeToken(TOKEN_AMP);
    case '|':
      if (match('|')) return makeToken(TOKEN_PIPE_PIPE);
      if (match('=')) return makeToken(TOKEN_PIPE_EQUAL);
      return makeToken(TOKEN_PIPE);
    case '!':
      return makeToken(match('=') ? TOKEN_BANG_EQUAL : TOKEN_BANG);
    case '=':
      return makeToken(match('=') ? TOKEN_EQUAL_EQUAL : TOKEN_EQUAL);
    case '<':
      if (match('=')) return makeToken(TOKEN_LESS_EQUAL);
      if (match('<')) return makeToken(match('=') ? TOKEN_SHIFT_LEFT_EQUAL : TOKEN_SHIFT_LEFT);
      return makeToken(TOKEN_LESS);
    case '>':
      if (match('=')) return makeToken(TOKEN_GREATER_EQUAL);
      if (match('>')) return makeToken(match('=') ? TOKEN_SHIFT_RIGHT_EQUAL : TOKEN_SHIFT_RIGHT);
      return makeToken(TOKEN_GREATER);
  }

  return errorToken("Unexpected character.");
}
