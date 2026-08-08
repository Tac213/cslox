#ifndef clox_value_h
#define clox_value_h

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#ifdef NAN_BOXING
#include <string.h>
#endif

typedef struct Obj Obj;
typedef struct ObjString ObjString;

#ifdef NAN_BOXING

#define SIGN_BIT ((uint64_t)0x8000000000000000)
#define QNAN ((uint64_t)0x7ffc000000000000) // quiet NaN

#define TAG_UNDEFINED 0 // 00.
#define TAG_NIL 1       // 01.
#define TAG_FALSE 2     // 10.
#define TAG_TRUE 3      // 11.

typedef uint64_t Value;

#define IS_BOOL(value) (((value) | 1) == TRUE_VAL)
#define IS_NIL(value) ((value) == NIL_VAL)
#define IS_NUMBER(value) (((value) & QNAN) != QNAN)
#define IS_OBJ(value) (((value) & (QNAN | SIGN_BIT)) == (QNAN | SIGN_BIT))
#define IS_UNDEFINED(value) ((value) == QNAN)

#define AS_BOOL(value) ((value) == TRUE_VAL)
#define AS_NUMBER(value) valueToNum(value)
#define AS_OBJ(value) asObj(value)

#define BOOL_VAL(b) ((b) ? TRUE_VAL : FALSE_VAL)
#define FALSE_VAL ((Value)(uint64_t)(QNAN | TAG_FALSE))
#define TRUE_VAL ((Value)(uint64_t)(QNAN | TAG_TRUE))
#define NIL_VAL ((Value)(uint64_t)(QNAN | TAG_NIL))
#define NUMBER_VAL(num) numToValue(num)
#define OBJ_VAL(obj) objToValue((Obj *)(obj))
#define UNDEFINED_VAL ((Value)(uint64_t)QNAN)

static inline double valueToNum(Value value) {
    double num;
    memcpy(&num, &value, sizeof(Value));
    return num;
}

static inline Value numToValue(double num) {
    Value value;
    memcpy(&value, &num, sizeof(double));
    return value;
}

// Extract an Obj pointer from a tagged Value (no alias violation)
static inline Obj *asObj(Value value) {
    uint64_t masked = value & ~(SIGN_BIT | QNAN);
    Obj *obj;
    memcpy((void *)&obj, (const void *)&masked, sizeof(Obj *));
    return obj;
}

// Tag an Obj pointer into a Value (no alias violation)
static inline Value objToValue(Obj *obj) {
    uintptr_t addr = (uintptr_t)obj;
    uint64_t masked;
    memcpy((void *)&masked, (const void *)&addr, sizeof(uintptr_t));
    return masked | SIGN_BIT | QNAN;
}

#else

typedef enum {
    VAL_BOOL,
    VAL_NIL,
    VAL_NUMBER,
    VAL_OBJ,
    VAL_UNDEFINED, // Internal use only
} ValueType;

typedef struct {
    ValueType type;
    union {
        bool boolean;
        double number;
        Obj *obj;
    } as;
} Value;

#define IS_BOOL(value) ((value).type == VAL_BOOL)
#define IS_NIL(value) ((value).type == VAL_NIL)
#define IS_NUMBER(value) ((value).type == VAL_NUMBER)
#define IS_OBJ(value) ((value).type == VAL_OBJ)
#define IS_UNDEFINED(value) ((value).type == VAL_UNDEFINED)

#define AS_BOOL(value) ((value).as.boolean)
#define AS_NUMBER(value) ((value).as.number)
#define AS_OBJ(value) ((value).as.obj)

#define BOOL_VAL(value) ((Value){VAL_BOOL, {.boolean = (value)}})
#define NIL_VAL ((Value){VAL_NIL, {.number = 0}})
#define NUMBER_VAL(value) ((Value){VAL_NUMBER, {.number = (value)}})
#define OBJ_VAL(object) ((Value){VAL_OBJ, {.obj = (Obj *)(object)}})
#define UNDEFINED_VAL ((Value){VAL_UNDEFINED, {.number = 0}})

#endif

typedef struct {
    uint32_t capacity;
    uint32_t count;
    Value *values;
} ValueArray;

bool valuesEqual(Value a, Value b);
void initValueArray(ValueArray *array);
void writeValueArray(ValueArray *array, Value value);
void freeValueArray(ValueArray *array);
void printValue(FILE *stream, Value value);
void printObject(FILE *stream, Value value);
void stringify(const Value *value, char *buffer, size_t size);
void typeOf(const Value *value, char *buffer, size_t size);

#endif
