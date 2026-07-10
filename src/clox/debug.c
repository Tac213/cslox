#include "debug.h"
#include "chunk.h"
#include "value.h"
#include <stdio.h>

static uint32_t simpleInstruction(const char *name, uint32_t offset) {
    fprintf(stdout, "%s\n", name);
    return offset + 1;
}

static uint32_t constantInstruction(const char *name, Chunk *chunk,
                                    uint32_t offset) {
    uint8_t constant = chunk->code[offset + 1];
    fprintf(stdout, "%-16s %4d '", name, constant);
    printValue(stdout, chunk->constants.values[constant]);
    fprintf(stdout, "'\n");
    return offset + 2;
}

static uint32_t constantLongInstruction(const char *name, Chunk *chunk,
                                        uint32_t offset) {
    uint32_t constant = (uint32_t)chunk->code[offset + 1] |
                        ((uint32_t)chunk->code[offset + 2] << 8) |
                        ((uint32_t)chunk->code[offset + 3] << 16) |
                        ((uint32_t)chunk->code[offset + 4] << 24);
    fprintf(stdout, "%-16s %4d '", name, constant);
    printValue(stdout, chunk->constants.values[constant]);
    fprintf(stdout, "'\n");
    return offset + 5;
}

void disassembleChunk(Chunk *chunk, const char *name) {
    fprintf(stdout, "== %s ==\n", name);

    for (uint32_t offset = 0; offset < chunk->count;) {
        offset = disassembleInstruction(chunk, offset);
    }
}

uint32_t disassembleInstruction(Chunk *chunk, uint32_t offset) {
    fprintf(stdout, "%04d ", offset);
    uint32_t line = getLine(chunk, offset);
    if (offset > 0 && line == getLine(chunk, offset - 1)) {
        fprintf(stdout, "   | ");
    } else {
        fprintf(stdout, "%4d ", line);
    }

    uint8_t instruction = chunk->code[offset];
    switch (instruction) {
    case OP_CONSTANT:
        return constantInstruction("OP_CONSTANT", chunk, offset);
    case OP_CONSTANT_LONG:
        return constantLongInstruction("OP_CONSTANT_LONG", chunk, offset);
    case OP_RETURN:
        return simpleInstruction("OP_RETURN", offset);
    default:
        fprintf(stdout, "Unknown opcode %d\n", instruction);
        return offset + 1;
    }
}
