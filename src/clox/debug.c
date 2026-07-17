#include "debug.h"
#include "chunk.h"
#include "value.h"
#include <stdio.h>

static uint32_t simpleInstruction(const char *name, uint32_t offset) {
    fprintf(stdout, "%s\n", name);
    return offset + 1;
}

static uint32_t jumpInstruction(const char *name, int sign, Chunk *chunk,
                                uint32_t offset) {
    uint16_t jump = (uint16_t)(chunk->code[offset + 1] << 8);
    jump |= chunk->code[offset + 2];
    fprintf(stdout, "%-16s %4d -> %d\n", name, offset,
            offset + 3 + (sign * jump));
    return offset + 3;
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
    uint32_t constant = ((uint32_t)chunk->code[offset + 1] << 24) |
                        ((uint32_t)chunk->code[offset + 2] << 16) |
                        ((uint32_t)chunk->code[offset + 3] << 8) |
                        (uint32_t)chunk->code[offset + 4];
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
    case OP_NIL:
        return simpleInstruction("OP_NIL", offset);
    case OP_TRUE:
        return simpleInstruction("OP_TRUE", offset);
    case OP_FALSE:
        return simpleInstruction("OP_FALSE", offset);
    case OP_UNDEFINED:
        return simpleInstruction("OP_UNDEFINED", offset);
    case OP_POP:
        return simpleInstruction("OP_POP", offset);
    case OP_GET_GLOBAL:
        return constantInstruction("OP_GET_GLOBAL", chunk, offset);
    case OP_DEFINE_GLOBAL:
        return constantInstruction("OP_DEFINE_GLOBAL", chunk, offset);
    case OP_SET_GLOBAL:
        return constantInstruction("OP_SET_GLOBAL", chunk, offset);
    case OP_EQUAL:
        return simpleInstruction("OP_EQUAL", offset);
    case OP_GREATER:
        return simpleInstruction("OP_GREATER", offset);
    case OP_GREATER_EQUAL:
        return simpleInstruction("OP_GREATER_EQUAL", offset);
    case OP_LESS:
        return simpleInstruction("OP_LESS", offset);
    case OP_LESS_EQUAL:
        return simpleInstruction("OP_LESS_EQUAL", offset);
    case OP_ADD:
        return simpleInstruction("OP_ADD", offset);
    case OP_SUBTRACT:
        return simpleInstruction("OP_SUBTRACT", offset);
    case OP_MULTIPLY:
        return simpleInstruction("OP_MULTIPLY", offset);
    case OP_DIVIDE:
        return simpleInstruction("OP_DIVIDE", offset);
    case OP_NOT:
        return simpleInstruction("OP_NOT", offset);
    case OP_NEGATE:
        return simpleInstruction("OP_NEGATE", offset);
    case OP_PRINT:
        return simpleInstruction("OP_PRINT", offset);
    case OP_JUMP:
        return jumpInstruction("OP_JUMP", 1, chunk, offset);
    case OP_JUMP_IF_FALSE:
        return jumpInstruction("OP_JUMP_IF_FALSE", 1, chunk, offset);
    case OP_RETURN:
        return simpleInstruction("OP_RETURN", offset);
    default:
        fprintf(stdout, "Unknown opcode %d\n", instruction);
        return offset + 1;
    }
}
