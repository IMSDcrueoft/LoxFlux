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

static Value isFunctionNative(int argCount, Value* args) {
	return BOOL_VAL(argCount >= 1 && (IS_CLOSURE(args[0]) || IS_NATIVE(args[0]) || IS_BOUND_METHOD(args[0])));
}

static Value isBooleanNative(int argCount, Value* args) {
	return BOOL_VAL(argCount >= 1 && IS_BOOL(args[0]));
}

static Value objectNative(int argCount, Value* args) {
	return OBJ_VAL(newInstance(&vm.emptyClass));
}

//convert value to stringBuilder
//static Value toStringNative(int argCount, Value* args) {
//	return NIL_VAL;
//}

//convert value to number
//static Value toNumberNative(int argCount, Value* args) {
//	return NIL_VAL;
//}

//dont need isNil

COLD_FUNCTION
void importNative_object() {
	defineNative_object("Object", objectNative);
	defineNative_object("isNumber", isNumberNative);
	defineNative_object("isString", isStringNative);
	defineNative_object("isStringBuilder", isStringBuilderNative);
	defineNative_object("isFunction", isFunctionNative);
	defineNative_object("isClass", isClassNative);
	defineNative_object("isObject", isObjectNative);
	defineNative_object("isArray", isArrayNative);
	defineNative_object("isArrayLike", isArrayLikeNative);
	defineNative_object("isBoolean", isBooleanNative);
}