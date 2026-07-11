#include "vm.h"
#ifdef DEBUG_TRACE_EXECUTION
#include "debug.h"
#endif
#include <stdbool.h>
#include <stdio.h>

static VM vm;

static InterpretResult run() {
#define READ_BYTE() (*vm.ip++)
#define READ_CONSTANT() (vm.chunk->constants.values[READ_BYTE()])
#define BINARY_OP(op)                                                          \
    do {                                                                       \
        Value *b = vm.stackTop - 1;                                            \
        Value *a = vm.stackTop - 2;                                            \
        *a = (*a)op(*b);                                                       \
        vm.stackTop--;                                                         \
    } while (false)

    for (;;) {
#ifdef DEBUG_TRACE_EXECUTION
        fprintf(stdout, "          ");
        for (Value *slot = vm.stack; slot < vm.stackTop; slot++) {
            fprintf(stdout, "[ ");
            printValue(stdout, *slot);
            fprintf(stdout, " ]");
        }
        fprintf(stdout, "\n");
        disassembleInstruction(vm.chunk, (uint32_t)(vm.ip - vm.chunk->code));
#endif

        uint8_t instruction;
        switch (instruction = READ_BYTE()) {
        case OP_CONSTANT: {
            Value constant = READ_CONSTANT();
            push(constant);
            break;
        }
        case OP_CONSTANT_LONG: {
            uint32_t constantIndex =
                (uint32_t)READ_BYTE() | ((uint32_t)READ_BYTE() << 8) |
                ((uint32_t)READ_BYTE() << 16) | ((uint32_t)READ_BYTE() << 24);
            Value constant = vm.chunk->constants.values[constantIndex];
            push(constant);
            break;
        }
        case OP_ADD:
            BINARY_OP(+);
            break;
        case OP_SUBTRACT:
            BINARY_OP(-);
            break;
        case OP_MULTIPLY:
            BINARY_OP(*);
            break;
        case OP_DIVIDE:
            BINARY_OP(/);
            break;
        case OP_NEGATE: {
            Value *value = vm.stackTop - 1;
            *value = -(*value);
            break;
        }
        case OP_RETURN: {
            printValue(stdout, pop());
            fprintf(stdout, "\n");
            return INTERPRET_OK;
        }
        default: {
            break;
        }
        }
    }

#undef READ_BYTE
#undef READ_CONSTANT
#undef BINARY_OP
}

static void resetStack() { vm.stackTop = vm.stack; }

void initVM() { resetStack(); }

void freeVM() {}

InterpretResult interpret(Chunk *chunk) {
    vm.chunk = chunk;
    vm.ip = vm.chunk->code;
    return run();
}

void push(Value value) {
    *vm.stackTop = value;
    vm.stackTop++;
}

Value pop() {
    vm.stackTop--;
    return *vm.stackTop;
}
