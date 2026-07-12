#include "object.h"
#include "memory.h"
#include "vm.h"

#include <string.h>

#define ALLOCATE_OBJ(type, objectType)                                         \
    (type *)allocateObject(sizeof(type), objectType)

static Obj *allocateObject(size_t size, ObjType type) {
    Obj *object = (Obj *)reallocate(NULL, 0, size);
    object->type = type;

    object->next = vm.objects;
    vm.objects = object;
    return object;
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

ObjString *copyString(const char *chars, uint32_t length) {
    ObjString *string = allocateString(length);
    memcpy(string->chars, chars, length);
    string->chars[length] = '\0';
    return string;
}

ObjString *concatenateString(ObjString *a, ObjString *b) {
    if (a == NULL || b == NULL) {
        return NULL;
    }

    uint32_t length = a->length + b->length;
    ObjString *string = allocateString(length);
    memcpy(string->chars, a->chars, a->length);
    memcpy(string->chars + a->length, b->chars, b->length);
    return string;
}
