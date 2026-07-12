#include "vm.h"
#include "compiler.h"
#include "memory.h"
#include "object.h"
#ifdef DEBUG_TRACE_EXECUTION
#include "debug.h"
#endif
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

// Forward declaration
static Value peek(int distance);
static bool isFalsey(Value value);
static void concatenate();
static void runtimeError(const char *format, ...);

VM vm;

static InterpretResult run() {
#define READ_BYTE() (*vm.ip++)
#define READ_SHORT() (vm.ip += 2, (uint16_t)((vm.ip[-2] << 8) | vm.ip[-1]))
#define READ_CONSTANT() (vm.chunk->constants.values[READ_BYTE()])
#define BINARY_OP(valueType, valueEnum, op)                                    \
    do {                                                                       \
        Value *b = vm.stackTop - 1;                                            \
        Value *a = vm.stackTop - 2;                                            \
        if (!IS_NUMBER(*a) || !IS_NUMBER(*b)) {                                \
            runtimeError("Operands must be numbers.");                         \
            return INTERPRET_RUNTIME_ERROR;                                    \
        }                                                                      \
        a->as.valueType = (a->as.number)op(b->as.number);                      \
        a->type = (valueEnum);                                                 \
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
        case OP_NIL:
            push(NIL_VAL);
            break;
        case OP_TRUE:
            push(BOOL_VAL(true));
            break;
        case OP_FALSE:
            push(BOOL_VAL(false));
            break;
        case OP_EQUAL: {
            Value *b = vm.stackTop - 1;
            Value *a = vm.stackTop - 2;
            a->as.boolean = valuesEqual(*a, *b);
            a->type = VAL_BOOL;
            vm.stackTop--;
            break;
        }
        case OP_GREATER:
            BINARY_OP(boolean, VAL_BOOL, >);
            break;
        case OP_LESS:
            BINARY_OP(boolean, VAL_BOOL, <);
            break;
        case OP_ADD: {
            if (IS_STRING(peek(0)) && IS_STRING(peek(1))) {
                concatenate();
            } else if (IS_NUMBER(peek(0)) && IS_NUMBER(peek(1))) {
                Value *b = vm.stackTop - 1;
                Value *a = vm.stackTop - 2;
                a->as.number = AS_NUMBER(*a) + AS_NUMBER(*b);
                vm.stackTop--;
            } else {
                runtimeError("Operands must be two numbers or two strings.");
                return INTERPRET_RUNTIME_ERROR;
            }
            break;
        }
        case OP_SUBTRACT:
            BINARY_OP(number, VAL_NUMBER, -);
            break;
        case OP_MULTIPLY:
            BINARY_OP(number, VAL_NUMBER, *);
            break;
        case OP_DIVIDE:
            BINARY_OP(number, VAL_NUMBER, /);
            break;
        case OP_NOT: {
            Value *value = vm.stackTop - 1;
            value->as.boolean = isFalsey(*value);
            value->type = VAL_BOOL;
            break;
        }
        case OP_NEGATE: {
            if (!IS_NUMBER(peek(0))) {
                runtimeError("Operand must be a number.");
                return INTERPRET_RUNTIME_ERROR;
            }
            Value *value = vm.stackTop - 1;
            value->as.number = -value->as.number;
            break;
        }
        case OP_JUMP: {
            uint16_t offset = READ_SHORT();
            vm.ip += offset;
            break;
        }
        case OP_JUMP_IF_FALSE: {
            uint16_t offset = READ_SHORT();
            if (isFalsey(peek(0))) {
                vm.ip += offset;
            }
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
#undef READ_SHORT
#undef READ_CONSTANT
#undef BINARY_OP
}

static void resetStack() { vm.stackTop = vm.stack; }

void initVM() {
    resetStack();
    vm.objects = NULL;
}

void freeVM() {
    freeObjects();
    vm.objects = NULL;
}

InterpretResult interpret(const char *source) {
    Chunk chunk;
    initChunk(&chunk);

    if (!compile(source, &chunk)) {
        freeChunk(&chunk);
        return INTERPRET_COMPILE_ERROR;
    }

    vm.chunk = &chunk;
    vm.ip = vm.chunk->code;

    InterpretResult result = run();

    freeChunk(&chunk);
    return result;
}

void push(Value value) {
    *vm.stackTop = value;
    vm.stackTop++;
}

Value pop() {
    vm.stackTop--;
    return *vm.stackTop;
}

Value peek(int distance) { return vm.stackTop[-1 - distance]; }

bool isFalsey(Value value) {
    return IS_NIL(value) || (IS_BOOL(value) && !AS_BOOL(value));
}

void concatenate() {
    ObjString *b = AS_STRING(pop());
    ObjString *a = AS_STRING(pop());

    int length = a->length + b->length;
    char *chars = ALLOCATE(char, length + 1);
    memcpy(chars, a->chars, a->length);
    memcpy(chars + a->length, b->chars, b->length);
    chars[length] = '\0';

    ObjString *result = takeString(chars, length);
    push(OBJ_VAL(result));
}

void runtimeError(const char *format, ...) {
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fputs("\n", stderr);

    uint32_t instruction = vm.ip - vm.chunk->code - 1;
    uint32_t line = getLine(vm.chunk, instruction);
    fprintf(stderr, "[line %d]\n", line);
    resetStack();
}
