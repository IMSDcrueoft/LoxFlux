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

static Value isNumberNative(int argCount, Value* args)
{
	return BOOL_VAL(argCount >= 1 && IS_NUMBER(args[0]));
}

static Value isArrayNative(int argCount, Value* args) {
	return BOOL_VAL(argCount >= 1 && isArrayLike(args[0]));
}

//dont need isNil or isBool

COLD_FUNCTION
void importNative_object() {
	defineNative_object("isClass", isClassNative);
	defineNative_object("isObject", isObjectNative);
	defineNative_object("isString", isStringNative);
	defineNative_object("isNumber", isNumberNative);
	defineNative_object("isArray", isArrayNative);
}