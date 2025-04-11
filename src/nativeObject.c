/*
* MIT License
* Copyright (c) 2025 IM&SD (https://github.com/IMSDcrueoft)
* See LICENSE file in the root directory for full license text.
*/
#include "nativeBuiltin.h"
#include "vm.h"
//Object
static Value instanceOfNative(int argCount, Value* args)
{
	bool isInstance = (argCount >= 2) && (IS_INSTANCE(args[0]) && IS_CLASS(args[1])) && (AS_INSTANCE(args[0])->klass == AS_CLASS(args[1]));
	return BOOL_VAL(isInstance);
}

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

//dont need isNil or isBool

COLD_FUNCTION
void importNative_object() {
	defineNative_object("instanceOf", instanceOfNative);
	defineNative_object("isClass", isClassNative);
	defineNative_object("isObject", isObjectNative);
	defineNative_object("isString", isStringNative);
	defineNative_object("isNumber", isNumberNative);
}