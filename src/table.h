/*
 * MIT License
 * Copyright (c) 2025 IMSDcrueoft (https://github.com/IMSDcrueoft)
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

typedef enum {
	TABLE_NORMAL,
	TABLE_GLOBAL,
	TABLE_FREEZE
} TableType;

typedef struct {
	TableType type;
	uint32_t padding;

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

bool tableGet(Table* table, ObjString* key, Value* value);
bool tableSet(Table* table, ObjString* key, Value value);
bool tableDelete(Table* table, ObjString* key);
void tableAddAll(Table* from, Table* to);

//void tableRemoveWhite(Table* table);
void markTable(Table* table);

ObjString* tableFindString(StringTable* table, C_STR chars,uint32_t length, uint64_t hash);

void stringTable_init(StringTable* table);
void stringTable_free(StringTable* table);
bool tableSet_string(StringTable* table, ObjString* key);
StringEntry* tableGetStringEntry(StringTable* table, ObjString* key);

//if not value exist, set add return the entry pointer
void numberTable_init(NumberTable* table);
void numberTable_free(NumberTable* table);
NumberEntry* tableGetNumberEntry(NumberTable* table, Value *value);