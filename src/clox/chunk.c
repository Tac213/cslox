#include "chunk.h"
#include "memory.h"
#include "value.h"
#include <stdint.h>

void initChunk(Chunk *chunk) {
    chunk->count = 0;
    chunk->capacity = 0;
    chunk->code = NULL;
    chunk->lines.count = 0;
    chunk->lines.capacity = 0;
    chunk->lines.rleLines = NULL;
    initValueArray(&chunk->constants);
}

void freeChunk(Chunk *chunk) {
    FREE_ARRAY(uint8_t, chunk->code, chunk->capacity);
    FREE_ARRAY(uint32_t, chunk->lines.rleLines, chunk->lines.capacity);
    freeValueArray(&chunk->constants);
    initChunk(chunk);
}

void writeChunk(Chunk *chunk, uint8_t byte, uint32_t line) {
    if (chunk->capacity < chunk->count + 1) {
        int oldCapacity = chunk->capacity;
        chunk->capacity = GROW_CAPACITY(oldCapacity);
        chunk->code =
            GROW_ARRAY(uint8_t, chunk->code, oldCapacity, chunk->capacity);
    }

    chunk->code[chunk->count] = byte;
    chunk->count++;

    uint32_t *count = &chunk->lines.count;
    if (*count == 0 || chunk->lines.rleLines[*count - 2] != line) {
        if (chunk->lines.capacity < *count + 2) {
            uint32_t oldCapacity = chunk->lines.capacity;
            chunk->lines.capacity = GROW_CAPACITY(oldCapacity);
            chunk->lines.rleLines =
                GROW_ARRAY(uint32_t, chunk->lines.rleLines, oldCapacity,
                           chunk->lines.capacity);
        }
        // Start a new run: store the line number followed by a run count of 0.
        chunk->lines.rleLines[*count] = line;
        chunk->lines.rleLines[*count + 1] = 0;
        *count += 2;
    }

    chunk->lines.rleLines[*count - 1]++;
}

int addConstant(Chunk *chunk, Value value) {
    writeValueArray(&chunk->constants, value);
    return chunk->constants.count - 1;
}

uint32_t getLine(Chunk *chunk, uint32_t offset) {
    if (chunk->lines.count == 0) {
        return 0;
    }

    uint32_t count = 0;
    for (uint32_t i = 0; i < chunk->lines.count; i += 2) {
        count += chunk->lines.rleLines[i + 1];
        if (count > offset) {
            return chunk->lines.rleLines[i];
        }
    }

    // Offset is beyond all known runs; fall back to the last recorded line.
    return chunk->lines.rleLines[chunk->lines.count - 2];
}
