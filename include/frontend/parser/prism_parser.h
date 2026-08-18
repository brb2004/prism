#ifndef prism_parser_h
#define prism_parser_h

#include "util/c_std.h"
#include "util/throw.h"

typedef struct CProgram CProgram;
typedef struct ErrorsContext ErrorsContext;
typedef struct IdentifierContext IdentifierContext;


#ifdef __cplusplus
extern "C" {
#endif
error_t parse_prism(const char* source, const char* filename, ErrorsContext* errors,
    IdentifierContext* identifiers, unique_ptr_t(CProgram) * c_ast);
#ifdef __cplusplus
}
#endif

#endif
