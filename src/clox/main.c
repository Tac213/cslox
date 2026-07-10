#include "chunk.h"
#include "debug.h"

int main(int argc, const char *argv[]) {
    Chunk chunk;
    initChunk(&chunk);

    writeConstant(&chunk, 1.2, 1);
    writeChunk(&chunk, OP_RETURN, 2);

    disassembleChunk(&chunk, "test chunk");
    freeChunk(&chunk);
    return 0;
}
