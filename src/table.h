/*
 * MIT License
 * Copyright (c) 2025 IM&SD (https://github.com/IMSDcrueoft)
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

typedef enum {
	TABLE_NORMAL,
	TABLE_GLOBAL,
	TABLE_MODULE
} TableType;

typedef struct {
	TableType type;
	uint32_t __PADDING;
	uint32_t count;
	uint32_t capacity;
	Entry* entries;
} Table;

typedef struct {
	uint32_t count;
	uint32_t capacity;
	NumberEntry* entries;
} NumberTable;

void table_init(Table* table);
void table_free(Table* table);

bool tableGet(Table* table, ObjString* key, Value* value);
bool tableSet(Table* table, ObjString* key, Value value);
bool tableDelete(Table* table, ObjString* key);
void tableAddAll(Table* from, Table* to);

//globol table fn
bool tableGet_g(Table* table, ObjString* key, Value* value);
bool tableSet_g(Table* table, ObjString* key, Value value);
bool tableDelete_g(Table* table, ObjString* key);

ObjString* tableFindString(Table* table, C_STR chars,uint32_t length, uint64_t hash);

Entry* tableGetStringEntry(Table* table, ObjString* string);
//if not value exist, set add return the entry pointer
void numberTable_init(NumberTable* table);
void numberTable_free(NumberTable* table);
NumberEntry* tableGetNumberEntry(NumberTable* table, Value *value);