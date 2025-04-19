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
	return BOOL_VAL(argCount >= 1 && (IS_STRING(args[0]) || IS_STRING_BUILDER(args[0])));
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
	return BOOL_VAL(argCount >= 1 && (IS_CLOSURE(args[0]) || IS_NATIVE(args[0])));
}

static Value isBooleanNative(int argCount, Value* args) {
	return BOOL_VAL(argCount >= 1 && IS_BOOL(args[0]));
}

static Value typeNative(int argCount, Value* args) {
	if (argCount >= 1) {
		switch (args[0].type) {
		case VAL_BOOL: return OBJ_VAL(vm.typeStrings[TYPE_STRING_BOOL]);
		case VAL_NIL: return OBJ_VAL(vm.typeStrings[TYPE_STRING_NIL]);
		case VAL_NUMBER: return OBJ_VAL(vm.typeStrings[TYPE_STRING_NUMBER]);
		case VAL_OBJ: {
			switch (OBJ_TYPE(args[0])) {
			case OBJ_STRING:
				return OBJ_VAL(vm.typeStrings[TYPE_STRING_STRING]);
			case OBJ_STRING_BUILDER:
				return OBJ_VAL(vm.typeStrings[TYPE_STRING_STRING_BUILDER]);
			case OBJ_CLOSURE:
				return OBJ_VAL(vm.typeStrings[TYPE_STRING_FUNCTION]);
			case OBJ_NATIVE:
				return OBJ_VAL(vm.typeStrings[TYPE_STRING_NATIVE]);
			case OBJ_CLASS:
				return OBJ_VAL(vm.typeStrings[TYPE_STRING_CLASS]);
			case OBJ_INSTANCE:
				return OBJ_VAL(vm.typeStrings[TYPE_STRING_OBJECT]);
			case OBJ_ARRAY:
				return OBJ_VAL(vm.typeStrings[TYPE_STRING_ARRAY]);
			case OBJ_ARRAY_F64:
				return OBJ_VAL(vm.typeStrings[TYPE_STRING_ARRAY_F64]);
			case OBJ_ARRAY_F32:
				return OBJ_VAL(vm.typeStrings[TYPE_STRING_ARRAY_F32]);
			case OBJ_ARRAY_U32:
				return OBJ_VAL(vm.typeStrings[TYPE_STRING_ARRAY_U32]);
			case OBJ_ARRAY_I32:
				return OBJ_VAL(vm.typeStrings[TYPE_STRING_ARRAY_I32]);
			case OBJ_ARRAY_U16:
				return OBJ_VAL(vm.typeStrings[TYPE_STRING_ARRAY_U16]);
			case OBJ_ARRAY_I16:
				return OBJ_VAL(vm.typeStrings[TYPE_STRING_ARRAY_I16]);
			case OBJ_ARRAY_U8:
				return OBJ_VAL(vm.typeStrings[TYPE_STRING_ARRAY_U8]);
			case OBJ_ARRAY_I8:
				return OBJ_VAL(vm.typeStrings[TYPE_STRING_ARRAY_I8]);
			}
		}
		}
	}

	return NIL_VAL;
}

//dont need isNil

COLD_FUNCTION
void importNative_object() {
	defineNative_object("isNumber", isNumberNative);
	defineNative_object("isString", isStringNative);
	defineNative_object("isFunction", isFunctionNative);
	defineNative_object("isClass", isClassNative);
	defineNative_object("isObject", isObjectNative);
	defineNative_object("isArray", isArrayNative);
	defineNative_object("isArrayLike", isArrayLikeNative);
	defineNative_object("isBoolean", isBooleanNative);
	defineNative_object("type", typeNative);
}