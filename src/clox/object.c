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

    object->isMarked = false;
    object->next = vm.objects;
    vm.objects = object;

#ifdef DEBUG_LOG_GC
    char typeOfObject[128];
    Value obj = OBJ_VAL(object);
    if (type == OBJ_INSTANCE) {
        // Intialized the `klass` field so that
        // we can safely call `typeOf`.
        AS_INSTANCE(obj)->klass = NULL;
    }
    typeOf(&obj, typeOfObject, sizeof(typeOfObject));
    fprintf(stdout, "%p allocate %zu for %s\n", (void *)object, size,
            typeOfObject);
#endif

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
    string->obj.isMarked = false;
    vm.objects = (Obj *)string;
    string->length = length;
    string->chars[length] = '\0';
#ifdef DEBUG_LOG_GC
    fprintf(stdout, "%p allocate %zu for string\n", (void *)string,
            sizeof(ObjString) +
                ((length + 1) * sizeof(((ObjString *)0)->chars[0])));
#endif

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

    /*
     * The string is brand new, it isn’t reachable anywhere. And resizing the
     * string pool can trigger a collection. To avoid this, we stash the string
     * on the stack first.
     */
    push(OBJ_VAL(string));
    tableSet(&vm.strings, string, NIL_VAL);
    pop();
    return string;
}

static void stringifyFunction(const ObjFunction *function, char *buffer,
                              size_t size) {
    if (function->name == NULL) {
        if (function->isLambda) {
            snprintf(buffer, size, "<lambda>");
        } else {
            snprintf(buffer, size, "<script>");
        }
    } else {
        snprintf(buffer, size, "<lox fn %s>", function->name->chars);
    }
}

void stringifyObject(const Value *value, char *buffer, size_t size) {
    switch (OBJ_TYPE(*value)) {
    case OBJ_BOUND_METHOD: {
        ObjBoundMethod *bound = AS_BOUND_METHOD(*value);
        Value *receiver = &bound->receiver;
        if (IS_INSTANCE(*receiver)) {
            if (IS_CLASS(*receiver)) {
                ObjClass *klass = AS_CLASS(*receiver);
                snprintf(buffer, size, "<class method %s.%s>",
                         klass->name->chars,
                         bound->method->function->name->chars);
            } else {
                ObjClass *klass = AS_INSTANCE(*receiver)->klass;
                snprintf(buffer, size, "<bound method %s.%s>",
                         klass->name->chars,
                         bound->method->function->name->chars);
            }
            break;
        }
        stringifyFunction(bound->method->function, buffer, size);
        break;
    }
    case OBJ_PROPERTY:
        snprintf(buffer, size, "<lox property>");
        break;
    case OBJ_CLASS:
        snprintf(buffer, size, "<lox class %s>", AS_CLASS(*value)->name->chars);
        break;
    case OBJ_CLOSURE:
        stringifyFunction(AS_CLOSURE(*value)->function, buffer, size);
        break;
    case OBJ_FUNCTION:
        stringifyFunction(AS_FUNCTION(*value), buffer, size);
        break;
    case OBJ_INSTANCE:
        snprintf(buffer, size, "<%s instance>",
                 AS_INSTANCE(*value)->klass->name->chars);
        break;
    case OBJ_NATIVE:
        snprintf(buffer, size, "<native fn %s>",
                 ((ObjNative *)AS_OBJ(*value))->name->chars);
        break;
    case OBJ_STRING:
        snprintf(buffer, size, "%s", AS_CSTRING(*value));
        break;
    case OBJ_UPVALUE:
        snprintf(buffer, size, "upvalue");
        break;
    }
}

ObjBoundMethod *newBoundMethod(Value receiver, ObjClosure *method) {
    ObjBoundMethod *bound = ALLOCATE_OBJ(ObjBoundMethod, OBJ_BOUND_METHOD);
    bound->receiver = receiver;
    bound->method = method;
    return bound;
}

ObjProperty *newProperty(ObjClosure *getter, ObjClosure *setter) {
    ObjProperty *property = ALLOCATE_OBJ(ObjProperty, OBJ_PROPERTY);
    property->getter = getter;
    property->setter = setter;
    return property;
}

ObjClass *newClass(ObjString *name) {
    ObjClass *klass = ALLOCATE_OBJ(ObjClass, OBJ_CLASS);
    ObjInstance *instance = (ObjInstance *)klass;
    if (vm.type == NULL) {
        // The base metaclass itself.
        instance->klass = NULL;
    } else {
        instance->klass = vm.type;
    }
    initTable(&instance->fields);
    klass->name = name;
    initTable(&klass->methods);
    initTable(&klass->properties);
    return klass;
}

ObjClosure *newClosure(ObjFunction *function) {
    ObjUpvalue **upvalues = ALLOCATE(ObjUpvalue *, function->upvalueCount);
    for (uint32_t i = 0; i < function->upvalueCount; i++) {
        upvalues[i] = NULL;
    }

    ObjClosure *closure = ALLOCATE_OBJ(ObjClosure, OBJ_CLOSURE);
    closure->function = function;
    closure->upvalues = upvalues;
    closure->upvalueCount = function->upvalueCount;
    return closure;
}

ObjFunction *newFunction() {
    ObjFunction *function = ALLOCATE_OBJ(ObjFunction, OBJ_FUNCTION);
    function->arity = 0;
    function->upvalueCount = 0;
    function->isLambda = false;
    function->name = NULL;
    initChunk(&function->chunk);
    return function;
}

ObjInstance *newInstance(ObjClass *klass) {
    ObjInstance *instance = ALLOCATE_OBJ(ObjInstance, OBJ_INSTANCE);
    instance->klass = klass;
    initTable(&instance->fields);
    return instance;
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

ObjUpvalue *newUpvalue(Value *slot) {
    ObjUpvalue *upvalue = ALLOCATE_OBJ(ObjUpvalue, OBJ_UPVALUE);
    upvalue->location = slot;
    upvalue->closed = UNDEFINED_VAL;
    upvalue->next = NULL;
    return upvalue;
}
