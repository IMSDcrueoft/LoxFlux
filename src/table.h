/*
 * MIT License
 * Copyright (c) 2025-2026 IMSDcrueoft (https://github.com/IMSDcrueoft)
 * See LICENSE file in the root directory for full license text.
*/
#pragma once
#include "common.h"
#include "value.h"

typedef struct {
	ObjString* key;
	Value value;
} Entry;

typedef struct {
	uint64_t binary;//binary info of number
	uint64_t hash;//hash of number
	bool isValid;
	uint32_t index;//index of constant array
} NumberEntry;

typedef struct {
	ObjString* key;
	uint32_t index;//index of constant array
} StringEntry;

typedef struct {
	bool isGlobal;	// global has its own ic
	bool isFrozen;	// property writes/deletes are rejected by the instance paths in vm.c (tableSet/tableDelete stay unguarded on purpose)
	bool isEternal;	// cannot be freed by gc and no need to mark it
	bool isPoisoned;// for inline cache

	uint8_t extendPayload[4];

	uint32_t count;
	uint32_t capacity;
	Entry* entries;
} Table;

typedef struct {
	uint32_t count;
	uint32_t capacity;
	NumberEntry* entries;
} NumberTable;

typedef struct {
	uint32_t count;
	uint32_t capacity;
	StringEntry* entries;
} StringTable;

void table_init(Table* table);
void table_free(Table* table);

bool tableGet(Table* table, ObjString* key, Value* value_out);
//single-probe: get value and entry pointer in one probe (for inline cache backfill)
bool tableGetEntry(Table* table, ObjString* key, Value* value_out, Entry** entry_out);
bool tableSet(Table* table, ObjString* key, Value value);
bool tableDelete(Table* table, ObjString* key);
void tableAddAll(Table* from, Table* to);

//void tableRemoveWhite(Table* table);
void markTable(Table* table);

ObjString* tableFindString(StringTable* table, C_STR chars, uint32_t length, uint64_t hash);
//tableFindString over a not-materialized concat: the target content is
//segment a followed by segment b (connect-string dedup, zero temporaries)
ObjString* tableFindConcatString(StringTable* table, C_STR charsA, uint32_t lengthA, C_STR charsB, uint32_t lengthB, uint64_t hash);

void stringTable_init(StringTable* table);
void stringTable_free(StringTable* table);
bool tableSet_string(StringTable* table, ObjString* key);
StringEntry* tableGetStringEntry(StringTable* table, ObjString* key);

void tableSet_script(StringTable* table, ObjString* key, uint32_t index);
StringEntry* tableGetScriptEntry(StringTable* table, ObjString* key);

void numberTable_init(NumberTable* table);
void numberTable_free(NumberTable* table);
NumberEntry* tableGetNumberEntry(NumberTable* table, Value* value);