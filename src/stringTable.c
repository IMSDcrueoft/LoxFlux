/*
 * MIT License
 * Copyright (c) 2025 IMSDcrueoft (https://github.com/IMSDcrueoft)
 * See LICENSE file in the root directory for full license text.
*/
#include "memory.h"
#include "table.h"
#include "object.h"
#include "hash.h"

#define TABLE_MAX_LOAD 0.75 // 3/4
#define MUL_3_DIV_4(x) ((x) * 3 / 4)

void stringTable_init(StringTable* table)
{
	table->count = 0;
	table->capacity = 0;
	table->entries = NULL;
}

void stringTable_free(StringTable* table)
{
	FREE_ARRAY_NO_GC(StringEntry, table->entries, table->capacity);
	stringTable_init(table);
}

static StringEntry* findStringEntry(StringEntry* entries, uint32_t capacity, ObjString* key) {
	//check it
	StringEntry* entry = NULL;
	uint32_t index = key->hash & (capacity - 1);

	while (true) {
		entry = &entries[index];

		if (entry->key == NULL) {
			return entry;
		}
		else if (entry->key == key) {
			// We found the key.
			return entry;
		}

		index = (index + 1) & (capacity - 1);
	}
}

static void adjustStringCapacity(StringTable* table, uint32_t capacity) {
	//we need re input, so don't reallocate
	StringEntry* entries = ALLOCATE_NO_GC(StringEntry, capacity);

	for (uint32_t i = 0; i < capacity; ++i) {
		entries[i].key = NULL;
		entries[i].index = UINT32_MAX;
	}

	table->count = 0;

	for (uint32_t i = 0; i < table->capacity; ++i) {
		StringEntry* entry = &table->entries[i];
		if (entry->key == NULL) continue;

		StringEntry* dest = findStringEntry(entries, capacity, entry->key);
		dest->key = entry->key;
		dest->index = entry->index;

		table->count++;
	}

	FREE_ARRAY_NO_GC(StringEntry, table->entries, table->capacity);

	table->entries = entries;
	table->capacity = capacity;
}

bool tableSet_string(StringTable* table, ObjString* key)
{
	//if (table->count + 1 > table->capacity * TABLE_MAX_LOAD) {
	if ((table->count + 1) > MUL_3_DIV_4((uint64_t)(table->capacity))) {
		uint32_t capacity = GROW_CAPACITY(table->capacity);
		adjustStringCapacity(table, capacity);
	}

	StringEntry* entry = findStringEntry(table->entries, table->capacity, key);
	bool isNewKey = (entry->key == NULL);

	if (isNewKey) {
		table->count++;
		entry->key = key;
		entry->index = UINT32_MAX;
	}

	return isNewKey;
}

StringEntry* tableGetStringEntry(StringTable* table, ObjString* key)
{
	//check it
	StringEntry* entry = NULL;
	uint32_t index = key->hash & (table->capacity - 1);

	while (true) {
		entry = &table->entries[index];

		if (entry->key == key) {
			return entry;
		}
		else if (entry->key == NULL) {
			return NULL;
		}

		index = (index + 1) & (table->capacity - 1);
	}
}

void tableSet_script(StringTable* table, ObjString* key, uint32_t index)
{
	//if (table->count + 1 > table->capacity * TABLE_MAX_LOAD) {
	if ((table->count + 1) > MUL_3_DIV_4((uint64_t)(table->capacity))) {
		uint32_t capacity = GROW_CAPACITY(table->capacity);
		adjustStringCapacity(table, capacity);
	}

	StringEntry* entry = findStringEntry(table->entries, table->capacity, key);
	bool isNewKey = (entry->key == NULL);

	if (isNewKey) {
		table->count++;
		entry->key = key;
		entry->index = index;
	}
}

StringEntry* tableGetScriptEntry(StringTable* table, ObjString* key)
{
	if (table->count == 0) return NULL;

	//check it
	StringEntry* entry = NULL;
	uint32_t index = key->hash & (table->capacity - 1);

	while (true) {
		entry = &table->entries[index];

		if (entry->key == key) {
			return entry;
		}
		else if (entry->key == NULL) {
			return NULL;
		}

		index = (index + 1) & (table->capacity - 1);
	}
}

ObjString* tableFindString(StringTable* table, C_STR chars, uint32_t length, uint64_t hash)
{
	if (table->count == 0) return NULL;

	uint32_t index = hash & (table->capacity - 1);

	while (true) {
		StringEntry* entry = &table->entries[index];
		if (entry->key == NULL) {
			return NULL;
		}
		else if (entry->key->length == length &&
			entry->key->hash == hash &&
			memcmp(entry->key->chars, chars, length) == 0) {
			// We found it.
			return entry->key;
		}

		index = (index + 1) & (table->capacity - 1);
	}
}