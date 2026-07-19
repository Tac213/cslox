#include "object.h"
#include "memory.h"
#include "vm.h"

#include <string.h>

#define ALLOCATE_OBJ(type, objectType)                                         \
    (type *)allocateObject(sizeof(type), objectType)

#define STACK_STRING_BUFFER_SIZE 256

static Obj *allocateObject(size_t size, ObjType type) {
    Obj *object = (Obj *)reallocate(NULL, 0, size);
    object->type = type;

    object->next = vm.objects;
    vm.objects = object;
    return object;
}

static uint32_t hashString(const char *key, uint32_t length) {
    uint32_t hash = 2166136261U;
    for (uint32_t i = 0; i < length; i++) {
        hash ^= (uint8_t)key[i];
        hash *= 16777619;
    }
    return hash;
}

static ObjString *allocateString(uint32_t length) {
    ObjString *string = (ObjString *)reallocate(
        NULL, 0,
        sizeof(ObjString) +
            ((length + 1) * sizeof(((ObjString *)0)->chars[0])));
    string->obj.type = OBJ_STRING;
    string->obj.next = vm.objects;
    vm.objects = (Obj *)string;
    string->length = length;
    string->chars[length] = '\0';
    return string;
}

static ObjString *internString(const char *chars, uint32_t length,
                               uint32_t hash) {
    ObjString *interned = tableFindString(&vm.strings, chars, length, hash);
    if (interned != NULL) {
        return interned;
    }

    // Not found — allocate and insert into the intern table
    ObjString *string = allocateString(length);
    memcpy(string->chars, chars, length);
    string->chars[length] = '\0';
    string->hash = hash;
    tableSet(&vm.strings, string, NIL_VAL);
    return string;
}

void stringifyObject(const Value *value, char *buffer, size_t size) {
    switch (OBJ_TYPE(*value)) {
    case OBJ_FUNCTION:
        if (AS_FUNCTION(*value)->name == NULL) {
            if (AS_FUNCTION(*value)->isLambda) {
                snprintf(buffer, size, "<lambda>");
            } else {
                snprintf(buffer, size, "<script>");
            }
            break;
        }
        snprintf(buffer, size, "<lox fn %s>", AS_FUNCTION(*value)->name->chars);
        break;
    case OBJ_NATIVE:
        snprintf(buffer, size, "<native fn %s>",
                 ((ObjNative *)AS_OBJ(*value))->name->chars);
        break;
    case OBJ_STRING:
        snprintf(buffer, size, "%s", AS_CSTRING(*value));
        break;
    }
}

ObjFunction *newFunction() {
    ObjFunction *function = ALLOCATE_OBJ(ObjFunction, OBJ_FUNCTION);
    function->arity = 0;
    function->isLambda = false;
    function->name = NULL;
    initChunk(&function->chunk);
    return function;
}

ObjNative *newNative(NativeFn function, uint8_t arity, ObjString *name) {
    ObjNative *native = ALLOCATE_OBJ(ObjNative, OBJ_NATIVE);
    native->function = function;
    native->arity = arity;
    native->name = name;
    return native;
}

void freeFunction(ObjFunction *function) {
    // Remove from vm.objects linked list.
    Obj **current = &vm.objects;
    while (*current != NULL) {
        if (*current == (Obj *)function) {
            *current = function->obj.next;
            break;
        }
        current = &(*current)->next;
    }
    freeChunk(&function->chunk);
    FREE(ObjFunction, function);
}

ObjString *copyString(const char *chars, uint32_t length) {
    uint32_t hash = hashString(chars, length);
    return internString(chars, length, hash);
}

ObjString *concatenateString(ObjString *a, ObjString *b) {
    if (a == NULL || b == NULL) {
        return NULL;
    }

    uint32_t length = a->length + b->length;
    int isOnStack = length < STACK_STRING_BUFFER_SIZE;
    char stackBuf[STACK_STRING_BUFFER_SIZE];
    char *temp = isOnStack ? stackBuf : ALLOCATE(char, length + 1);

    memcpy(temp, a->chars, a->length);
    memcpy(temp + a->length, b->chars, b->length);
    temp[length] = '\0';

    uint32_t hash = hashString(temp, length);
    ObjString *string = internString(temp, length, hash);

    if (!isOnStack) {
        FREE_ARRAY(char, temp, length + 1);
    }
    return string;
}

ObjString *concatenateStringNumber(ObjString *s, double num) {
    if (s == NULL) {
        return NULL;
    }

    char numberStr[32];
    uint32_t numberStrLen = snprintf(numberStr, sizeof(numberStr), "%g", num);
    uint32_t length = s->length + numberStrLen;
    int isOnStack = length < STACK_STRING_BUFFER_SIZE;
    char stackBuf[STACK_STRING_BUFFER_SIZE];
    char *temp = isOnStack ? stackBuf : ALLOCATE(char, length + 1);

    memcpy(temp, s->chars, s->length);
    memcpy(temp + s->length, numberStr, numberStrLen);
    temp[length] = '\0';

    uint32_t hash = hashString(temp, length);
    ObjString *string = internString(temp, length, hash);

    if (!isOnStack) {
        FREE_ARRAY(char, temp, length + 1);
    }
    return string;
}

ObjString *concatenateNumberString(double num, ObjString *s) {
    if (s == NULL) {
        return NULL;
    }

    char numberStr[32];
    uint32_t numberStrLen = snprintf(numberStr, sizeof(numberStr), "%g", num);
    uint32_t length = s->length + numberStrLen;
    int isOnStack = length < STACK_STRING_BUFFER_SIZE;
    char stackBuf[STACK_STRING_BUFFER_SIZE];
    char *temp = isOnStack ? stackBuf : ALLOCATE(char, length + 1);

    memcpy(temp, numberStr, numberStrLen);
    memcpy(temp + numberStrLen, s->chars, s->length);
    temp[length] = '\0';

    uint32_t hash = hashString(temp, length);
    ObjString *string = internString(temp, length, hash);

    if (!isOnStack) {
        FREE_ARRAY(char, temp, length + 1);
    }
    return string;
}

ObjString *repeatString(ObjString *s, uint32_t n) {
    if (s == NULL) {
        return NULL;
    }
    if (n == 0) {
        return internString("", 0, hashString("", 0));
    }

    uint32_t length = s->length * n;
    int isOnStack = length < STACK_STRING_BUFFER_SIZE;
    char stackBuf[STACK_STRING_BUFFER_SIZE];
    char *temp = isOnStack ? stackBuf : ALLOCATE(char, length + 1);

    char *ptr = temp;
    for (uint32_t i = 0; i < n; i++) {
        memcpy(ptr, s->chars, s->length);
        ptr += s->length;
    }
    temp[length] = '\0';

    uint32_t hash = hashString(temp, length);
    ObjString *string = internString(temp, length, hash);

    if (!isOnStack) {
        FREE_ARRAY(char, temp, length + 1);
    }
    return string;
}

int compareString(ObjString *a, ObjString *b) {
    return strcmp(a->chars, b->chars);
}
