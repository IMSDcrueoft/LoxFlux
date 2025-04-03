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
#include "chunk.h"

typedef enum {
	OBJ_CLOSURE,
	OBJ_FUNCTION,
	OBJ_NATIVE,
	OBJ_UPVALUE,
	//char array
	OBJ_STRING,
	//string builder and array are typed arrays
	OBJ_STRING_BUILDER,
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

typedef struct {
	Obj obj;
	uint32_t arity;
	uint32_t upvalueCount;
	Chunk chunk;
	ObjString* name;
} ObjFunction;

typedef struct ObjUpvalue {
	Obj obj;
	Value* location;
	Value closed; //closed value
	struct ObjUpvalue* next;
} ObjUpvalue;

typedef struct {
	Obj obj;

	int32_t upvalueCount;
	ObjUpvalue** upvalues;

	ObjFunction* function;
} ObjClosure;

//argCount and argValues
typedef Value(*NativeFn)(int argCount, Value* args, C_STR* errorInfo);

typedef struct {
	Obj obj;
	NativeFn function;
} ObjNative;

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
	char payload[];
};

#define OBJ_TYPE(value)				(AS_OBJ(value)->type)
#define IS_CLOSURE(value)			isObjType(value, OBJ_CLOSURE)
#define IS_FUNCTION(value)			isObjType(value, OBJ_FUNCTION)
#define IS_NATIVE(value)			isObjType(value, OBJ_NATIVE)
#define IS_STRING(value)			isObjType(value, OBJ_STRING)
#define IS_STRING_BUILDER(value)	isObjType(value, OBJ_STRING_BUILDER)
#define IS_ARRAY(value)				isObjType(value, OBJ_ARRAY)

#define IS_ARRAY_U8(value)        isObjType(value, OBJ_ARRAY_U8)
#define IS_ARRAY_I8(value)        isObjType(value, OBJ_ARRAY_I8)
#define IS_ARRAY_U16(value)       isObjType(value, OBJ_ARRAY_U16)
#define IS_ARRAY_I16(value)       isObjType(value, OBJ_ARRAY_I16)
#define IS_ARRAY_U32(value)       isObjType(value, OBJ_ARRAY_U32)
#define IS_ARRAY_I32(value)       isObjType(value, OBJ_ARRAY_I32)
#define IS_ARRAY_F32(value)       isObjType(value, OBJ_ARRAY_F32)
#define IS_ARRAY_F64(value)       isObjType(value, OBJ_ARRAY_F64)

#define AS_CLOSURE(value)	((ObjClosure*)AS_OBJ(value))
#define AS_FUNCTION(value)	((ObjFunction*)AS_OBJ(value))
#define AS_NATIVE(value)	(((ObjNative*)AS_OBJ(value))->function)
#define AS_STRING(value)	((ObjString*)AS_OBJ(value))
#define AS_CSTRING(value)	(((ObjString*)AS_OBJ(value))->chars)

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

ObjUpvalue* newUpvalue(Value* slot);
ObjFunction* newFunction();
ObjClosure* newClosure(ObjFunction* function);
ObjNative* newNative(NativeFn function);