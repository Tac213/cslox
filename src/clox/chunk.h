#ifndef clox_chunk_h
#define clox_chunk_h

#include "value.h"
#include <stdint.h>

typedef enum {
    OP_CONSTANT,
    OP_RETURN,
} OpCode;

typedef struct {
    int count;
    int capacity;
    uint8_t *code;
    // Run-length encoded line information.
    // rleLines stores pairs of (lineNumber, runCount), so each line run
    // consumes 2 entries. count is the number of entries in use (always even).
    struct {
        uint32_t count;
        uint32_t capacity;
        uint32_t *rleLines;
    } lines;
    ValueArray constants;
} Chunk;

void initChunk(Chunk *chunk);
void freeChunk(Chunk *chunk);
void writeChunk(Chunk *chunk, uint8_t byte, uint32_t line);

int addConstant(Chunk *chunk, Value value);

uint32_t getLine(Chunk *chunk, uint32_t offset);

#endif
