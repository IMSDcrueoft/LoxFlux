/*
 * MIT License
 * Copyright (c) 2025 IM&SD (https://github.com/IMSDcrueoft)
 * See LICENSE file in the root directory for full license text.
*/
#include "memory.h"
#include "object.h"
#include "table.h"
#include "hash.h"

#define TABLE_MAX_LOAD 0.75 // 3/4
#define MUL_3_DIV_4(x) (((x << 1) + (x)) >> 2)

void table_init(Table* table)
{
	table->count = 0;
	table->capacity = 0;
	table->entries = NULL;
}

void table_free(Table* table)
{
	FREE_ARRAY(Entry, table->entries, table->capacity);
	table_init(table);
}

static Entry* findEntry(Entry* entries, uint32_t capacity, ObjString* key, TableType type) {
	//check it
	Entry* entry = NULL;

	//find by cache symbol
	if ((type == TABLE_GLOBAL) && (key->symbol != INVALID_OBJ_STRING_SYMBOL)) {
		entry = &entries[key->symbol];

		if (entry->key == key) {
			// We found the key.
			return entry;
		}
	}

	uint32_t index = key->hash & (capacity - 1);
	Entry* tombstone = NULL;

	while (true) {
		entry = &entries[index];

		if (entry->key == NULL) {
			if (IS_NIL(entry->value)) {
				//only for global
				if (type == TABLE_GLOBAL) {
					key->symbol = index;
				}
				// if we find hole after tombstone ,it means the tombstone is target else return the hole
				// Empty entry.
				return tombstone != NULL ? tombstone : entry;
			}
			else {
				// We found a tombstone.
				if (tombstone == NULL) tombstone = entry;
			}
		}
		else if (entry->key == key) {
			//only for global
			if (type == TABLE_GLOBAL) {
				key->symbol = index;
			}
			// We found the key.
			return entry;
		}

		index = (index + 1) & (capacity - 1);
	}
}

//for global table
static Entry* findEntry_g(Entry* entries, uint32_t capacity, ObjString* key, TableType type) {
	//check it
	Entry* entry = NULL;

	//find by cache symbol
	if (key->symbol != INVALID_OBJ_STRING_SYMBOL) {
		entry = &entries[key->symbol];

		if (entry->key == key) {
			// We found the key.
			return entry;
		}
	}

	uint32_t index = key->hash & (capacity - 1);
	Entry* tombstone = NULL;

	while (true) {
		entry = &entries[index];

		if (entry->key == NULL) {
			if (IS_NIL(entry->value)) {
				key->symbol = index;
				// if we find hole after tombstone ,it means the tombstone is target else return the hole
				// Empty entry.
				return tombstone != NULL ? tombstone : entry;
			}
			else {
				// We found a tombstone.
				if (tombstone == NULL) tombstone = entry;
			}
		}
		else if (entry->key == key) {
			key->symbol = index;
			// We found the key.
			return entry;
		}

		index = (index + 1) & (capacity - 1);
	}
}

static void adjustCapacity(Table* table, uint32_t capacity) {
	//we need re input, so don't reallocate
	Entry* entries = ALLOCATE(Entry, capacity);

	for (uint32_t i = 0; i < capacity; ++i) {
		entries[i].key = NULL;
		entries[i].value = NIL_VAL;
	}

	table->count = 0;

	for (uint32_t i = 0; i < table->capacity; ++i) {
		Entry* entry = &table->entries[i];
		if (entry->key == NULL) continue;

		Entry* dest = findEntry(entries, capacity, entry->key, table->type);
		dest->key = entry->key;
		dest->value = entry->value;

		table->count++;
	}

	FREE_ARRAY(Entry, table->entries, table->capacity);

	table->entries = entries;
	table->capacity = capacity;
}

bool tableGet(Table* table, ObjString* key, Value* value) {
	if (table->count == 0) return false;

	Entry* entry = findEntry(table->entries, table->capacity, key, table->type);
	if (entry->key == NULL) return false;

	*value = entry->value;
	return true;
}

bool tableSet(Table* table, ObjString* key, Value value)
{
	//if (table->count + 1 > table->capacity * TABLE_MAX_LOAD) {
	if ((table->count + 1) > MUL_3_DIV_4((uint64_t)(table->capacity))) {
		uint32_t capacity = GROW_CAPACITY(table->capacity);
		adjustCapacity(table, capacity);
	}

	Entry* entry = findEntry(table->entries, table->capacity, key, table->type);
	bool isNewKey = entry->key == NULL;
	if (isNewKey && IS_NIL(entry->value)) table->count++;

	entry->key = key;
	entry->value = value;
	return isNewKey;
}

bool tableDelete(Table* table, ObjString* key) {
	if (table->count == 0) return false;

	// Find the entry.
	Entry* entry = findEntry(table->entries, table->capacity, key, table->type);
	if (entry->key == NULL) return false;

	// Place a tombstone in the entry. 
	entry->key = NULL;
	entry->value = BOOL_VAL(true);//value of tombstone is true
	return true;
}

void tableAddAll(Table* from, Table* to)
{
	for (uint32_t i = 0; i < from->capacity; ++i) {
		Entry* entry = &from->entries[i];
		if (entry->key != NULL) {
			tableSet(to, entry->key, entry->value);
		}
	}
}

bool tableGet_g(Table* table, ObjString* key, Value* value) {
	if (table->count == 0) return false;

	Entry* entry = findEntry_g(table->entries, table->capacity, key, table->type);
	if (entry->key == NULL) return false;

	*value = entry->value;
	return true;
}

bool tableSet_g(Table* table, ObjString* key, Value value)
{
	//if (table->count + 1 > table->capacity * TABLE_MAX_LOAD) {
	if ((table->count + 1) > MUL_3_DIV_4((uint64_t)(table->capacity))) {
		uint32_t capacity = GROW_CAPACITY(table->capacity);
		adjustCapacity(table, capacity);
	}

	Entry* entry = findEntry_g(table->entries, table->capacity, key, table->type);
	bool isNewKey = entry->key == NULL;
	if (isNewKey && IS_NIL(entry->value)) table->count++;

	entry->key = key;
	entry->value = value;
	return isNewKey;
}

bool tableDelete_g(Table* table, ObjString* key) {
	if (table->count == 0) return false;

	// Find the entry.
	Entry* entry = findEntry_g(table->entries, table->capacity, key, table->type);
	if (entry->key == NULL) return false;

	// Place a tombstone in the entry. 
	entry->key = NULL;
	entry->value = BOOL_VAL(true);//value of tombstone is true
	return true;
}

ObjString* tableFindString(Table* table, C_STR chars, uint32_t length, uint64_t hash)
{
	if (table->count == 0) return NULL;

	uint32_t index = hash & (table->capacity - 1);

	while (true) {
		Entry* entry = &table->entries[index];
		if (entry->key == NULL) {
			// Stop if we find an empty non-tombstone entry.
			if (IS_NIL(entry->value)) return NULL;
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

Entry* tableGetStringEntry(Table* table, ObjString* key)
{
	//check it
	Entry* entry = NULL;

	uint32_t index = key->hash & (table->capacity - 1);

	while (true) {
		entry = &table->entries[index];

		if (entry->key == key) {
			return entry;
		}
		else if (entry->key == NULL) {
			// Stop if we find an empty non-tombstone entry.
			if (IS_NIL(entry->value)) return NULL;
		}

		index = (index + 1) & (table->capacity - 1);
	}
}

void numberTable_init(NumberTable* table)
{
	table->count = 0;
	table->capacity = 0;
	table->entries = NULL;
}

void numberTable_free(NumberTable* table)
{
	FREE_ARRAY(NumberEntry, table->entries, table->capacity);
	numberTable_init(table);
}

static NumberEntry* findNumberEntry(NumberEntry* entries, uint32_t capacity, uint64_t binary, uint64_t hash) {
	//check it
	NumberEntry* entry = NULL;
	uint32_t index = hash & (capacity - 1);

	while (true) {
		entry = &entries[index];

		if (!entry->isValid) {
			return entry;
		}
		else if (entry->binary == binary) {
			// We found the key.
			return entry;
		}

		index = (index + 1) & (capacity - 1);
	}
}

static void adjustNumberCapacity(NumberTable* table, uint32_t capacity) {
	//we need re input, so don't reallocate
	NumberEntry* entries = ALLOCATE(NumberEntry, capacity);

	for (uint32_t i = 0; i < capacity; ++i) {
		entries[i].binary = 0;
		entries[i].hash = UINT32_MAX;
		entries[i].isValid = false;
		entries[i].index = UINT32_MAX;
	}

	table->count = 0;

	for (uint32_t i = 0; i < table->capacity; ++i) {
		NumberEntry* entry = &table->entries[i];
		if (!entry->isValid) continue;

		NumberEntry* dest = findNumberEntry(entries, capacity, entry->binary, entry->hash);
		dest->binary = entry->binary;
		dest->hash = entry->hash;
		dest->isValid = true;
		dest->index = entry->index;

		table->count++;
	}

	FREE_ARRAY(NumberEntry, table->entries, table->capacity);

	table->entries = entries;
	table->capacity = capacity;
}

// the number might not in pool,i need to treat NaN as NaN,so compare binary
NumberEntry* tableGetNumberEntry(NumberTable* table, Value* value)
{
	//if (table->count + 1 > table->capacity * TABLE_MAX_LOAD) {
	if ((table->count + 1) > MUL_3_DIV_4((uint64_t)(table->capacity))) {
		uint32_t capacity = GROW_CAPACITY(table->capacity);
		adjustNumberCapacity(table, capacity);
	}

	uint64_t hash = HASH_64bits(&AS_BINARY(*value), sizeof(uint64_t));
	NumberEntry* entry = findNumberEntry(table->entries, table->capacity, AS_BINARY(*value), hash);
	bool isNewKey = !entry->isValid;
	if (isNewKey) {
		table->count++;

		entry->binary = AS_BINARY(*value);
		entry->hash = hash;
		entry->isValid = true;
	}

	return entry;
}
