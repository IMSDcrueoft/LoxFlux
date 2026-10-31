/*
* MIT License
* Copyright (c) 2025-2026 IMSDcrueoft (https://github.com/IMSDcrueoft)
* See LICENSE file in the root directory for full license text.
*/
#include "nativeBuiltin.h"
#include "vm.h"
//Object
static LfxValue isClassNative(LfxVM* vm, int argCount, LfxValue* args)
{
	(void)vm;
	return BOOL_VAL(argCount >= 1 && IS_CLASS(args[0]));
}

//object is instance to user
static LfxValue isObjectNative(LfxVM* vm, int argCount, LfxValue* args)
{
	(void)vm;
	return BOOL_VAL(argCount >= 1 && IS_INSTANCE(args[0]));
}

static LfxValue isStringNative(LfxVM* vm, int argCount, LfxValue* args)
{
	(void)vm;
	return BOOL_VAL(argCount >= 1 && IS_STRING(args[0]));
}

static LfxValue isStringBuilderNative(LfxVM* vm, int argCount, LfxValue* args)
{
	(void)vm;
	return BOOL_VAL(argCount >= 1 && IS_STRING_BUILDER(args[0]));
}

static LfxValue isNumberNative(LfxVM* vm, int argCount, LfxValue* args)
{
	(void)vm;
	return BOOL_VAL(argCount >= 1 && IS_NUMBER(args[0]));
}

static LfxValue isArrayNative(LfxVM* vm, int argCount, LfxValue* args) {
	(void)vm;
	return BOOL_VAL(argCount >= 1 && IS_ARRAY(args[0]));
}

static LfxValue isArrayLikeNative(LfxVM* vm, int argCount, LfxValue* args) {
	(void)vm;
	return BOOL_VAL(argCount >= 1 && isArrayLike(args[0]));
}

static LfxValue isTypedArrayNative(LfxVM* vm, int argCount, LfxValue* args) {
	(void)vm;
	return BOOL_VAL(argCount >= 1 && isTypedArray(args[0]));
}

static LfxValue isFunctionNative(LfxVM* vm, int argCount, LfxValue* args) {
	(void)vm;
	return BOOL_VAL(argCount >= 1 && (IS_CLOSURE(args[0]) || IS_NATIVE(args[0]) || IS_BOUND_METHOD(args[0])));
}

static LfxValue isBooleanNative(LfxVM* vm, int argCount, LfxValue* args) {
	(void)vm;
	return BOOL_VAL(argCount >= 1 && IS_BOOL(args[0]));
}

static LfxValue isFiberNative(LfxVM* vm, int argCount, LfxValue* args) {
	(void)vm;
	return BOOL_VAL(argCount >= 1 && IS_FIBER(args[0]));
}

//returns the class object of an instance; pairs with the class-level
//instanceof semantics: getClass(a) instanceof getClass(b) is a dynamic
//subclass check. non-instances have no class -> nil
static LfxValue getClassNative(LfxVM* vm, int argCount, LfxValue* args) {
	(void)vm;
	if (argCount < 1 || !IS_INSTANCE(args[0])) {
		return NIL_VAL;
	}

	ObjClass* klass = AS_INSTANCE(args[0])->klass;
	return OBJ_VAL(klass);
}

static LfxValue getGlobalNative(LfxVM* vm, int argCount, LfxValue* args) {
	return OBJ_VAL(&vm->globals);
}

//JS-like Object.freeze, instance fields only (shallow): no add/modify/delete
//after freezing. The global object is excluded: it backs the VM's variable
//store, so freezing it would brick every global definition
static LfxValue freezeNative(LfxVM* vm, int argCount, LfxValue* args) {
	(void)vm;
	if (argCount < 1 || !IS_INSTANCE(args[0])) {
		fprintf(stderr, "freeze() expects an instance.\n");
		return NIL_VAL;
	}

	ObjInstance* instance = AS_INSTANCE(args[0]);

	if (instance->fields.isGlobal) {
		fprintf(stderr, "Cannot freeze the global object.\n");
		return NIL_VAL;
	}

	instance->fields.isFrozen = true;
	return args[0];//JS-style: returns the frozen object
}

//JS-like Object.isFrozen, restricted to what freeze() can freeze:
//only instances carry a frozen state, every other value is just "not frozen"
static LfxValue isFrozenNative(LfxVM* vm, int argCount, LfxValue* args) {
	(void)vm;
	if (argCount < 1 || !IS_INSTANCE(args[0])) {
		return FALSE_VAL;
	}

	return BOOL_VAL(AS_INSTANCE(args[0])->fields.isFrozen);
}

COLD_FUNCTION
static LfxValue keysNative(LfxVM* vm, int argCount, LfxValue* args) {
	// Create an empty array as result
	ObjArray* result = newArray(vm, OBJ_ARRAY);
	stack_push(vm, OBJ_VAL(result));

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
				reserveArray(vm, result, newCapacity);
			}

			// Add key to result array  
			ARRAY_ELEMENT(result, LfxValue, result->length) = OBJ_VAL(entry->key);
			result->length++;
		}
	}

	return OBJ_VAL(result);
}

//pure predicate: any type mismatch is simply "not an own property"
//own property = data field only (methods live on the klass, not the instance)
static LfxValue hasOwnNative(LfxVM* vm, int argCount, LfxValue* args) {
	(void)vm;
	if (argCount < 2 || !IS_INSTANCE(args[0]) || !IS_STRING(args[1])) {
		return FALSE_VAL;
	}

	ObjInstance* instance = AS_INSTANCE(args[0]);
	LfxValue dummy;
	return BOOL_VAL(tableGet(&instance->fields, AS_STRING(args[1]), &dummy));
}

COLD_FUNCTION
void importNative_object(LfxVM* vm) {
	defineNative_object(vm, "isNumber", isNumberNative);
	defineNative_object(vm, "isString", isStringNative);
	defineNative_object(vm, "isStringBuilder", isStringBuilderNative);
	defineNative_object(vm, "isFunction", isFunctionNative);
	defineNative_object(vm, "isClass", isClassNative);
	defineNative_object(vm, "isObject", isObjectNative);
	defineNative_object(vm, "isArray", isArrayNative);
	defineNative_object(vm, "isTypedArray", isTypedArrayNative);
	defineNative_object(vm, "isArrayLike", isArrayLikeNative);
	defineNative_object(vm, "isBoolean", isBooleanNative);
	defineNative_object(vm, "isFiber", isFiberNative);
	defineNative_object(vm, "getClass", getClassNative);
	defineNative_object(vm, "getGlobal", getGlobalNative);
	defineNative_object(vm, "keys", keysNative);
	defineNative_object(vm, "hasOwn", hasOwnNative);
	defineNative_object(vm, "freeze", freezeNative);
	defineNative_object(vm, "isFrozen", isFrozenNative);
}
