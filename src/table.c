/*
 * MIT License
 * Copyright (c) 2025-2026 IMSDcrueoft (https://github.com/IMSDcrueoft)
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
static Entry* findEntry(Entry* entries, uint32_t capacity, ObjString* key, bool isGlobal) {
	Entry* entry = NULL;

	uint32_t index = key->hash & (capacity - 1);
	Entry* tombstone = NULL;

	while (true) {
		entry = &entries[index];

		if (entry->key == NULL) {
			if (IS_NIL(entry->value)) {
				//only for global
				if (isGlobal) {
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
			if (isGlobal) {
				key->symbol = index;
			}
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

		Entry* dest = findEntry(entries, capacity, entry->key, table->isGlobal);
		dest->key = entry->key;
		dest->value = entry->value;

		table->count++;
	}

	FREE_ARRAY(Entry, table->entries, table->capacity);

	table->entries = entries;
	table->capacity = capacity;
}

HOT_FUNCTION
bool tableGet(Table* table, ObjString* key, LfxValue* value_out) {
	if (table->count == 0) return false;

	Entry* entry = findEntry(table->entries, table->capacity, key, table->isGlobal);
	if (entry->key == NULL) return false;

	*value_out = entry->value;
	return true;
}

HOT_FUNCTION
bool tableGetEntry(Table* table, ObjString* key, LfxValue* value_out, Entry** entry_out) {
	if (table->count == 0) return false;

	Entry* entry = findEntry(table->entries, table->capacity, key, table->isGlobal);
	if (entry->key == NULL) return false;

	*value_out = entry->value;
	*entry_out = entry;
	return true;
}

HOT_FUNCTION
bool tableSet(Table* table, ObjString* key, LfxValue value)
{
	//NOTE: no frozen guard here on purpose: freezing is a property-write concept,
	//enforced by the instance property paths in vm.c. Generic tables (builtins
	//import, globals, class methods) must stay freely writable while being built
	//(builtins are frozen at construction and still filled through this function)

	//if (table->count + 1 > table->capacity * TABLE_MAX_LOAD) {
	if ((table->count + 1) > MUL_3_DIV_4((uint64_t)(table->capacity))) {
		uint32_t capacity = GROW_CAPACITY(table->capacity);
		adjustCapacity(table, capacity);
	}

	Entry* entry = findEntry(table->entries, table->capacity, key, table->isGlobal);
	bool isNewKey = entry->key == NULL;
	if (isNewKey && IS_NIL(entry->value)) table->count++;

	entry->key = key;
	entry->value = value;
	return isNewKey;
}

HOT_FUNCTION
bool tableDelete(Table* table, ObjString* key) {
	//NOTE: no frozen guard here (see tableSet): deletion is rejected by the
	//instance property paths in vm.c

	if (table->count == 0) return false;

	// Find the entry.
	Entry* entry = findEntry(table->entries, table->capacity, key, table->isGlobal);
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

bool tableGetFirstEntryIndex(Table* table, uint32_t* index_out) {
	for (uint32_t i = 0; i < table->capacity; i++) {
		if (table->entries[i].key != NULL) {
			*index_out = i;
			return true;
		}
	}

	return false;
}

//on a hit *index_inout has been advanced past the produced slot
HOT_FUNCTION
bool tableIterateNext(Table* table, uint32_t* index_inout, Entry** entry_out) {
	//skips empty slots and tombstones; rehash during iteration is best-effort
	while (*index_inout < table->capacity) {
		Entry* entry = &table->entries[(*index_inout)++];

		if (entry->key != NULL) {
			*entry_out = entry;
			return false;
		}
	}

	return true;
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
	if (table->isEternal) return;//eternal table don't need mark, live forever and its entries are eternal too

	for (uint32_t i = 0; i < table->capacity; i++) {
		Entry* entry = &table->entries[i];
		//markObject((Obj*)entry->key);
		markValue(entry->value);
	}
}