#include "memory.h"
#include "compiler.h"
#include "object.h"
#include "vm.h"
#include <stdlib.h>

#ifdef DEBUG_LOG_GC
#include <stdio.h>
#endif

static void freeObject(Obj *object) {
#ifdef DEBUG_LOG_GC
    fprintf(stdout, "%p free type %d\n", (void *)object, object->type);
#endif

    switch (object->type) {
    case OBJ_BOUND_METHOD:
        FREE(ObjBoundMethod, object);
        break;
    case OBJ_CLASS: {
        ObjInstance *instance = (ObjInstance *)object;
        freeTable(&instance->fields);
        ObjClass *klass = (ObjClass *)object;
        freeTable(&klass->methods);
        FREE(ObjClass, object);
        break;
    }
    case OBJ_CLOSURE: {
        ObjClosure *closure = (ObjClosure *)object;
        FREE_ARRAY(ObjUpvalue *, (void *)closure->upvalues,
                   closure->upvalueCount);
        FREE(ObjClosure, object);
        break;
    }
    case OBJ_FUNCTION: {
        ObjFunction *function = (ObjFunction *)object;
        freeChunk(&function->chunk);
        FREE(ObjFunction, object);
        break;
    }
    case OBJ_INSTANCE: {
        ObjInstance *instance = (ObjInstance *)object;
        freeTable(&instance->fields);
        FREE(ObjInstance, object);
        break;
    }
    case OBJ_NATIVE:
        FREE(ObjNative, object);
        break;
    case OBJ_STRING: {
        ObjString *string = (ObjString *)object;
        FREE(ObjString, object);
        break;
    }
    case OBJ_UPVALUE:
        FREE(ObjUpvalue, object);
        break;
    default:
        break;
    }
}

static void markArray(ValueArray *array) {
    for (uint32_t i = 0; i < array->count; i++) {
        markValue(array->values[i]);
    }
}

static void blackenObject(Obj *object) {
#ifdef DEBUG_LOG_GC
    fprintf(stdout, "%p blacken ", (void *)object);
    printValue(stdout, OBJ_VAL(object));
    fprintf(stdout, "\n");
#endif

    switch (object->type) {
    case OBJ_BOUND_METHOD: {
        ObjBoundMethod *bound = (ObjBoundMethod *)object;
        markValue(bound->receiver);
        markObject((Obj *)bound->method);
        break;
    }
    case OBJ_CLASS: {
        ObjClass *klass = (ObjClass *)object;
        ObjInstance *instance = (ObjInstance *)klass;
        if (instance->klass) {
            markObject((Obj *)instance->klass);
        }
        markTable(&instance->fields);
        markObject((Obj *)klass->name);
        markTable(&klass->methods);
        break;
    }
    case OBJ_CLOSURE: {
        ObjClosure *closure = (ObjClosure *)object;
        markObject((Obj *)closure->function);
        for (uint32_t i = 0; i < closure->upvalueCount; i++) {
            markObject((Obj *)closure->upvalues[i]);
        }
        break;
    }
    case OBJ_FUNCTION: {
        ObjFunction *function = (ObjFunction *)object;
        markObject((Obj *)function->name);
        markArray(&function->chunk.constants);
        break;
    }
    case OBJ_INSTANCE: {
        ObjInstance *instance = (ObjInstance *)object;
        markObject((Obj *)instance->klass);
        markTable(&instance->fields);
        break;
    }
    case OBJ_NATIVE:
    case OBJ_STRING:
        break;
    case OBJ_UPVALUE:
        markValue(((ObjUpvalue *)object)->closed);
        break;
    }
}

static void markRoots() {
    // Roots: values on the stack.
    for (Value *slot = vm.stack; slot < vm.stackTop; slot++) {
        markValue(*slot);
    }

    // Call frames.
    for (int i = 0; i < vm.frameCount; i++) {
        markObject((Obj *)vm.frames[i].closure);
    }
    // Upvalues.
    for (ObjUpvalue *upvalue = vm.openUpvalues; upvalue != NULL;
         upvalue = upvalue->next) {
        markObject((Obj *)upvalue);
    }

    // Roots: global varaibles.
    markTable(&vm.globals);

    markCompilerRoots();
    markObject((Obj *)vm.type);
    markObject((Obj *)vm.initString);
}

static void traceReferences() {
    while (vm.grayCount > 0) {
        Obj *object = vm.grayStack[--vm.grayCount];
        blackenObject(object);
    }
}

static void sweep() {
    Obj *previous = NULL;
    Obj *object = vm.objects;
    while (object != NULL) {
        if (object->isMarked) {
            object->isMarked = false;
            previous = object;
            object = object->next;
        } else {
            Obj *unreached = object;
            object = object->next;
            if (previous != NULL) {
                previous->next = object;
            } else {
                vm.objects = object;
            }

            freeObject(unreached);
        }
    }
}

#define GC_HEAP_GROW_FACTOR 2

void collectGarbage() {
#ifdef DEBUG_LOG_GC
    fprintf(stdout, "-- gc begin\n");
    size_t before = vm.bytesAllocated;
#endif

    // Mark-Sweep
    markRoots();
    traceReferences();
    tableRemoveWhite(&vm.strings);
    sweep();

    vm.nextGC = vm.bytesAllocated * GC_HEAP_GROW_FACTOR;

#ifdef DEBUG_LOG_GC
    fprintf(stdout, "-- gc end\n");
    fprintf(stdout, "   collected %zu bytes (from %zu to %zu) next at %zu\n",
            before - vm.bytesAllocated, before, vm.bytesAllocated, vm.nextGC);
#endif
}

void *reallocate(void *pointer, size_t oldSize, size_t newSize) {
    vm.bytesAllocated += newSize - oldSize;
    if (newSize > oldSize) {
#ifdef DEBUG_STRESS_GC
        collectGarbage();
#endif

        if (vm.bytesAllocated > vm.nextGC) {
            collectGarbage();
        }
    }
    if (newSize == 0) {
        free(pointer);
        return NULL;
    }

    void *result = realloc(pointer, newSize);
    if (result == NULL) {
        exit(1);
    }
    return result;
}

void markValue(Value value) {
    if (IS_OBJ(value)) {
        markObject(AS_OBJ(value));
    }
}

void markObject(Obj *object) {
    if (object == NULL) {
        return;
    }
    if (object->isMarked) {
        return;
    }

#ifdef DEBUG_LOG_GC
    fprintf(stdout, "%p mark ", (void *)object);
    printValue(stdout, OBJ_VAL(object));
    fprintf(stdout, "\n");
#endif

    object->isMarked = true;

    if (vm.grayCapacity < vm.grayCount + 1) {
        int newCapacity = GROW_CAPACITY(vm.grayCapacity);
        Obj **newStack =
            (Obj **)realloc((void *)vm.grayStack, sizeof(Obj *) * newCapacity);
        if (newStack == NULL) {
            // Handle out-of-memory (exit, or try to recover GC state)
            exit(1);
        }
        vm.grayStack = newStack;
        vm.grayCapacity = newCapacity;
    }

    vm.grayStack[vm.grayCount++] = object;
}

void freeObjects() {
    Obj *object = vm.objects;
    while (object != NULL) {
        Obj *next = object->next;
        freeObject(object);
        object = next;
    }

    free((void *)vm.grayStack);
}
