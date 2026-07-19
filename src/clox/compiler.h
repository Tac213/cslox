#ifndef clox_compiler_h
#define clox_compiler_h

#include "object.h"
#include <stdbool.h>

ObjFunction *compile(const char *source, bool isREPL);

#endif
