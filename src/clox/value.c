#include "value.h"
#include "memory.h"
#include "object.h"
#include <string.h>

bool valuesEqual(Value a, Value b) {
    if (a.type != b.type) {
        return false;
    }
    switch (a.type) {
    case VAL_BOOL:
        return AS_BOOL(a) == AS_BOOL(b);
    case VAL_NIL:
        return true;
    case VAL_NUMBER:
        return AS_NUMBER(a) == AS_NUMBER(b);
    case VAL_OBJ: {
        ObjType aType = OBJ_TYPE(a);
        ObjType bType = OBJ_TYPE(b);
        if (aType != bType) {
            return false;
        }
        switch (aType) {
        case OBJ_STRING: {
            ObjString *aString = AS_STRING(a);
            ObjString *bString = AS_STRING(b);
            return (aString->length == bString->length &&
                    memcmp(aString->chars, bString->chars, aString->length) ==
                        0) != 0;
        }
        default:
            return false;
        }
    }
    default:
        return false; // Unreachable.
    }
}

void initValueArray(ValueArray *array) {
    array->values = NULL;
    array->capacity = 0;
    array->count = 0;
}

void writeValueArray(ValueArray *array, Value value) {
    if (array->capacity < array->count + 1) {
        uint32_t oldCapacity = array->capacity;
        array->capacity = GROW_CAPACITY(oldCapacity);
        array->values =
            GROW_ARRAY(Value, array->values, oldCapacity, array->capacity);
    }

    array->values[array->count] = value;
    array->count++;
}

void freeValueArray(ValueArray *array) {
    FREE_ARRAY(Value, array->values, array->capacity);
    initValueArray(array);
}

void printValue(FILE *stream, Value value) {
    char valueStr[32];
    stringify(&value, valueStr, sizeof(valueStr));
    fprintf(stream, "%s", valueStr);
}

void stringify(const Value *value, char *buffer, size_t size) {
    if (buffer == NULL || size == 0) {
        return;
    }

    switch (value->type) {
    case VAL_BOOL:
        snprintf(buffer, size, "%s", AS_BOOL(*value) ? "True" : "False");
        break;
    case VAL_NIL:
        snprintf(buffer, size, "%s", "nil");
        break;
    case VAL_NUMBER:
        snprintf(buffer, size, "%g", AS_NUMBER(*value));
        break;
    case VAL_OBJ: {
        stringifyObject(value, buffer, size);
        break;
    }
    default:
        snprintf(buffer, size, "%s", "");
        break;
    }
    buffer[size - 1] = '\0';
}

void typeOf(const Value *value, char *buffer, size_t size) {
    if (buffer == NULL || size == 0) {
        return;
    }
    switch (value->type) {
    case VAL_BOOL:
        snprintf(buffer, size, "%s", "bool");
        break;
    case VAL_NIL:
        snprintf(buffer, size, "%s", "nil");
        break;
    case VAL_NUMBER:
        snprintf(buffer, size, "%s", "number");
        break;
    case VAL_OBJ: {
        ObjType objType = OBJ_TYPE(*value);
        switch (objType) {
        case OBJ_BOUND_METHOD:
            snprintf(buffer, size, "%s", "method");
            break;
        case OBJ_CLASS:
            snprintf(buffer, size, "%s", "class");
            break;
        case OBJ_CLOSURE:
            snprintf(buffer, size, "%s", "closure");
            break;
        case OBJ_FUNCTION:
            snprintf(buffer, size, "%s", "function");
            break;
        case OBJ_INSTANCE: {
            ObjInstance *instance = AS_INSTANCE(*value);
            if (instance->klass) {
                snprintf(buffer, size, "%s", instance->klass->name->chars);
            } else {
                snprintf(buffer, size, "%s", "instance");
            }
            break;
        }
        case OBJ_NATIVE:
            snprintf(buffer, size, "%s", "native function");
            break;
        case OBJ_STRING:
            snprintf(buffer, size, "%s", "string");
            break;
        case OBJ_UPVALUE:
            snprintf(buffer, size, "%s", "upvalue");
            break;
        default:
            break;
        }
        break;
    }
    default:
        snprintf(buffer, size, "%s", "object");
        break;
    }
    buffer[size - 1] = '\0';
}
