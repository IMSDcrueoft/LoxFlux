/*
 * MIT License
 * Copyright (c) 2025-2026 IMSDcrueoft (https://github.com/IMSDcrueoft)
 * See LICENSE file in the root directory for full license text.
*/
#include "memory.h"
#include "table.h"
#include "hash.h"

#define TABLE_MAX_LOAD 0.75 // 3/4
#define MUL_3_DIV_4(x) ((x) * 3 / 4)

void binaryTable_init(BinaryTable* table)
{
	table->count = 0;
	table->capacity = 0;
	table->entries = NULL;
}

void binaryTable_free(LfxVM* vm, BinaryTable* table)
{
	FREE_ARRAY_NO_GC(vm, BinaryEntry, table->entries, table->capacity);
	binaryTable_init(table);
}

static BinaryEntry* findBinaryEntry(BinaryEntry* entries, uint32_t capacity, uint64_t binary, uint64_t hash) {
	BinaryEntry* entry = NULL;
	uint32_t index = hash & (capacity - 1);

	while (true) {
		entry = &entries[index];

		if (!entry->isValid) {
			return entry;
		}
		else if (entry->binary == binary) {
			return entry;
		}

		index = (index + 1) & (capacity - 1);
	}
}

static void adjustBinaryCapacity(LfxVM* vm, BinaryTable* table, uint32_t capacity) {
	//we need re input, so don't reallocate
	BinaryEntry* entries = ALLOCATE_NO_GC(vm, BinaryEntry, capacity);

	for (uint32_t i = 0; i < capacity; ++i) {
		entries[i].binary = 0;
		entries[i].hash = UINT64_MAX;
		entries[i].isValid = false;
		entries[i].index = UINT32_MAX;
	}

	table->count = 0;

	for (uint32_t i = 0; i < table->capacity; ++i) {
		BinaryEntry* entry = &table->entries[i];
		if (!entry->isValid) continue;

		BinaryEntry* dest = findBinaryEntry(entries, capacity, entry->binary, entry->hash);
		dest->binary = entry->binary;
		dest->hash = entry->hash;
		dest->isValid = true;
		dest->index = entry->index;

		table->count++;
	}

	FREE_ARRAY_NO_GC(vm, BinaryEntry, table->entries, table->capacity);

	table->entries = entries;
	table->capacity = capacity;
}

// the key might not be in pool: keys are compared as raw 64-bit binary (NaN as NaN, natives by pointer bits)
BinaryEntry* tableGetBinaryEntry(BinaryTable* table, LfxValue* value)
{
	if (table->count == 0) return NULL;//empty pool:capacity - 1 would go out of bounds

	uint64_t binary = AS_BINARY(*value);
	uint64_t hash = HASH_64bits(&binary, sizeof(uint64_t));

	uint32_t index = hash & (table->capacity - 1);

	while (true) {
		BinaryEntry* entry = &table->entries[index];

		if (!entry->isValid) {
			return NULL;
		}
		else if (entry->binary == binary) {
			return entry;
		}

		index = (index + 1) & (table->capacity - 1);
	}
}

BinaryEntry* tableGetOrInsertBinaryEntry(LfxVM* vm, BinaryTable* table, LfxValue* value)
{
	(void)vm;//load-bearing once the _NO_GC macros take the vm
	if ((table->count + 1) > MUL_3_DIV_4((uint64_t)(table->capacity))) {
		uint32_t capacity = GROW_CAPACITY(table->capacity);
		adjustBinaryCapacity(vm, table, capacity);
	}

	uint64_t binary = AS_BINARY(*value);
	uint64_t hash = HASH_64bits(&binary, sizeof(uint64_t));
	BinaryEntry* entry = findBinaryEntry(table->entries, table->capacity, binary, hash);

	if (!entry->isValid) {
		table->count++;

		entry->binary = binary;
		entry->hash = hash;
		entry->isValid = true;
	}

	return entry;
}

bool tableSet_binary(LfxVM* vm, BinaryTable* table, LfxValue* value)
{
	(void)vm;//load-bearing once the _NO_GC macros take the vm
	if ((table->count + 1) > MUL_3_DIV_4((uint64_t)(table->capacity))) {
		uint32_t capacity = GROW_CAPACITY(table->capacity);
		adjustBinaryCapacity(vm, table, capacity);
	}

	uint64_t binary = AS_BINARY(*value);
	uint64_t hash = HASH_64bits(&binary, sizeof(uint64_t));
	BinaryEntry* entry = findBinaryEntry(table->entries, table->capacity, binary, hash);

	bool isNewKey = !entry->isValid;
	if (isNewKey) {
		table->count++;

		entry->binary = binary;
		entry->hash = hash;
		entry->isValid = true;
	}

	return isNewKey;
}
