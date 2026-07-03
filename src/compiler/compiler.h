#ifndef prism_compiler_h
#define prism_compiler_h

#include "vm.h"
#include "obj.h"

ObjFunction* compile(const char* source);
void markCompilerRoots();

#endif