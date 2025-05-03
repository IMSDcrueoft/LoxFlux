/*
* MIT License
* Copyright (c) 2025 IMSDcrueoft (https://github.com/IMSDcrueoft)
* See LICENSE file in the root directory for full license text.
*/
#include "nativeBuiltin.h"
#include "vm.h"
//ctor

static Value objectNative(int argCount, Value* args) {
	return OBJ_VAL(newInstance(&vm.emptyClass));
}

static Value ArrayNative(int argCount, Value* args) {
	uint32_t length = 0;

	if (argCount >= 1 && IS_NUMBER(args[0])) {
		double size = AS_NUMBER(args[0]);

		//no error
		if (size > 0 && size <= ARRAYLIKE_MAX) {
			length = (uint32_t)size;
		}
		else {
			fprintf(stderr, "Array size overflow\n");
			exit(1);
		}
	}

	ObjArray* array = newArray(OBJ_ARRAY);
	stack_push(OBJ_VAL(array));
	reserveArray(array, length);

	while (array->length < length)
	{
		((Value*)array->payload)[array->length] = NIL_VAL;
		array->length++;
	}

	return OBJ_VAL(array);
}

static Value F64ArrayNative(int argCount, Value* args) {
	uint32_t length = 0;

	if (argCount >= 1 && IS_NUMBER(args[0])) {
		double size = AS_NUMBER(args[0]);

		//no error
		if (size > 0 && size <= ARRAYLIKE_MAX) {
			length = (uint32_t)size;
		}
		else {
			fprintf(stderr, "Array size overflow\n");
			exit(1);
		}
	}

	ObjArray* array = newArray(OBJ_ARRAY_F64);
	stack_push(OBJ_VAL(array));
	reserveArray(array, length);
	uint64_t size = 8ULL * length;
	memset(array->payload, 0, size);//must fit ieee754

	array->length = length;
	return OBJ_VAL(array);
}

static Value F32ArrayNative(int argCount, Value* args) {
	uint32_t length = 0;

	if (argCount >= 1 && IS_NUMBER(args[0])) {
		double size = AS_NUMBER(args[0]);

		if (size > 0 && size <= ARRAYLIKE_MAX) {
			length = (uint32_t)size;
		}
		else {
			fprintf(stderr, "Array size overflow\n");
			exit(1);
		}
	}

	ObjArray* array = newArray(OBJ_ARRAY_F32);
	stack_push(OBJ_VAL(array));
	reserveArray(array, length);
	uint64_t size = 4ULL * length;
	memset(array->payload, 0, size); // must fit ieee754

	array->length = length;
	return OBJ_VAL(array);
}

static Value U32ArrayNative(int argCount, Value* args) {
	uint32_t length = 0;

	if (argCount >= 1 && IS_NUMBER(args[0])) {
		double size = AS_NUMBER(args[0]);

		if (size > 0 && size <= ARRAYLIKE_MAX) {
			length = (uint32_t)size;
		}
		else {
			fprintf(stderr, "Array size overflow\n");
			exit(1);
		}
	}

	ObjArray* array = newArray(OBJ_ARRAY_U32);
	stack_push(OBJ_VAL(array));
	reserveArray(array, length);
	uint64_t size = 4ULL * length;
	memset(array->payload, 0, size);

	array->length = length;
	return OBJ_VAL(array);
}

static Value I32ArrayNative(int argCount, Value* args) {
	uint32_t length = 0;

	if (argCount >= 1 && IS_NUMBER(args[0])) {
		double size = AS_NUMBER(args[0]);

		if (size > 0 && size <= ARRAYLIKE_MAX) {
			length = (uint32_t)size;
		}
		else {
			fprintf(stderr, "Array size overflow\n");
			exit(1);
		}
	}

	ObjArray* array = newArray(OBJ_ARRAY_I32);
	stack_push(OBJ_VAL(array));
	reserveArray(array, length);
	uint64_t size = 4ULL * length;
	memset(array->payload, 0, size);

	array->length = length;
	return OBJ_VAL(array);
}

static Value U16ArrayNative(int argCount, Value* args) {
	uint32_t length = 0;

	if (argCount >= 1 && IS_NUMBER(args[0])) {
		double size = AS_NUMBER(args[0]);

		if (size > 0 && size <= ARRAYLIKE_MAX) {
			length = (uint32_t)size;
		}
		else {
			fprintf(stderr, "Array size overflow\n");
			exit(1);
		}
	}

	ObjArray* array = newArray(OBJ_ARRAY_U16);
	stack_push(OBJ_VAL(array));
	reserveArray(array, length);
	uint64_t size = 2ULL * length;
	memset(array->payload, 0, size);

	array->length = length;
	return OBJ_VAL(array);
}

static Value I16ArrayNative(int argCount, Value* args) {
	uint32_t length = 0;

	if (argCount >= 1 && IS_NUMBER(args[0])) {
		double size = AS_NUMBER(args[0]);

		if (size > 0 && size <= ARRAYLIKE_MAX) {
			length = (uint32_t)size;
		}
		else {
			fprintf(stderr, "Array size overflow\n");
			exit(1);
		}
	}

	ObjArray* array = newArray(OBJ_ARRAY_I16);
	stack_push(OBJ_VAL(array));
	reserveArray(array, length);
	uint64_t size = 2ULL * length;
	memset(array->payload, 0, size);

	array->length = length;
	return OBJ_VAL(array);
}

static Value U8ArrayNative(int argCount, Value* args) {
	uint32_t length = 0;

	if (argCount >= 1 && IS_NUMBER(args[0])) {
		double size = AS_NUMBER(args[0]);

		if (size > 0 && size <= ARRAYLIKE_MAX) {
			length = (uint32_t)size;
		}
		else {
			fprintf(stderr, "Array size overflow\n");
			exit(1);
		}
	}

	ObjArray* array = newArray(OBJ_ARRAY_U8);
	stack_push(OBJ_VAL(array));
	reserveArray(array, length);
	uint64_t size = 1ULL * length;
	memset(array->payload, 0, size);

	array->length = length;
	return OBJ_VAL(array);
}

static Value I8ArrayNative(int argCount, Value* args) {
	uint32_t length = 0;

	if (argCount >= 1 && IS_NUMBER(args[0])) {
		double size = AS_NUMBER(args[0]);

		if (size > 0 && size <= ARRAYLIKE_MAX) {
			length = (uint32_t)size;
		}
		else {
			fprintf(stderr, "Array size overflow\n");
			exit(1);
		}
	}

	ObjArray* array = newArray(OBJ_ARRAY_I8);
	stack_push(OBJ_VAL(array));
	reserveArray(array, length);
	uint64_t size = 1ULL * length;
	memset(array->payload, 0, size);

	array->length = length;
	return OBJ_VAL(array);
}

static uint32_t calculateBuilderCapacity(uint64_t initialLength) {
	uint64_t capacity = initialLength + 1;
	if (capacity > ARRAYLIKE_MAX) {
		fprintf(stderr, "StringBuilder size overflow");
		exit(1);
	}
	if (capacity < 64) {
		capacity = max(16, ((capacity * 2) + 7) & ~7);
	}
	else {
		capacity = (((capacity * 3) >> 1) + 7) & ~7;
	}
	return min(ARRAYLIKE_MAX, capacity);
}

//The builder accepts one parameter string/stringBuilder as the initial value
static Value stringBuilderNative(int argCount, Value* args) {
	ObjArray* stringBuilder = newArray(OBJ_STRING_BUILDER);
	stack_push(OBJ_VAL(stringBuilder));

	//Select an adaptation scheme based on the type
	if (argCount >= 1) {
		C_STR stringPtr = NULL;
		uint32_t length = 0;

		if (IS_STRING(args[0])) {
			ObjString* string = AS_STRING(args[0]);
			stringPtr = &string->chars[0];
			length = string->length;
		}
		else if (IS_STRING_BUILDER(args[0])) {
			ObjArray* string = AS_ARRAY(args[0]);
			stringPtr = string->payload;
			length = string->length;
		}

		if (stringPtr != NULL) {
			uint32_t capacity = calculateBuilderCapacity(length);
			stringBuilder->length = length;
			stringBuilder->capacity = capacity;
			stringBuilder->payload = ALLOCATE(char, capacity);
			memcpy(stringBuilder->payload, stringPtr, length);
			ARRAY_ELEMENT(stringBuilder, char, stringBuilder->length) = '\0';
		}
	}

	if (stringBuilder->capacity == 0) {
		stringBuilder->length = 0;
		stringBuilder->capacity = 16;
		stringBuilder->payload = ALLOCATE(char, 16);
		ARRAY_ELEMENT(stringBuilder, char, stringBuilder->length) = '\0';
	}

	return OBJ_VAL(stringBuilder);
}

COLD_FUNCTION
void importNative_ctor() {
	defineNative_ctor("Object", objectNative);

	defineNative_ctor("Array", ArrayNative);
	defineNative_ctor("F64Array", F64ArrayNative);
	defineNative_ctor("F32Array", F32ArrayNative);
	defineNative_ctor("U32Array", U32ArrayNative);
	defineNative_ctor("I32Array", I32ArrayNative);
	defineNative_ctor("U16Array", U16ArrayNative);
	defineNative_ctor("I16Array", I16ArrayNative);
	defineNative_ctor("U8Array", U8ArrayNative);
	defineNative_ctor("I8Array", I8ArrayNative);

	defineNative_ctor("StringBuilder", stringBuilderNative);
}