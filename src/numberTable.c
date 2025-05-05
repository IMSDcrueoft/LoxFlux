/*
 * MIT License
 * Copyright (c) 2025 IMSDcrueoft (https://github.com/IMSDcrueoft)
 * See LICENSE file in the root directory for full license text.
*/
#include "memory.h"
#include "table.h"
#include "hash.h"

#define TABLE_MAX_LOAD 0.75 // 3/4
#define MUL_3_DIV_4(x) ((x) * 3 / 4)

void numberTable_init(NumberTable* table)
{
	table->count = 0;
	table->capacity = 0;
	table->entries = NULL;
}

void numberTable_free(NumberTable* table)
{
	FREE_ARRAY_NO_GC(NumberEntry, table->entries, table->capacity);
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
	NumberEntry* entries = ALLOCATE_NO_GC(NumberEntry, capacity);

	for (uint32_t i = 0; i < capacity; ++i) {
		entries[i].binary = 0;
		entries[i].hash = UINT64_MAX;
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

	FREE_ARRAY_NO_GC(NumberEntry, table->entries, table->capacity);

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