#include "vm.h"
#include "compiler.h"
#include "memory.h"
#include "object.h"
#ifdef DEBUG_TRACE_EXECUTION
#include "debug.h"
#endif

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

// Forward declaration
static Value peek(int distance);
static bool isFalsey(Value value);
static void runtimeError(const char *format, ...);

VM vm;

static InterpretResult run() {
#define READ_BYTE() (*vm.ip++)
#define READ_SHORT() (vm.ip += 2, (uint16_t)((vm.ip[-2] << 8) | vm.ip[-1]))
#define READ_CONSTANT() (vm.chunk->constants.values[READ_BYTE()])
#define READ_STRING() AS_STRING(READ_CONSTANT())
#define BINARY_ERROR(op)                                                       \
    do {                                                                       \
        char typeOfA[128];                                                     \
        char typeOfB[128];                                                     \
        typeOf(a, typeOfA, sizeof(typeOfA));                                   \
        typeOf(b, typeOfB, sizeof(typeOfB));                                   \
        runtimeError("'" #op "' not supported between '%s' and '%s'.",         \
                     typeOfA, typeOfB);                                        \
        return INTERPRET_COMPILE_ERROR;                                        \
    } while (false)
#define BINARY_CMP_OP(op)                                                      \
    do {                                                                       \
        Value *b = vm.stackTop - 1;                                            \
        Value *a = vm.stackTop - 2;                                            \
        if (IS_NUMBER(*a) && IS_NUMBER(*b)) {                                  \
            a->as.boolean = (a->as.number)op(b->as.number);                    \
            a->type = VAL_BOOL;                                                \
            vm.stackTop--;                                                     \
        } else if (IS_STRING(*a) && IS_STRING(*b)) {                           \
            a->as.boolean =                                                    \
                (compareString(AS_STRING(*a), AS_STRING(*b)) op(0));           \
            a->type = VAL_BOOL;                                                \
            vm.stackTop--;                                                     \
        } else {                                                               \
            BINARY_ERROR(op);                                                  \
        }                                                                      \
    } while (false)
#define IS_POSITIVE_INTEGER(value)                                             \
    (IS_NUMBER(value) && AS_NUMBER(value) >= 0 &&                              \
     AS_NUMBER(value) <= UINT32_MAX &&                                         \
     AS_NUMBER(value) == (double)(uint32_t)(AS_NUMBER(value)))

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
                ((uint32_t)READ_BYTE() << 24) | ((uint32_t)READ_BYTE() << 16) |
                ((uint32_t)READ_BYTE() << 8) | (uint32_t)READ_BYTE();
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
        case OP_POP:
            pop();
            break;
        case OP_GET_GLOBAL: {
            ObjString *name = READ_STRING();
            Value value;
            if (!tableGet(&vm.globals, name, &value)) {
                runtimeError("Undefined variable '%s'.", name->chars);
                return INTERPRET_RUNTIME_ERROR;
            }
            push(value);
            break;
        }
        case OP_DEFINE_GLOBAL: {
            ObjString *name = READ_STRING();
            tableSet(&vm.globals, name, peek(0));
            pop();
            break;
        }
        case OP_SET_GLOBAL: {
            ObjString *name = READ_STRING();
            if (tableSet(&vm.globals, name, peek(0))) {
                tableDelete(&vm.globals, name);
                runtimeError("Undefined variable '%s'.", name->chars);
                return INTERPRET_RUNTIME_ERROR;
            }
            break;
        }
        case OP_EQUAL: {
            Value *b = vm.stackTop - 1;
            Value *a = vm.stackTop - 2;
            a->as.boolean = valuesEqual(*a, *b);
            a->type = VAL_BOOL;
            vm.stackTop--;
            break;
        }
        case OP_GREATER:
            BINARY_CMP_OP(>);
            break;
        case OP_GREATER_EQUAL:
            BINARY_CMP_OP(>=);
            break;
        case OP_LESS:
            BINARY_CMP_OP(<);
            break;
        case OP_LESS_EQUAL:
            BINARY_CMP_OP(<=);
            break;
        case OP_ADD: {
            Value *b = vm.stackTop - 1;
            Value *a = vm.stackTop - 2;
            if (IS_STRING(*a) && IS_STRING(*b)) {
                vm.stackTop -= 2;
                push(OBJ_VAL(concatenateString(AS_STRING(*a), AS_STRING(*b))));
            } else if (IS_NUMBER(*a) && IS_STRING(*b)) {
                vm.stackTop -= 2;
                push(OBJ_VAL(
                    concatenateNumberString(AS_NUMBER(*a), AS_STRING(*b))));
            } else if (IS_STRING(*a) && IS_NUMBER(*b)) {
                vm.stackTop -= 2;
                push(OBJ_VAL(
                    concatenateStringNumber(AS_STRING(*a), AS_NUMBER(*b))));
            } else if (IS_NUMBER(*a) && IS_NUMBER(*b)) {
                Value *b = vm.stackTop - 1;
                Value *a = vm.stackTop - 2;
                AS_NUMBER(*a) = AS_NUMBER(*a) + AS_NUMBER(*b);
                vm.stackTop--;
            } else {
                BINARY_ERROR(+);
            }
            break;
        }
        case OP_SUBTRACT: {
            Value *b = vm.stackTop - 1;
            Value *a = vm.stackTop - 2;
            if (IS_NUMBER(*a) && IS_NUMBER(*b)) {
                AS_NUMBER(*a) = AS_NUMBER(*a) - AS_NUMBER(*b);
                vm.stackTop--;
            } else {
                BINARY_ERROR(-);
            }
            break;
        }
        case OP_MULTIPLY: {
            Value *b = vm.stackTop - 1;
            Value *a = vm.stackTop - 2;
            if (IS_NUMBER(*a) && IS_NUMBER(*b)) {
                AS_NUMBER(*a) = AS_NUMBER(*a) * AS_NUMBER(*b);
                vm.stackTop--;
            } else if (IS_POSITIVE_INTEGER(*a) && IS_STRING(*b)) {
                vm.stackTop -= 2;
                push(OBJ_VAL(
                    repeatString(AS_STRING(*b), (uint32_t)(AS_NUMBER(*a)))));
            } else if (IS_STRING(*a) && IS_POSITIVE_INTEGER(*b)) {
                vm.stackTop -= 2;
                push(OBJ_VAL(
                    repeatString(AS_STRING(*a), (uint32_t)(AS_NUMBER(*b)))));
            } else {
                BINARY_ERROR(*);
            }
            break;
        }
        case OP_DIVIDE: {
            Value *b = vm.stackTop - 1;
            Value *a = vm.stackTop - 2;
            if (IS_NUMBER(*a) && IS_NUMBER(*b)) {
                if (AS_NUMBER(*b) == 0.0) {
                    runtimeError("Division by zero.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                AS_NUMBER(*a) = AS_NUMBER(*a) / AS_NUMBER(*b);
                vm.stackTop--;
            } else {
                BINARY_ERROR(/);
            }
            break;
        }
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
        case OP_PRINT: {
            printValue(stdout, pop());
            fprintf(stdout, "\n");
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
            // Exit interpreter.
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
#undef READ_CONSTANT
#undef BINARY_ERROR
#undef BINARY_CMP_OP
#undef IS_POSITIVE_INTEGER
}

static void resetStack() { vm.stackTop = vm.stack; }

void initVM() {
    resetStack();
    initTable(&vm.globals);
    initTable(&vm.strings);
    vm.objects = NULL;
}

void freeVM() {
    freeTable(&vm.globals);
    freeTable(&vm.strings);
    freeObjects();
    vm.objects = NULL;
}

InterpretResult interpret(const char *source, Value *replValue) {
    Chunk chunk;
    initChunk(&chunk);

    bool isREPL = replValue != NULL;
    if (!compile(source, &chunk, isREPL)) {
        freeChunk(&chunk);
        return INTERPRET_COMPILE_ERROR;
    }

    vm.chunk = &chunk;
    vm.ip = vm.chunk->code;

    InterpretResult result = run();

    if (replValue != NULL && vm.stackTop > vm.stack) {
        *replValue = pop();
    }

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
