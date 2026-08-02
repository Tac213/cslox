#include "vm.h"
#include "compiler.h"
#include "memory.h"
#include "object.h"
#ifdef DEBUG_TRACE_EXECUTION
#include "debug.h"
#endif

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
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
static ObjUpvalue *captureUpvalue(Value *local);
static void closeUpvalues(Value *last);
static void defineMethod(ObjString *name);
static void defineClassMethod(ObjString *name);
static void defineProperty(ObjString *name, uint8_t accessorFlag);
static bool bindMethod(ObjClass *klass, ObjString *name);
static bool getProperty(ObjInstance *instance, ObjString *name, Value *value);
static bool setProperty(ObjInstance *instance, ObjString *name, Value value);
static bool invoke(ObjString *name, int argCount);
static bool invokeFromClass(ObjClass *klass, ObjString *name, int argCount);
static void defineNative(const char *name, uint8_t arity, NativeFn function);
static Value clockNative(int argCount, Value *args);
static Value typeofNative(int argCount, Value *args);
static Value stringifyNative(int argCount, Value *args);
static Value stringStartsWithNative(int argCount, Value *args);
static Value stringEndsWithNative(int argCount, Value *args);
static Value hasattrNative(int argCount, Value *args);
static Value getattrNative(int argCount, Value *args);
static Value setattrNative(int argCount, Value *args);
static Value delattrNative(int argCount, Value *args);

typedef enum {
    RUN_UNTIL_END, // Run until top-level script returns (vm.frameCount == 0).
    RUN_ONE_FRAME, // Return control after the current function's OP_RETURN.
} RunMode;

VM vm;
static bool hadRuntimeError = false;

static InterpretResult run(RunMode mode) {
    CallFrame *frame = &vm.frames[vm.frameCount - 1];

#define READ_BYTE() (*frame->ip++)
#define READ_SHORT()                                                           \
    (frame->ip += 2, (uint16_t)((frame->ip[-2] << 8) | frame->ip[-1]))
#define READ_CONSTANT()                                                        \
    (frame->closure->function->chunk.constants.values[READ_BYTE()])
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
            &frame->closure->function->chunk,
            (uint32_t)(frame->ip - frame->closure->function->chunk.code));
#endif

        uint8_t instruction;
        switch (instruction = READ_BYTE()) {
        case OP_CONSTANT: {
            Value constant = READ_CONSTANT();
            push(constant);
            break;
        }
        case OP_CONSTANT_LONG: {
            uint32_t byte1 = READ_BYTE();
            uint32_t byte2 = READ_BYTE();
            uint32_t byte3 = READ_BYTE();
            uint32_t byte4 = READ_BYTE();
            uint32_t constantIndex =
                (byte1 << 24) | (byte2 << 16) | (byte3 << 8) | byte4;
            Value constant =
                frame->closure->function->chunk.constants.values[constantIndex];
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
            uint32_t byte1 = READ_BYTE();
            uint32_t byte2 = READ_BYTE();
            uint32_t byte3 = READ_BYTE();
            uint32_t byte4 = READ_BYTE();
            uint32_t constantIndex =
                (byte1 << 24) | (byte2 << 16) | (byte3 << 8) | byte4;
            ObjString *name = AS_STRING(frame->closure->function->chunk
                                            .constants.values[constantIndex]);
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
            uint32_t byte1 = READ_BYTE();
            uint32_t byte2 = READ_BYTE();
            uint32_t byte3 = READ_BYTE();
            uint32_t byte4 = READ_BYTE();
            uint32_t constantIndex =
                (byte1 << 24) | (byte2 << 16) | (byte3 << 8) | byte4;
            ObjString *name = AS_STRING(frame->closure->function->chunk
                                            .constants.values[constantIndex]);
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
            uint32_t byte1 = READ_BYTE();
            uint32_t byte2 = READ_BYTE();
            uint32_t byte3 = READ_BYTE();
            uint32_t byte4 = READ_BYTE();
            uint32_t constantIndex =
                (byte1 << 24) | (byte2 << 16) | (byte3 << 8) | byte4;
            ObjString *name = AS_STRING(frame->closure->function->chunk
                                            .constants.values[constantIndex]);
            if (tableSet(&vm.globals, name, peek(0))) {
                tableDelete(&vm.globals, name);
                runtimeError("Undefined variable '%s'.", name->chars);
                return INTERPRET_RUNTIME_ERROR;
            }
            break;
        }
        case OP_GET_UPVALUE: {
            uint8_t slot = READ_BYTE();
            push(*frame->closure->upvalues[slot]->location);
            break;
        }
        case OP_SET_UPVALUE: {
            uint8_t slot = READ_BYTE();
            *frame->closure->upvalues[slot]->location = peek(0);
            break;
        }
        case OP_GET_PROPERTY: {
            if (!IS_INSTANCE(peek(0))) {
                runtimeError("Only instances have properties.");
                return INTERPRET_RUNTIME_ERROR;
            }

            ObjInstance *instance = AS_INSTANCE(peek(0));
            ObjString *name = READ_STRING();

            Value value;
            if (tableGet(&instance->fields, name, &value)) {
                pop(); // Instance.
                push(value);
                break;
            }

            if (getProperty(instance, name, &value)) {
                pop(); // Instance.
                push(value);
                break;
            }

            if (hadRuntimeError) {
                return INTERPRET_RUNTIME_ERROR;
            }

            if (!bindMethod(instance->klass, name)) {
                return INTERPRET_RUNTIME_ERROR;
            }
            break;
        }
        case OP_GET_PROPERTY_LONG: {
            if (!IS_INSTANCE(peek(0))) {
                runtimeError("Only instances have properties.");
                return INTERPRET_RUNTIME_ERROR;
            }

            ObjInstance *instance = AS_INSTANCE(peek(0));
            uint32_t byte1 = READ_BYTE();
            uint32_t byte2 = READ_BYTE();
            uint32_t byte3 = READ_BYTE();
            uint32_t byte4 = READ_BYTE();
            uint32_t constantIndex =
                (byte1 << 24) | (byte2 << 16) | (byte3 << 8) | byte4;
            ObjString *name = AS_STRING(frame->closure->function->chunk
                                            .constants.values[constantIndex]);

            Value value;
            if (tableGet(&instance->fields, name, &value)) {
                pop(); // Instance.
                push(value);
                break;
            }

            if (getProperty(instance, name, &value)) {
                pop(); // Instance.
                push(value);
                break;
            }

            if (hadRuntimeError) {
                return INTERPRET_RUNTIME_ERROR;
            }

            if (!bindMethod(instance->klass, name)) {
                return INTERPRET_RUNTIME_ERROR;
            }
            break;
        }
        case OP_SET_PROPERTY: {
            if (!IS_INSTANCE(peek(1))) {
                runtimeError("Only instances have fields.");
                return INTERPRET_RUNTIME_ERROR;
            }

            ObjInstance *instance = AS_INSTANCE(peek(1));
            ObjString *name = READ_STRING();
            // Peek to prevent GC.
            Value v = peek(0);
            if (!setProperty(instance, name, v)) {
                if (hadRuntimeError) {
                    return INTERPRET_RUNTIME_ERROR;
                }
                tableSet(&instance->fields, name, v);
            }
            Value value = pop();
            pop(); // Instance.
            push(value);
            break;
        }
        case OP_SET_PROPERTY_LONG: {
            if (!IS_INSTANCE(peek(1))) {
                runtimeError("Only instances have fields.");
                return INTERPRET_RUNTIME_ERROR;
            }

            ObjInstance *instance = AS_INSTANCE(peek(1));
            uint32_t byte1 = READ_BYTE();
            uint32_t byte2 = READ_BYTE();
            uint32_t byte3 = READ_BYTE();
            uint32_t byte4 = READ_BYTE();
            uint32_t constantIndex =
                (byte1 << 24) | (byte2 << 16) | (byte3 << 8) | byte4;
            ObjString *name = AS_STRING(frame->closure->function->chunk
                                            .constants.values[constantIndex]);
            // Peek to prevent GC.
            Value v = peek(0);
            if (!setProperty(instance, name, v)) {
                if (hadRuntimeError) {
                    return INTERPRET_RUNTIME_ERROR;
                }
                tableSet(&instance->fields, name, v);
            }
            Value value = pop();
            pop(); // Instance.
            push(value);
            break;
        }
        case OP_GET_SUPER: {
            ObjString *name = READ_STRING();
            ObjClass *superclass = AS_CLASS(pop());

            if (!bindMethod(superclass, name)) {
                return INTERPRET_RUNTIME_ERROR;
            }
            break;
        }
        case OP_GET_SUPER_LONG: {
            uint32_t byte1 = READ_BYTE();
            uint32_t byte2 = READ_BYTE();
            uint32_t byte3 = READ_BYTE();
            uint32_t byte4 = READ_BYTE();
            uint32_t constantIndex =
                (byte1 << 24) | (byte2 << 16) | (byte3 << 8) | byte4;
            ObjString *name = AS_STRING(frame->closure->function->chunk
                                            .constants.values[constantIndex]);

            ObjClass *superclass = AS_CLASS(pop());

            if (!bindMethod(superclass, name)) {
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
                *a = OBJ_VAL(concatenateString(AS_STRING(*a), AS_STRING(*b)));
                vm.stackTop -= 1;
            } else if (IS_NUMBER(*a) && IS_STRING(*b)) {
                *a = OBJ_VAL(
                    concatenateNumberString(AS_NUMBER(*a), AS_STRING(*b)));
                vm.stackTop -= 1;
            } else if (IS_STRING(*a) && IS_NUMBER(*b)) {
                *a = OBJ_VAL(
                    concatenateStringNumber(AS_STRING(*a), AS_NUMBER(*b)));
                vm.stackTop -= 1;
            } else if (IS_NUMBER(*a) && IS_NUMBER(*b)) {
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
                *a = OBJ_VAL(
                    repeatString(AS_STRING(*b), (uint32_t)(AS_NUMBER(*a))));
                vm.stackTop -= 1;
            } else if (IS_STRING(*a) && IS_POSITIVE_INTEGER(*b)) {
                *a = OBJ_VAL(
                    repeatString(AS_STRING(*a), (uint32_t)(AS_NUMBER(*b))));
                vm.stackTop -= 1;
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
        case OP_INVOKE: {
            ObjString *method = READ_STRING();
            int argCount = READ_BYTE();
            if (!invoke(method, argCount)) {
                return INTERPRET_RUNTIME_ERROR;
            }
            frame = &vm.frames[vm.frameCount - 1];
            break;
        }
        case OP_INVOKE_LONG: {
            uint32_t byte1 = READ_BYTE();
            uint32_t byte2 = READ_BYTE();
            uint32_t byte3 = READ_BYTE();
            uint32_t byte4 = READ_BYTE();
            uint32_t constantIndex =
                (byte1 << 24) | (byte2 << 16) | (byte3 << 8) | byte4;
            ObjString *method = AS_STRING(frame->closure->function->chunk
                                              .constants.values[constantIndex]);
            int argCount = READ_BYTE();
            if (!invoke(method, argCount)) {
                return INTERPRET_RUNTIME_ERROR;
            }
            frame = &vm.frames[vm.frameCount - 1];
            break;
        }
        case OP_SUPER_INVOKE: {
            ObjString *method = READ_STRING();
            int argCount = READ_BYTE();
            ObjClass *superclass = AS_CLASS(pop());
            if (!invokeFromClass(superclass, method, argCount)) {
                return INTERPRET_RUNTIME_ERROR;
            }
            frame = &vm.frames[vm.frameCount - 1];
            break;
        }
        case OP_SUPER_INVOKE_LONG: {
            uint32_t byte1 = READ_BYTE();
            uint32_t byte2 = READ_BYTE();
            uint32_t byte3 = READ_BYTE();
            uint32_t byte4 = READ_BYTE();
            uint32_t constantIndex =
                (byte1 << 24) | (byte2 << 16) | (byte3 << 8) | byte4;
            ObjString *method = AS_STRING(frame->closure->function->chunk
                                              .constants.values[constantIndex]);
            int argCount = READ_BYTE();
            ObjClass *superclass = AS_CLASS(pop());
            if (!invokeFromClass(superclass, method, argCount)) {
                return INTERPRET_RUNTIME_ERROR;
            }
            frame = &vm.frames[vm.frameCount - 1];
            break;
        }
        case OP_CLOSURE: {
            ObjFunction *function = AS_FUNCTION(READ_CONSTANT());
            ObjClosure *closure = newClosure(function);
            push(OBJ_VAL(closure));
            for (uint32_t i = 0; i < closure->upvalueCount; i++) {
                uint8_t isLocal = READ_BYTE();
                uint8_t index = READ_BYTE();
                if (isLocal) {
                    closure->upvalues[i] = captureUpvalue(frame->slots + index);
                } else {
                    closure->upvalues[i] = frame->closure->upvalues[index];
                }
            }
            break;
        }
        case OP_CLOSURE_LONG: {
            uint32_t byte1 = READ_BYTE();
            uint32_t byte2 = READ_BYTE();
            uint32_t byte3 = READ_BYTE();
            uint32_t byte4 = READ_BYTE();
            uint32_t constantIndex =
                (byte1 << 24) | (byte2 << 16) | (byte3 << 8) | byte4;
            ObjFunction *function =
                AS_FUNCTION(frame->closure->function->chunk.constants
                                .values[constantIndex]);
            ObjClosure *closure = newClosure(function);
            push(OBJ_VAL(closure));
            break;
        }
        case OP_CLOSE_UPVALUE:
            closeUpvalues(vm.stackTop - 1);
            pop();
            break;
        case OP_RETURN: {
            Value result = pop();
            closeUpvalues(frame->slots);
            vm.frameCount--;
            if (vm.frameCount == 0) {
                // Pop the <script> closure
                // pushed in `interpret`.
                pop();
                // Exit interpreter.
                return INTERPRET_OK;
            }
            vm.stackTop = frame->slots;
            push(result);
            frame = &vm.frames[vm.frameCount - 1];
            if (mode == RUN_ONE_FRAME) {
                return INTERPRET_OK;
            }
            break;
        }
        case OP_CLASS:
            push(OBJ_VAL(newClass(READ_STRING())));
            break;
        case OP_CLASS_LONG: {
            uint32_t byte1 = READ_BYTE();
            uint32_t byte2 = READ_BYTE();
            uint32_t byte3 = READ_BYTE();
            uint32_t byte4 = READ_BYTE();
            uint32_t constantIndex =
                (byte1 << 24) | (byte2 << 16) | (byte3 << 8) | byte4;
            ObjString *name = AS_STRING(frame->closure->function->chunk
                                            .constants.values[constantIndex]);
            push(OBJ_VAL(newClass(name)));
            break;
        }
        case OP_INHERIT: {
            Value superclassValue = peek(1);
            if (!IS_CLASS(superclassValue)) {
                runtimeError("Superclass must be a class.");
                return INTERPRET_RUNTIME_ERROR;
            }

            ObjClass *subclass = AS_CLASS(peek(0));
            ObjClass *superclass = AS_CLASS(superclassValue);
            tableAddAll(&superclass->methods, &subclass->methods);
            tableAddAll(&superclass->properties, &subclass->properties);
            pop(); // Subclass.
            break;
        }
        case OP_METHOD:
            defineMethod(READ_STRING());
            break;
        case OP_METHOD_LONG: {
            uint32_t byte1 = READ_BYTE();
            uint32_t byte2 = READ_BYTE();
            uint32_t byte3 = READ_BYTE();
            uint32_t byte4 = READ_BYTE();
            uint32_t constantIndex =
                (byte1 << 24) | (byte2 << 16) | (byte3 << 8) | byte4;
            ObjString *name = AS_STRING(frame->closure->function->chunk
                                            .constants.values[constantIndex]);
            defineMethod(name);
            break;
        }
        case OP_CLASS_METHOD:
            defineClassMethod(READ_STRING());
            break;
        case OP_CLASS_METHOD_LONG: {
            uint32_t byte1 = READ_BYTE();
            uint32_t byte2 = READ_BYTE();
            uint32_t byte3 = READ_BYTE();
            uint32_t byte4 = READ_BYTE();
            uint32_t constantIndex =
                (byte1 << 24) | (byte2 << 16) | (byte3 << 8) | byte4;
            ObjString *name = AS_STRING(frame->closure->function->chunk
                                            .constants.values[constantIndex]);
            defineClassMethod(name);
            break;
        }
        case OP_PROPERTY: {
            ObjString *name = READ_STRING();
            uint8_t accessorFlag = READ_BYTE();
            defineProperty(name, accessorFlag);
            break;
        }
        case OP_PROPERTY_LONG: {
            uint32_t byte1 = READ_BYTE();
            uint32_t byte2 = READ_BYTE();
            uint32_t byte3 = READ_BYTE();
            uint32_t byte4 = READ_BYTE();
            uint32_t constantIndex =
                (byte1 << 24) | (byte2 << 16) | (byte3 << 8) | byte4;
            ObjString *name = AS_STRING(frame->closure->function->chunk
                                            .constants.values[constantIndex]);
            defineProperty(name, READ_BYTE());
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

static bool call(ObjClosure *closure, uint8_t argCount) {
    if (argCount != closure->function->arity) {
        runtimeError("Expected %d %s but got %d.", closure->function->arity,
                     closure->function->arity <= 1 ? "argument" : "arguments",
                     argCount);
        return false;
    }

    if (vm.frameCount == FRAMES_MAX) {
        runtimeError("Stack overflow.");
        return false;
    }

    CallFrame *frame = &vm.frames[vm.frameCount++];
    frame->closure = closure;
    frame->ip = closure->function->chunk.code;
    frame->slots = vm.stackTop - argCount - 1;
    return true;
}

static void resetStack() {
    vm.stackTop = vm.stack;
    vm.frameCount = 0;
    vm.openUpvalues = NULL;
}

void initVM() {
    resetStack();
    initTable(&vm.globals);
    initTable(&vm.strings);

    vm.objects = NULL;
    vm.bytesAllocated = 0;
    vm.nextGC = (size_t)(1024 * 1024);

    vm.grayCount = 0;
    vm.grayCapacity = 0;
    vm.grayStack = NULL;

    vm.type = NULL;
    vm.type = newClass(copyString("type", 4));

    vm.initString = NULL;
    vm.initString = copyString("init", 4);

    defineNative("clock", 0, clockNative);
    defineNative("typeof", 1, typeofNative);
    defineNative("stringify", 1, stringifyNative);
    defineNative("startswith", 2, stringStartsWithNative);
    defineNative("endswith", 2, stringEndsWithNative);
    defineNative("hasattr", 2, hasattrNative);
    defineNative("getattr", 3, getattrNative);
    defineNative("setattr", 3, setattrNative);
    defineNative("delattr", 2, delattrNative);
}

void freeVM() {
    freeTable(&vm.globals);
    freeTable(&vm.strings);

    vm.initString = NULL;
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
    ObjClosure *closure = newClosure(function);
    pop();
    push(OBJ_VAL(closure));
    call(closure, 0);

    InterpretResult result = run(RUN_UNTIL_END);

    if (replValue != NULL && vm.stackTop > vm.stack) {
        *replValue = *vm.stackTop;
        // Pop the <script> closure pushed above.
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
        case OBJ_BOUND_METHOD: {
            ObjBoundMethod *bound = AS_BOUND_METHOD(callee);
            vm.stackTop[-argCount - 1] = bound->receiver; // `this`
            return call(bound->method, argCount);
        }
        case OBJ_CLASS: {
            ObjClass *klass = AS_CLASS(callee);
            vm.stackTop[-argCount - 1] = OBJ_VAL(newInstance(klass)); // `this`
            Value initializer;
            if (tableGet(&klass->methods, vm.initString, &initializer)) {
                return call(AS_CLOSURE(initializer), argCount);
            }
            if (argCount != 0) {
                runtimeError("Expected 0 arguments but got %d.", argCount);
                return false;
            }
            return true;
        }
        case OBJ_CLOSURE:
            return call(AS_CLOSURE(callee), argCount);
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
        ObjFunction *function = frame->closure->function;
        uint32_t instruction = frame->ip - function->chunk.code - 1;
        uint32_t line = getLine(&function->chunk, instruction);
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

ObjUpvalue *captureUpvalue(Value *local) {
    ObjUpvalue *prevUpvalue = NULL;
    ObjUpvalue *upvalue = vm.openUpvalues;
    while (upvalue != NULL && upvalue->location > local) {
        prevUpvalue = upvalue;
        upvalue = upvalue->next;
    }

    if (upvalue != NULL && upvalue->location == local) {
        return upvalue;
    }

    ObjUpvalue *createdUpvalue = newUpvalue(local);
    createdUpvalue->next = upvalue;

    if (prevUpvalue == NULL) {
        vm.openUpvalues = createdUpvalue;
    } else {
        prevUpvalue->next = createdUpvalue;
    }
    return createdUpvalue;
}

void closeUpvalues(Value *last) {
    while (vm.openUpvalues != NULL && vm.openUpvalues->location >= last) {
        ObjUpvalue *upvalue = vm.openUpvalues;
        upvalue->closed = *upvalue->location;
        upvalue->location = &upvalue->closed;
        vm.openUpvalues = upvalue->next;
    }
}

void defineMethod(ObjString *name) {
    Value method = peek(0);
    ObjClass *klass = AS_CLASS(peek(1));
    tableSet(&klass->methods, name, method);
    pop();
}

void defineClassMethod(ObjString *name) {
    Value method = peek(0);
    Value receiver = peek(1);
    ObjInstance *instance = AS_INSTANCE(receiver);
    ObjBoundMethod *bound = newBoundMethod(receiver, AS_CLOSURE(method));
    tableSet(&instance->fields, name, OBJ_VAL(bound));
    pop();
}

void defineProperty(ObjString *name, uint8_t accessorFlag) {
    uint8_t hasGetter = (accessorFlag >> 2) & 1;
    uint8_t hasSetter = (accessorFlag >> 1) & 1;
    uint8_t isGetterFirst = accessorFlag & 1;

    ObjClosure *getter = NULL;
    ObjClosure *setter = NULL;
    ObjClass *klass = NULL;
    uint8_t accessorCount = 0;
    if (hasGetter) {
        if (hasSetter) {
            if (isGetterFirst) {
                setter = AS_CLOSURE(peek(0));
                getter = AS_CLOSURE(peek(1));
            } else {
                setter = AS_CLOSURE(peek(1));
                getter = AS_CLOSURE(peek(0));
            }
            klass = AS_CLASS(peek(2));
            accessorCount = 2;
        } else {
            getter = AS_CLOSURE(peek(0));
            klass = AS_CLASS(peek(1));
            accessorCount = 1;
        }
    }
    if (!setter && hasSetter) {
        setter = AS_CLOSURE(peek(0));
        klass = AS_CLASS(peek(1));
        accessorCount = 1;
    }

    ObjProperty *property = newProperty(getter, setter);
    tableSet(&klass->properties, name, OBJ_VAL(property));
    vm.stackTop -= accessorCount;
}

bool bindMethod(ObjClass *klass, ObjString *name) {
    Value method;
    if (!tableGet(&klass->methods, name, &method)) {
        runtimeError("%s instance has no attribute '%s'.", klass->name->chars,
                     name->chars);
        return false;
    }

    ObjBoundMethod *bound = newBoundMethod(peek(0), AS_CLOSURE(method));
    pop();
    push(OBJ_VAL(bound));
    return true;
}

bool getProperty(ObjInstance *instance, ObjString *name, Value *value) {
    Value propertyValue;
    if (!tableGet(&instance->klass->properties, name, &propertyValue)) {
        return false;
    }
    ObjProperty *property = AS_PROPERTY(propertyValue);
    ObjClosure *getter = property->getter;
    if (!getter) {
        runtimeError("Property '%s' has no getter.", name->chars);
        return false;
    }
    push(OBJ_VAL(instance));
    if (!call(getter, 0)) {
        return false;
    }
    InterpretResult result = run(RUN_ONE_FRAME);
    if (result != INTERPRET_OK) {
        return false;
    }
    *value = pop();
    return true;
}

bool setProperty(ObjInstance *instance, ObjString *name, Value value) {
    Value propertyValue;
    if (!tableGet(&instance->klass->properties, name, &propertyValue)) {
        return false;
    }
    ObjProperty *property = AS_PROPERTY(propertyValue);
    ObjClosure *setter = property->setter;
    if (!setter) {
        runtimeError("Property '%s' has no setter.", name->chars);
        return false;
    }
    push(OBJ_VAL(instance));
    push(value);
    if (!call(setter, 1)) {
        return false;
    }
    InterpretResult result = run(RUN_ONE_FRAME);
    if (result != INTERPRET_OK) {
        return false;
    }
    pop(); // return value of the property setter.
    return true;
}

bool invoke(ObjString *name, int argCount) {
    Value receiver = peek(argCount);

    if (!IS_INSTANCE(receiver)) {
        runtimeError("Only instances have methods.");
        return false;
    }

    ObjInstance *instance = AS_INSTANCE(receiver);

    Value value;
    if (tableGet(&instance->fields, name, &value)) {
        vm.stackTop[-argCount - 1] = value;
        return callValue(value, argCount);
    }

    if (getProperty(instance, name, &value)) {
        // Call the property getter to get the property value first.
        // Then do the same thing as `instance->fields`.
        vm.stackTop[-argCount - 1] = value;
        return callValue(value, argCount);
    }

    if (hadRuntimeError) {
        return false;
    }

    return invokeFromClass(instance->klass, name, argCount);
}

bool invokeFromClass(ObjClass *klass, ObjString *name, int argCount) {
    Value method;
    if (!tableGet(&klass->methods, name, &method)) {
        runtimeError("%s instance has no attribute '%s'.", klass->name->chars,
                     name->chars);
        return false;
    }
    return call(AS_CLOSURE(method), argCount);
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

Value hasattrNative(int argCount, Value *args) {
    Value *instanceObj = &args[0];
    Value *nameObj = &args[1];
    if (!IS_INSTANCE(*instanceObj)) {
        char typeOfArg[128];
        typeOf(instanceObj, typeOfArg, sizeof(typeOfArg));
        runtimeError(
            "Argument 1 has incorrect type, expected instance, got '%s'.",
            typeOfArg);
        return UNDEFINED_VAL;
    }
    if (!IS_STRING(*nameObj)) {
        char typeOfArg[128];
        typeOf(nameObj, typeOfArg, sizeof(typeOfArg));
        runtimeError(
            "Argument 2 has incorrect type, expected 'string', got '%s'.",
            typeOfArg);
        return UNDEFINED_VAL;
    }

    ObjInstance *instance = AS_INSTANCE(*instanceObj);
    ObjString *name = AS_STRING(*nameObj);

    Value value;
    if (tableGet(&instance->fields, name, &value)) {
        return BOOL_VAL(true);
    }
    return BOOL_VAL(false);
}

Value getattrNative(int argCount, Value *args) {
    Value *instanceObj = &args[0];
    Value *nameObj = &args[1];
    Value *defaultObj = &args[2];
    if (!IS_INSTANCE(*instanceObj)) {
        char typeOfArg[128];
        typeOf(instanceObj, typeOfArg, sizeof(typeOfArg));
        runtimeError(
            "Argument 1 has incorrect type, expected instance, got '%s'.",
            typeOfArg);
        return UNDEFINED_VAL;
    }
    if (!IS_STRING(*nameObj)) {
        char typeOfArg[128];
        typeOf(nameObj, typeOfArg, sizeof(typeOfArg));
        runtimeError(
            "Argument 2 has incorrect type, expected 'string', got '%s'.",
            typeOfArg);
        return UNDEFINED_VAL;
    }

    ObjInstance *instance = AS_INSTANCE(*instanceObj);
    ObjString *name = AS_STRING(*nameObj);

    Value value;
    if (tableGet(&instance->fields, name, &value)) {
        return value;
    }
    return *defaultObj;
}

Value setattrNative(int argCount, Value *args) {
    Value *instanceObj = &args[0];
    Value *nameObj = &args[1];
    Value *value = &args[2];
    if (!IS_INSTANCE(*instanceObj)) {
        char typeOfArg[128];
        typeOf(instanceObj, typeOfArg, sizeof(typeOfArg));
        runtimeError(
            "Argument 1 has incorrect type, expected instance, got '%s'.",
            typeOfArg);
        return UNDEFINED_VAL;
    }
    if (!IS_STRING(*nameObj)) {
        char typeOfArg[128];
        typeOf(nameObj, typeOfArg, sizeof(typeOfArg));
        runtimeError(
            "Argument 2 has incorrect type, expected 'string', got '%s'.",
            typeOfArg);
        return UNDEFINED_VAL;
    }

    ObjInstance *instance = AS_INSTANCE(*instanceObj);
    ObjString *name = AS_STRING(*nameObj);

    tableSet(&instance->fields, name, *value);
    return NIL_VAL;
}

Value delattrNative(int argCount, Value *args) {
    Value *instanceObj = &args[0];
    Value *nameObj = &args[1];
    if (!IS_INSTANCE(*instanceObj)) {
        char typeOfArg[128];
        typeOf(instanceObj, typeOfArg, sizeof(typeOfArg));
        runtimeError(
            "Argument 1 has incorrect type, expected instance, got '%s'.",
            typeOfArg);
        return UNDEFINED_VAL;
    }
    if (!IS_STRING(*nameObj)) {
        char typeOfArg[128];
        typeOf(nameObj, typeOfArg, sizeof(typeOfArg));
        runtimeError(
            "Argument 2 has incorrect type, expected 'string', got '%s'.",
            typeOfArg);
        return UNDEFINED_VAL;
    }

    ObjInstance *instance = AS_INSTANCE(*instanceObj);
    ObjString *name = AS_STRING(*nameObj);

    if (!tableDelete(&instance->fields, name)) {
        runtimeError("%s instance has no attribute '%s'.",
                     instance->klass->name->chars, name->chars);
        return UNDEFINED_VAL;
    }
    return NIL_VAL;
}
