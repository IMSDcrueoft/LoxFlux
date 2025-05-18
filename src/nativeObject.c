/*
* MIT License
* Copyright (c) 2025 IMSDcrueoft (https://github.com/IMSDcrueoft)
* See LICENSE file in the root directory for full license text.
*/
#include "nativeBuiltin.h"
#include "vm.h"
//Object
static Value isClassNative(int argCount, Value* args)
{
	return BOOL_VAL(argCount >= 1 && IS_CLASS(args[0]));
}

//object is instance to user
static Value isObjectNative(int argCount, Value* args)
{
	return BOOL_VAL(argCount >= 1 && IS_INSTANCE(args[0]));
}

static Value isStringNative(int argCount, Value* args)
{
	return BOOL_VAL(argCount >= 1 && IS_STRING(args[0]));
}

static Value isStringBuilderNative(int argCount, Value* args)
{
	return BOOL_VAL(argCount >= 1 && IS_STRING_BUILDER(args[0]));
}

static Value isNumberNative(int argCount, Value* args)
{
	return BOOL_VAL(argCount >= 1 && IS_NUMBER(args[0]));
}

static Value isArrayNative(int argCount, Value* args) {
	return BOOL_VAL(argCount >= 1 && IS_ARRAY(args[0]));
}

static Value isArrayLikeNative(int argCount, Value* args) {
	return BOOL_VAL(argCount >= 1 && isArrayLike(args[0]));
}

static Value isTypedArrayNative(int argCount, Value* args) {
	return BOOL_VAL(argCount >= 1 && isTypedArray(args[0]));
}

static Value isFunctionNative(int argCount, Value* args) {
	return BOOL_VAL(argCount >= 1 && (IS_CLOSURE(args[0]) || IS_NATIVE(args[0]) || IS_BOUND_METHOD(args[0])));
}

static Value isBooleanNative(int argCount, Value* args) {
	return BOOL_VAL(argCount >= 1 && IS_BOOL(args[0]));
}

static Value getGlobalNative(int argCount, Value* args) {
	if (argCount >= 1) {
		if (IS_STRING(args[0])) {
			ObjString* name = AS_STRING(args[0]);
			//inline cache make no sense here
			Value value;
			if (tableGet(&vm.globals, name, &value)) {
				return value;
			}
		}
	}

	return NIL_VAL;
}

static Value setGlobalNative(int argCount, Value* args) {
	if (argCount >= 1) {
		if (IS_STRING(args[0])) {
			ObjString* name = AS_STRING(args[0]);
			Value value = (argCount >= 2) ? args[1] : NIL_VAL;
			//inline cache make no sense here
			tableSet(&vm.globals, name, value);
			return BOOL_VAL(true);
		}
	}

	return BOOL_VAL(false);
}

COLD_FUNCTION
static Value keysNative(int argCount, Value* args) {
	// Create an empty array as result  
	ObjArray* result = newArray(OBJ_ARRAY);
	stack_push(OBJ_VAL(result));

	// Check argument count  
	if (argCount == 0 || !IS_INSTANCE(args[0])) {
		fprintf(stderr, "keys() expects an instance as first argument.");
		return OBJ_VAL(result);
	}

	// Get instance object  
	ObjInstance* instance = AS_INSTANCE(args[0]);

	// Iterate through the instance's fields table  
	for (uint32_t i = 0; i < instance->fields.capacity; i++) {
		Entry* entry = &instance->fields.entries[i];
		if (entry->key != NULL) {
			// Check if we need to grow the array  
			uint64_t newSize = result->length + 1;

			if (newSize > result->capacity) {
				uint64_t newCapacity = max((result->capacity < 64) ? (result->capacity * 2) : ((result->capacity * 3) >> 1), 8);
				reserveArray(result, newCapacity);
			}

			// Add key to result array  
			ARRAY_ELEMENT(result, Value, result->length) = OBJ_VAL(entry->key);
			result->length++;
		}
	}

	return OBJ_VAL(result);
}

COLD_FUNCTION
void importNative_object() {
	defineNative_object("isNumber", isNumberNative);
	defineNative_object("isString", isStringNative);
	defineNative_object("isStringBuilder", isStringBuilderNative);
	defineNative_object("isFunction", isFunctionNative);
	defineNative_object("isClass", isClassNative);
	defineNative_object("isObject", isObjectNative);
	defineNative_object("isArray", isArrayNative);
	defineNative_object("isTypedArray", isTypedArrayNative);
	defineNative_object("isArrayLike", isArrayLikeNative);
	defineNative_object("isBoolean", isBooleanNative);

	defineNative_object("getGlobal", getGlobalNative);
	defineNative_object("setGlobal", setGlobalNative);

	defineNative_object("keys", keysNative);
}