#ifndef clox_value_h
#define clox_value_h

#include <stdint.h>
#include <stdio.h>

typedef double Value;

typedef struct {
    uint32_t capacity;
    uint32_t count;
    Value *values;
} ValueArray;

void initValueArray(ValueArray *array);
void writeValueArray(ValueArray *array, Value value);
void freeValueArray(ValueArray *array);
void printValue(FILE *stream, Value value);

#endif
