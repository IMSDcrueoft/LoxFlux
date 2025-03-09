/*
 * MIT License
 * Copyright (c) 2025 IM&SD (https://github.com/IMSDcrueoft)
 * See LICENSE file in the root directory for full license text.
*/
#pragma once
#include "common.h"
#include "value.h"
#include "memory.h"
#include "table.h"

typedef enum {
	//char array
	OBJ_STRING,
	//any type array
	OBJ_ARRAY,
	OBJ_ARRAY_U8,
	OBJ_ARRAY_I8,
	OBJ_ARRAY_U16,
	OBJ_ARRAY_I16,
	OBJ_ARRAY_U32,
	OBJ_ARRAY_I32,
	OBJ_ARRAY_F32,
	OBJ_ARRAY_F64,
} ObjType;

struct Obj {
	ObjType type;
	struct Obj* next;
};

#define INVALID_OBJ_STRING_SYMBOL UINT32_MAX

struct ObjString {
	Obj obj;

	uint32_t symbol; // used to boost global hash table

	uint32_t length;
	uint64_t hash;
	char chars[]; // flexible array members FAM
};

struct ObjArray {
	Obj obj;
	uint32_t capacity;
	uint32_t length;
	Value elements[];
};

struct ObjArrayU8 {
	Obj obj;
	uint32_t capacity;
	uint32_t length;
	uint8_t elements[];
};

struct ObjArrayI8 {
	Obj obj;
	uint32_t capacity;
	uint32_t length;
	int8_t elements[];
};

struct ObjArrayU16 {
	Obj obj;
	uint32_t capacity;
	uint32_t length;
	uint16_t elements[];
};

struct ObjArrayI16 {
	Obj obj;
	uint32_t capacity;
	uint32_t length;
	int16_t elements[];
};

struct ObjArrayU32 {
	Obj obj;
	uint32_t capacity;
	uint32_t length;
	uint32_t elements[];
};

struct ObjArrayI32 {
	Obj obj;
	uint32_t capacity;
	uint32_t length;
	int32_t elements[];
};

struct ObjArrayF32 {
	Obj obj;
	uint32_t capacity;
	uint32_t length;
	float32_t elements[];
};

struct ObjArrayF64 {
	Obj obj;
	uint32_t capacity;
	uint32_t length;
	float64_t elements[];
};

#define OBJ_TYPE(value)        (AS_OBJ(value)->type)
#define IS_STRING(value)		isObjType(value, OBJ_STRING)
#define IS_ARRAY(value)			isObjType(value, OBJ_ARRAY)

#define IS_ARRAY_U8(value)       isObjType(value, OBJ_ARRAY_U8)
#define IS_ARRAY_I8(value)       isObjType(value, OBJ_ARRAY_I8)
#define IS_ARRAY_U16(value)       isObjType(value, OBJ_ARRAY_U16)
#define IS_ARRAY_I16(value)       isObjType(value, OBJ_ARRAY_I16)
#define IS_ARRAY_U32(value)       isObjType(value, OBJ_ARRAY_U32)
#define IS_ARRAY_I32(value)       isObjType(value, OBJ_ARRAY_I32)
#define IS_ARRAY_F32(value)       isObjType(value, OBJ_ARRAY_F32)
#define IS_ARRAY_F64(value)       isObjType(value, OBJ_ARRAY_F64)

#define AS_STRING(value)       ((ObjString*)AS_OBJ(value))
#define AS_CSTRING(value)      (((ObjString*)AS_OBJ(value))->chars)

static inline bool isObjType(Value value, ObjType type) {
	return IS_OBJ(value) && AS_OBJ(value)->type == type;
}

static inline bool IS_ARRAY_LIKE(Value value) {
	return IS_OBJ(value) && AS_OBJ(value)->type >= OBJ_ARRAY;//enum type
}

ObjString* copyString(C_STR chars, uint32_t length, bool escapeChars);
ObjString* connectString(ObjString* strA, ObjString* strB);

void printObject(Value value);

Entry* getStringEntryInPool(ObjString* string);
NumberEntry* getNumberEntryInPool(Value* value);