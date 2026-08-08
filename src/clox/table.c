#include "table.h"
#include "memory.h"
#include "object.h" // IWYU pragma: keep
#include <string.h>

#define TABLE_MAX_LOAD 0.75

static Entry *findEntry(Entry *entries, uint32_t capacity, ObjString *key) {
    // uint32_t index = key->hash % capacity;
    // Capacity is always a power of two, so we can use bitwise AND
    // instead of modulo for a faster index computation.
    uint32_t index = key->hash & (capacity - 1);
    Entry *tombstone = NULL;
    for (;;) {
        Entry *entry = &entries[index];
        if (entry->key == NULL) {
            if (IS_NIL(entry->value)) {
                // Empty entry.
                return tombstone != NULL ? tombstone : entry;
            }
            if (tombstone == NULL) {
                // We found a tombstone.
                tombstone = entry;
            }
        } else if (entry->key == key) {
            // We found the key.
            return entry;
        }

        // index = (index + 1) % capacity;
        // Same optimization: capacity is a power of two.
        index = (index + 1) & (capacity - 1);
    }
}

static void adjustCapacity(Table *table, uint32_t capacity) {
    Entry *entries = ALLOCATE(Entry, capacity);
    for (uint32_t i = 0; i < capacity; i++) {
        entries[i].key = NULL;
        entries[i].value = NIL_VAL;
    }

    table->count = 0;
    for (int i = 0; i < table->capacity; i++) {
        Entry *entry = &table->entries[i];
        if (entry->key == NULL) {
            continue;
        }

        Entry *dest = findEntry(entries, capacity, entry->key);
        dest->key = entry->key;
        dest->value = entry->value;
        table->count++;
    }

    FREE_ARRAY(Entry, table->entries, table->capacity);
    table->entries = entries;
    table->capacity = capacity;
}

void initTable(Table *table) {
    table->count = 0;
    table->capacity = 0;
    table->entries = NULL;
}

void freeTable(Table *table) {
    FREE_ARRAY(Entry, table->entries, table->capacity);
    initTable(table);
}

bool tableGet(Table *table, ObjString *key, Value *value) {
    if (table->count == 0) {
        return false;
    }

    Entry *entry = findEntry(table->entries, table->capacity, key);
    if (entry->key == NULL) {
        return false;
    }

    *value = entry->value;
    return true;
}

bool tableSet(Table *table, ObjString *key, Value value) {
    if (table->count + 1 > table->capacity * TABLE_MAX_LOAD) {
        uint32_t capacity = GROW_CAPACITY(table->capacity);
        adjustCapacity(table, capacity);
    }
    Entry *entry = findEntry(table->entries, table->capacity, key);
    bool isNewKey = entry->key == NULL;
    if (isNewKey && IS_NIL(entry->value)) {
        table->count++;
    }

    entry->key = key;
    entry->value = value;
    return isNewKey;
}

bool tableDelete(Table *table, ObjString *key) {
    if (table->count == 0) {
        return false;
    }

    // Find the entry.
    Entry *entry = findEntry(table->entries, table->capacity, key);
    if (entry->key == NULL) {
        return false;
    }

    // Place a tombstone in the entry.
    entry->key = NULL;
    entry->value = BOOL_VAL(true);
    return true;
}

void tableAddAll(Table *from, Table *to) {
    for (int i = 0; i < from->capacity; i++) {
        Entry *entry = &from->entries[i];
        if (entry->key != NULL) {
            tableSet(to, entry->key, entry->value);
        }
    }
}

ObjString *tableFindString(Table *table, const char *chars, uint32_t length,
                           uint32_t hash) {
    if (table->count == 0) {
        return NULL;
    }

    // uint32_t index = hash % table->capacity;
    // Capacity is always a power of two, so we can use bitwise AND
    // instead of modulo for a faster index computation.
    uint32_t index = hash & (table->capacity - 1);
    for (;;) {
        Entry *entry = &table->entries[index];
        if (entry->key == NULL) {
            // Stop if we find an empty non-tombstone entry.
            if (IS_NIL(entry->value)) {
                return NULL;
            }
        } else if (entry->key->length == length && entry->key->hash == hash &&
                   memcmp(entry->key->chars, chars, length) == 0) {
            // We found it.
            return entry->key;
        }

        // index = (index + 1) % table->capacity;
        // Same optimization: capacity is a power of two.
        index = (index + 1) & (table->capacity - 1);
    }
}

void tableRemoveWhite(Table *table) {
    for (uint32_t i = 0; i < table->capacity; i++) {
        Entry *entry = &table->entries[i];
        if (entry->key != NULL && !entry->key->obj.isMarked) {
            tableDelete(table, entry->key);
        }
    }
}

void markTable(Table *table) {
    for (uint32_t i = 0; i < table->capacity; i++) {
        Entry *entry = &table->entries[i];
        markObject((Obj *)entry->key);
        markValue(entry->value);
    }
}
