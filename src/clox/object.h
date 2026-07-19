#ifndef clox_object_h
#define clox_object_h

#include "chunk.h"
#include "value.h"
#include <stdint.h>

#define OBJ_TYPE(value) (AS_OBJ(value)->type)

#define IS_FUNCTION(value) isObjType(value, OBJ_FUNCTION)
#define IS_NATIVE(value) isObjType(value, OBJ_NATIVE)
#define IS_STRING(value) isObjType(value, OBJ_STRING)

#define AS_FUNCTION(value) ((ObjFunction *)AS_OBJ(value))
#define AS_NATIVE(value) (((ObjNative *)AS_OBJ(value))->function)
#define AS_STRING(value) ((ObjString *)AS_OBJ(value))
#define AS_CSTRING(value) (((ObjString *)AS_OBJ(value))->chars)

typedef enum {
    OBJ_FUNCTION,
    OBJ_NATIVE,
    OBJ_STRING,
} ObjType;

struct Obj {
    ObjType type;
    struct Obj *next;
};

typedef struct {
    Obj obj;
    uint32_t arity;
    Chunk chunk;
    ObjString *name;
} ObjFunction;

typedef Value (*NativeFn)(int argCount, Value *args);

typedef struct {
    Obj obj;
    NativeFn function;
    uint8_t arity;
    ObjString *name;
} ObjNative;

struct ObjString {
    Obj obj;
    uint32_t length;
    uint32_t hash;
    char chars[]; // flexible array member
};

void stringifyObject(const Value *value, char *buffer, size_t size);

ObjFunction *newFunction();
ObjNative *newNative(NativeFn function, uint8_t arity, ObjString *name);

ObjString *copyString(const char *chars, uint32_t length);
ObjString *concatenateString(ObjString *a, ObjString *b);
ObjString *concatenateStringNumber(ObjString *s, double num);
ObjString *concatenateNumberString(double num, ObjString *s);
ObjString *repeatString(ObjString *s, uint32_t n);
int compareString(ObjString *a, ObjString *b);

static inline bool isObjType(Value value, ObjType type) {
    return IS_OBJ(value) && AS_OBJ(value)->type == type;
}

#endif
