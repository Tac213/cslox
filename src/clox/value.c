#include "value.h"
#include "memory.h"
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
    stringify(value, valueStr, sizeof(valueStr));
    fprintf(stream, "%s", valueStr);
}

void stringify(Value value, char *buffer, size_t size) {
    if (buffer == NULL || size == 0) {
        return;
    }

    switch (value.type) {
    case VAL_BOOL:
        strncpy(buffer, AS_BOOL(value) ? "true" : "false", size - 1);
        break;
    case VAL_NIL:
        strncpy(buffer, "nil", size - 1);
        break;
    case VAL_NUMBER:
        snprintf(buffer, size, "%g", AS_NUMBER(value));
        break;
    default:
        strncpy(buffer, "", size - 1);
        break;
    }
    buffer[size - 1] = '\0';
}
