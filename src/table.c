/*
 * MIT License
 * Copyright (c) 2025 IMSDcrueoft (https://github.com/IMSDcrueoft)
 * See LICENSE file in the root directory for full license text.
*/
#include "memory.h"
#include "object.h"
#include "table.h"
#include "hash.h"
#include "gc.h"

#define TABLE_MAX_LOAD 0.75 // 3/4
#define MUL_3_DIV_4(x) ((x) * 3 / 4)

void table_init(Table* table)
{
	table->inlineCaching = 0;
	table->count = 0;
	table->capacity = 0;
	table->entries = NULL;
}

void table_free(Table* table)
{
	FREE_ARRAY(Entry, table->entries, table->capacity);
	table_init(table);
}

HOT_FUNCTION
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

HOT_FUNCTION
bool tableGet(Table* table, ObjString* key, Value* value) {
	if (table->count == 0) return false;

	Entry* entry = findEntry(table->entries, table->capacity, key, table->type);
	if (entry->key == NULL) return false;

	*value = entry->value;
	return true;
}

HOT_FUNCTION
bool tableSet(Table* table, ObjString* key, Value value)
{
	if (table->type == TABLE_FREEZE) return false;// not allowed

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

HOT_FUNCTION
bool tableDelete(Table* table, ObjString* key) {
	if (table->type == TABLE_FREEZE) return false;// not allowed

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

//void tableRemoveWhite(Table* table)
//{
//	for (uint32_t i = 0; i < table->capacity; i++) {
//		Entry* entry = &table->entries[i];
//		if (entry->key != NULL && !entry->key->obj.isMarked) {
//			tableDelete(table, entry->key);
//		}
//	}
//}

void markTable(Table* table) {
	for (uint32_t i = 0; i < table->capacity; i++) {
		Entry* entry = &table->entries[i];
		//markObject((Obj*)entry->key);
		markValue(entry->value);
	}
}