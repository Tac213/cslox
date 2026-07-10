#ifndef clox_debug_h
#define clox_debug_h

#include "chunk.h"

void disassembleChunk(Chunk *chunk, const char *name);
uint32_t disassembleInstruction(Chunk *chunk, uint32_t offset);

#endif
