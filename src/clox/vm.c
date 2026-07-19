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
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/time.h>
#endif

// Forward declaration
static Value peek(int distance);
static bool callValue(Value callee, uint8_t argCount);
static bool isFalsey(Value value);
static void runtimeError(const char *format, ...);
static void defineNative(const char *name, uint8_t arity, NativeFn function);
static Value clockNative(int argCount, Value *args);
static Value typeofNative(int argCount, Value *args);
static Value stringifyNative(int argCount, Value *args);
static Value stringStartsWithNative(int argCount, Value *args);
static Value stringEndsWithNative(int argCount, Value *args);

VM vm;
static bool hadRuntimeError = false;

static InterpretResult run() {
    CallFrame *frame = &vm.frames[vm.frameCount - 1];

#define READ_BYTE() (*frame->ip++)
#define READ_SHORT()                                                           \
    (frame->ip += 2, (uint16_t)((frame->ip[-2] << 8) | frame->ip[-1]))
#define READ_CONSTANT() (frame->function->chunk.constants.values[READ_BYTE()])
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
        disassembleInstruction(
            &frame->function->chunk,
            (uint32_t)(frame->ip - frame->function->chunk.code));
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
            Value constant =
                frame->function->chunk.constants.values[constantIndex];
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
        case OP_UNDEFINED:
            push(UNDEFINED_VAL);
            break;
        case OP_POP:
            pop();
            break;
        case OP_GET_LOCAL: {
            uint8_t slot = READ_BYTE();
            push(frame->slots[slot]);
            break;
        }
        case OP_GET_GLOBAL: {
            ObjString *name = READ_STRING();
            Value value;
            if (!tableGet(&vm.globals, name, &value)) {
                runtimeError("Undefined variable '%s'.", name->chars);
                return INTERPRET_RUNTIME_ERROR;
            }
            if (IS_UNDEFINED(value)) {
                runtimeError("Accessing a variable '%s' that has not been "
                             "initialized or assigned to.",
                             name->chars);
                return INTERPRET_RUNTIME_ERROR;
            }
            push(value);
            break;
        }
        case OP_GET_GLOBAL_LONG: {
            uint32_t constantIndex =
                ((uint32_t)READ_BYTE() << 24) | ((uint32_t)READ_BYTE() << 16) |
                ((uint32_t)READ_BYTE() << 8) | (uint32_t)READ_BYTE();
            ObjString *name = AS_STRING(
                frame->function->chunk.constants.values[constantIndex]);
            Value value;
            if (!tableGet(&vm.globals, name, &value)) {
                runtimeError("Undefined variable '%s'.", name->chars);
                return INTERPRET_RUNTIME_ERROR;
            }
            if (IS_UNDEFINED(value)) {
                runtimeError("Accessing a variable '%s' that has not been "
                             "initialized or assigned to.",
                             name->chars);
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
        case OP_DEFINE_GLOBAL_LONG: {
            uint32_t constantIndex =
                ((uint32_t)READ_BYTE() << 24) | ((uint32_t)READ_BYTE() << 16) |
                ((uint32_t)READ_BYTE() << 8) | (uint32_t)READ_BYTE();
            ObjString *name = AS_STRING(
                frame->function->chunk.constants.values[constantIndex]);
            tableSet(&vm.globals, name, peek(0));
            pop();
            break;
        }
        case OP_SET_LOCAL: {
            uint8_t slot = READ_BYTE();
            frame->slots[slot] = peek(0);
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
        case OP_SET_GLOBAL_LONG: {
            uint32_t constantIndex =
                ((uint32_t)READ_BYTE() << 24) | ((uint32_t)READ_BYTE() << 16) |
                ((uint32_t)READ_BYTE() << 8) | (uint32_t)READ_BYTE();
            ObjString *name = AS_STRING(
                frame->function->chunk.constants.values[constantIndex]);
            if (tableSet(&vm.globals, name, peek(0))) {
                tableDelete(&vm.globals, name);
                runtimeError("Undefined variable '%s'.", name->chars);
                return INTERPRET_RUNTIME_ERROR;
            }
            break;
        }
        case OP_CASE: {
            Value *b = vm.stackTop - 1;
            Value *a = vm.stackTop - 2;
            b->as.boolean = valuesEqual(*a, *b);
            b->type = VAL_BOOL;
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
            frame->ip += offset;
            break;
        }
        case OP_JUMP_IF_FALSE: {
            uint16_t offset = READ_SHORT();
            if (isFalsey(peek(0))) {
                frame->ip += offset;
            }
            break;
        }
        case OP_LOOP: {
            uint16_t offset = READ_SHORT();
            frame->ip -= offset;
            break;
        }
        case OP_CALL: {
            uint8_t argCount = READ_BYTE();
            if (!callValue(peek(argCount), argCount)) {
                return INTERPRET_RUNTIME_ERROR;
            }
            frame = &vm.frames[vm.frameCount - 1];
            break;
        }
        case OP_RETURN: {
            Value result = pop();
            vm.frameCount--;
            if (vm.frameCount == 0) {
                // Pop the <script> function
                // pushed in `interpret`.
                pop();
                // Exit interpreter.
                return INTERPRET_OK;
            }
            vm.stackTop = frame->slots;
            push(result);
            frame = &vm.frames[vm.frameCount - 1];
            break;
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

static bool call(ObjFunction *function, uint8_t argCount) {
    if (argCount != function->arity) {
        runtimeError("Expected %d %s but got %d.", function->arity,
                     function->arity <= 1 ? "argument" : "arguments", argCount);
        return false;
    }

    if (vm.frameCount == FRAMES_MAX) {
        runtimeError("Stack overflow.");
        return false;
    }

    CallFrame *frame = &vm.frames[vm.frameCount++];
    frame->function = function;
    frame->ip = function->chunk.code;
    frame->slots = vm.stackTop - argCount - 1;
    return true;
}

static void resetStack() {
    vm.stackTop = vm.stack;
    vm.frameCount = 0;
}

void initVM() {
    resetStack();
    initTable(&vm.globals);
    initTable(&vm.strings);
    vm.objects = NULL;

    defineNative("clock", 0, clockNative);
    defineNative("typeof", 1, typeofNative);
    defineNative("stringify", 1, stringifyNative);
    defineNative("startswith", 2, stringStartsWithNative);
    defineNative("endswith", 2, stringEndsWithNative);
}

void freeVM() {
    freeTable(&vm.globals);
    freeTable(&vm.strings);
    freeObjects();
    vm.objects = NULL;
}

InterpretResult interpret(const char *source, Value *replValue) {
    hadRuntimeError = false;
    bool isREPL = replValue != NULL;
    ObjFunction *function = compile(source, isREPL);
    if (function == NULL) {
        return INTERPRET_COMPILE_ERROR;
    }

    push(OBJ_VAL(function));
    CallFrame *frame = &vm.frames[vm.frameCount++];
    frame->function = function;
    frame->ip = function->chunk.code;
    frame->slots = vm.stack;

    InterpretResult result = run();

    if (replValue != NULL && vm.stackTop > vm.stack) {
        *replValue = *vm.stackTop;
        // Pop the <script> function pushed above.
        pop();
    }
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

bool callValue(Value callee, uint8_t argCount) {
    if (IS_OBJ(callee)) {
        switch (OBJ_TYPE(callee)) {
        case OBJ_FUNCTION:
            return call(AS_FUNCTION(callee), argCount);
        case OBJ_NATIVE: {
            uint8_t arity = ((ObjNative *)AS_OBJ(callee))->arity;
            if (argCount != arity) {
                runtimeError("Expected %d %s but got %d.", arity,
                             arity <= 1 ? "argument" : "arguments", argCount);
                return false;
            }
            NativeFn native = AS_NATIVE(callee);
            Value result = native(argCount, vm.stackTop - argCount);
            if (hadRuntimeError) {
                return false;
            }
            vm.stackTop -= argCount + 1;
            push(result);
            return true;
        }
        default:
            break; // Non-callable object type.
        }
    }
    char typeOfCallee[128];
    typeOf(&callee, typeOfCallee, sizeof(typeOfCallee));
    runtimeError("'%s' object is not callable.", typeOfCallee);
    return false;
}

bool isFalsey(Value value) {
    return IS_NIL(value) || (IS_BOOL(value) && !AS_BOOL(value));
}

void runtimeError(const char *format, ...) {
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fputs("\n", stderr);

    for (uint32_t i = 0; i < vm.frameCount; i++) {
        CallFrame *frame = &vm.frames[i];
        ObjFunction *function = frame->function;
        uint32_t instruction = frame->ip - frame->function->chunk.code - 1;
        uint32_t line = getLine(&frame->function->chunk, instruction);
        fprintf(stderr, "[line %d] in ", line);
        if (function->name == NULL) {
            fprintf(stderr, "script\n");
        } else {
            fprintf(stderr, "%s()\n", function->name->chars);
        }
    }

    hadRuntimeError = true;
    resetStack();
}

void defineNative(const char *name, uint8_t arity, NativeFn function) {
    ObjString *nameObj = copyString(name, (uint32_t)strlen(name));
    push(OBJ_VAL(nameObj));
    push(OBJ_VAL(newNative(function, arity, nameObj)));
    tableSet(&vm.globals, AS_STRING(vm.stack[0]), vm.stack[1]);
    pop();
    pop();
}

Value clockNative(int argCount, Value *args) {
#ifdef _WIN32
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);

    // Combine the two 32-bit halves into a single 64-bit value
    ULARGE_INTEGER uli;
    uli.LowPart = ft.dwLowDateTime;
    uli.HighPart = ft.dwHighDateTime;

    // FILETIME is 100-nanosecond intervals since Jan 1, 1601.
    // Subtract the offset to Unix epoch (Jan 1, 1970).
    uli.QuadPart -= 116444736000000000ULL;

    // Convert 100-ns intervals → seconds (divide by 10,000,000)
    return NUMBER_VAL((double)uli.QuadPart / 10000000.0);
#else
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return NUMBER_VAL((double)tv.tv_sec + ((double)tv.tv_usec / 1000000.0));
#endif
}

Value typeofNative(int argCount, Value *args) {
    char typeOfArg[128];
    typeOf(&args[0], typeOfArg, sizeof(typeOfArg));
    return OBJ_VAL(copyString(typeOfArg, strlen(typeOfArg)));
}

Value stringifyNative(int argCount, Value *args) {
    char stringified[128];
    stringify(&args[0], stringified, sizeof(stringified));
    return OBJ_VAL(copyString(stringified, strlen(stringified)));
}

Value stringStartsWithNative(int argCount, Value *args) {
    Value *strObj = &args[0];
    Value *prefixObj = &args[1];
    if (!IS_STRING(*strObj)) {
        char typeOfArg[128];
        typeOf(strObj, typeOfArg, sizeof(typeOfArg));
        runtimeError(
            "Argument 1 has incorrect type, expected 'string', got '%s'.",
            typeOfArg);
        return UNDEFINED_VAL;
    }
    if (!IS_STRING(*prefixObj)) {
        char typeOfArg[128];
        typeOf(prefixObj, typeOfArg, sizeof(typeOfArg));
        runtimeError(
            "Argument 2 has incorrect type, expected 'string', got '%s'.",
            typeOfArg);
        return UNDEFINED_VAL;
    }

    const char *str = AS_CSTRING(*strObj);
    const char *prefix = AS_CSTRING(*prefixObj);
    while (*prefix) {
        if (*str == '\0' || *str != *prefix) {
            return BOOL_VAL(false);
        }
        str++;
        prefix++;
    }
    return BOOL_VAL(true);
}

Value stringEndsWithNative(int argCount, Value *args) {
    Value *strObj = &args[0];
    Value *suffixObj = &args[1];
    if (!IS_STRING(*strObj)) {
        char typeOfArg[128];
        typeOf(strObj, typeOfArg, sizeof(typeOfArg));
        runtimeError(
            "Argument 1 has incorrect type, expected 'string', got '%s'.",
            typeOfArg);
        return UNDEFINED_VAL;
    }
    if (!IS_STRING(*suffixObj)) {
        char typeOfArg[128];
        typeOf(suffixObj, typeOfArg, sizeof(typeOfArg));
        runtimeError(
            "Argument 2 has incorrect type, expected 'string', got '%s'.",
            typeOfArg);
        return UNDEFINED_VAL;
    }

    const char *str = AS_CSTRING(*strObj);
    const char *suffix = AS_CSTRING(*suffixObj);

    size_t strLen = strlen(str);
    size_t sufLen = strlen(suffix);

    if (sufLen > strLen) {
        return BOOL_VAL(false);
    }

    // Compare the last `suf_len` characters of `str` with `suffix`
    return BOOL_VAL(strncmp(str + strLen - sufLen, suffix, sufLen) == 0);
}
