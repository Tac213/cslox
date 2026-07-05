#include "debug.h"
#include "chunk.h"
#include "value.h"
#include <stdio.h>

static int simpleInstruction(const char *name, int offset) {
    fprintf(stdout, "%s\n", name);
    return offset + 1;
}

static int constantInstruction(const char *name, Chunk *chunk, int offset) {
    uint8_t constant = chunk->code[offset + 1];
    fprintf(stdout, "%-16s %4d '", name, constant);
    printValue(stdout, chunk->constants.values[constant]);
    fprintf(stdout, "'\n");
    return offset + 2;
}

void disassembleChunk(Chunk *chunk, const char *name) {
    fprintf(stdout, "== %s ==\n", name);

    for (int offset = 0; offset < chunk->count;) {
        offset = disassembleInstruction(chunk, offset);
    }
}

int disassembleInstruction(Chunk *chunk, int offset) {
    fprintf(stdout, "%04d ", offset);
    if (offset > 0 && chunk->lines[offset] == chunk->lines[offset - 1]) {
        fprintf(stdout, "   | ");
    } else {
        fprintf(stdout, "%4d ", chunk->lines[offset]);
    }

    uint8_t instruction = chunk->code[offset];
    switch (instruction) {
    case OP_CONSTANT:
        return constantInstruction("OP_CONSTANT", chunk, offset);
    case OP_RETURN:
        return simpleInstruction("OP_RETURN", offset);
    default:
        fprintf(stdout, "Unknown opcode %d\n", instruction);
        return offset + 1;
    }
}
