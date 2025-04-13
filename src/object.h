/*
 * MIT License
 * Copyright (c) 2025 IMSDcrueoft (https://github.com/IMSDcrueoft)
 * See LICENSE file in the root directory for full license text.
*/
#pragma once
#include "common.h"
#include "value.h"
#include "table.h"
#include "chunk.h"

typedef enum {
	//objects that don't gc
	OBJ_STRING,
	OBJ_NATIVE,
	OBJ_FUNCTION,

	//objects gc able
	OBJ_CLOSURE,
	OBJ_UPVALUE,
	OBJ_CLASS,
	OBJ_INSTANCE,

	//string builder and array are typed arrays
	OBJ_STRING_BUILDER,
	//array
	OBJ_ARRAY,
	OBJ_ARRAY_F64,
	OBJ_ARRAY_F32,
	OBJ_ARRAY_U32,
	OBJ_ARRAY_I32,
	OBJ_ARRAY_U16,
	OBJ_ARRAY_I16,
	OBJ_ARRAY_U8,
	OBJ_ARRAY_I8,
} ObjType;

#if DEBUG_LOG_GC
extern const C_STR objTypeInfo[];
#endif

struct Obj {
	ObjType type;
	bool isMarked;
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
	Value closed; //closed value
	Value* location;
	struct ObjUpvalue* next;
} ObjUpvalue;

typedef struct {
	Obj obj;

	int32_t upvalueCount;
	ObjUpvalue** upvalues;
	ObjFunction* function;
} ObjClosure;

//argCount and argValues
typedef Value(*NativeFn)(int argCount, Value* args);

typedef struct {
	Obj obj;
	NativeFn function;
} ObjNative;

typedef struct {
	Obj obj;
	ObjString* name;
} ObjClass;

typedef struct {
	Obj obj;
	ObjClass* klass;
	Table fields;
} ObjInstance;

#define INVALID_OBJ_STRING_SYMBOL UINT32_MAX
struct ObjString {
	Obj obj;
	uint32_t symbol; // used to boost global hash table
	uint32_t length; // the real length,not include '\0'
	uint64_t hash;
	char chars[]; // flexible array members FAM
};

//begin at 8 and align to 8, when < 64,mul 2, then *1.5 and align 8
typedef struct {
	Obj obj;
	uint32_t capacity;
	uint32_t length;
	char* payload;
} ObjArray;

#define OBJ_TYPE(value)				(AS_OBJ(value)->type)
#define IS_CLOSURE(value)			isObjType(value, OBJ_CLOSURE)
#define IS_FUNCTION(value)			isObjType(value, OBJ_FUNCTION)
#define IS_NATIVE(value)			isObjType(value, OBJ_NATIVE)
#define IS_CLASS(value)				isObjType(value, OBJ_CLASS)
#define IS_INSTANCE(value)			isObjType(value, OBJ_INSTANCE)
#define IS_STRING(value)			isObjType(value, OBJ_STRING)
#define IS_STRING_BUILDER(value)	isObjType(value, OBJ_STRING_BUILDER)
#define IS_ARRAY(value)				isObjType(value, OBJ_ARRAY)

#define ARRAY_IS_TYPE(array, arrayType)		((array->obj.type) == arrayType)
#define ARRAY_ELEMENT(array, type, index)	(((type*)array->payload)[index])
#define ARRAY_IN_RANGE(array, index)		((index >= 0) && (index < array->length))

#define IS_ARRAY_ANY(value)		ARRAY_IS_TYPE(value, OBJ_ARRAY)

#define AS_CLOSURE(value)	((ObjClosure*)AS_OBJ(value))
#define AS_FUNCTION(value)	((ObjFunction*)AS_OBJ(value))
#define AS_CLASS(value)		((ObjClass*)AS_OBJ(value))
#define AS_INSTANCE(value)	((ObjInstance*)AS_OBJ(value))
#define AS_NATIVE(value)	(((ObjNative*)AS_OBJ(value))->function)
#define AS_STRING(value)	((ObjString*)AS_OBJ(value))
#define AS_ARRAY(value)		((ObjArray*)AS_OBJ(value))

static inline bool isObjType(Value value, ObjType type) {
	return IS_OBJ(value) && AS_OBJ(value)->type == type;
}

//array and stringBuilder (not including const string)
static inline bool isArrayLike(Value value) {
	return IS_OBJ(value) && AS_OBJ(value)->type >= OBJ_ARRAY;//enum type
}

static inline bool isIndexableArray(Value value) {
	return IS_OBJ(value) && AS_OBJ(value)->type >= OBJ_STRING_BUILDER;//enum type
}

static inline bool isTypedArray(Value value) {
	return IS_OBJ(value) && (AS_OBJ(value)->type >= OBJ_ARRAY_F64);//enum type
}

ObjString* copyString(C_STR chars, uint32_t length, bool escapeChars);
ObjString* connectString(ObjString* strA, ObjString* strB);

void printObject(Value value, bool isExpand);

Entry* getStringEntryInPool(ObjString* string);
NumberEntry* getNumberEntryInPool(Value* value);

ObjUpvalue* newUpvalue(Value* slot);
ObjFunction* newFunction();
ObjClosure* newClosure(ObjFunction* function);
ObjNative* newNative(NativeFn function);
ObjClass* newClass(ObjString* name);
ObjInstance* newInstance(ObjClass* klass);
ObjArray* newArray(uint64_t size);
ObjArray* newArrayF64(uint64_t size);
ObjArray* newArrayF32(uint64_t size);
ObjArray* newArrayU32(uint64_t size);
ObjArray* newArrayI32(uint64_t size);
ObjArray* newArrayU16(uint64_t size);
ObjArray* newArrayI16(uint64_t size);
ObjArray* newArrayU8(uint64_t size);
ObjArray* newArrayI8(uint64_t size);

void reserveArray(ObjArray* array, uint64_t size);

Value getStringValue(ObjString* string, uint32_t index);
Value getStringBuilderValue(ObjArray* stringBuilder, uint32_t index);
Value getTypedArrayElement(ObjArray* array, uint32_t index);
void setTypedArrayElement(ObjArray* array, uint32_t index, Value val);