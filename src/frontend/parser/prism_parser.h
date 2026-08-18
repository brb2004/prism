#ifndef prism_parser_h
#define prism_parser_h

#include "util/c_std.h"
#include "util/throw.h"

typedef struct CProgram CProgram;
typedef struct ErrorsContext ErrorsContext;
typedef struct IdentifierContext IdentifierContext;

// Lexes and parses Prism source straight into wheelcc's frontend AST.
// Replaces the lex_c_code + parse_tokens pair in main.c.
//
// `source` must stay alive for the duration of the call only - every
// identifier is interned into `identifiers` before returning.
// `filename` is used for error messages.
//
// Returns 0 on success, 1 if any syntax error was reported to stderr.
#ifdef __cplusplus
extern "C" {
#endif
error_t parse_prism(const char* source, const char* filename, ErrorsContext* errors,
    IdentifierContext* identifiers, unique_ptr_t(CProgram) * c_ast);
#ifdef __cplusplus
}
#endif

#endif
