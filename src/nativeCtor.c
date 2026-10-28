/*
* MIT License
* Copyright (c) 2025-2026 IMSDcrueoft (https://github.com/IMSDcrueoft)
* See LICENSE file in the root directory for full license text.
*/
#include "nativeBuiltin.h"
#include "vm.h"
//ctor

static LfxValue objectNative(LfxVM* vm, int argCount, LfxValue* args) {
	return OBJ_VAL(newInstance(&vm->defaultClass));
}

static LfxValue ArrayNative(LfxVM* vm, int argCount, LfxValue* args) {
	(void)vm;
	uint32_t length = 0;

	if (argCount >= 1 && IS_NUMBER(args[0])) {
		double size = AS_NUMBER(args[0]);

		if (size > ARRAYLIKE_MAX) {
			fprintf(stderr, "Array size overflow\n");
			exit(1);
		}
		else if (size > 0) {
			length = (uint32_t)size;
		}
		else if (size < 0) {
			return NIL_VAL;
		}
		//size == 0 -> empty array
	}
	else if (argCount >= 1) {
		return NIL_VAL;//not a number
	}

	ObjArray* array = newArray(OBJ_ARRAY);
	stack_push(vm, OBJ_VAL(array));
	reserveArray(array, length);

	while (array->length < length)
	{
		((LfxValue*)array->payload)[array->length] = NIL_VAL;
		array->length++;
	}

	return OBJ_VAL(array);
}

static LfxValue F64ArrayNative(LfxVM* vm, int argCount, LfxValue* args) {
	(void)vm;
	uint32_t length = 0;

	if (argCount >= 1 && IS_NUMBER(args[0])) {
		double size = AS_NUMBER(args[0]);

		if (size > ARRAYLIKE_MAX) {
			fprintf(stderr, "Array size overflow\n");
			exit(1);
		}
		else if (size > 0) {
			length = (uint32_t)size;
		}
		else if (size < 0) {
			return NIL_VAL;
		}
		//size == 0 -> empty array
	}
	else if (argCount >= 1) {
		return NIL_VAL;//not a number
	}

	ObjArray* array = newArray(OBJ_ARRAY_F64);
	stack_push(vm, OBJ_VAL(array));
	reserveArray(array, length);
	uint64_t size = 8ULL * length;
	memset(array->payload, 0, size);//must fit ieee754

	array->length = length;
	return OBJ_VAL(array);
}

static LfxValue F32ArrayNative(LfxVM* vm, int argCount, LfxValue* args) {
	(void)vm;
	uint32_t length = 0;

	if (argCount >= 1 && IS_NUMBER(args[0])) {
		double size = AS_NUMBER(args[0]);

		if (size > ARRAYLIKE_MAX) {
			fprintf(stderr, "Array size overflow\n");
			exit(1);
		}
		else if (size > 0) {
			length = (uint32_t)size;
		}
		else if (size < 0) {
			return NIL_VAL;
		}
		//size == 0 -> empty array
	}
	else if (argCount >= 1) {
		return NIL_VAL;//not a number
	}

	ObjArray* array = newArray(OBJ_ARRAY_F32);
	stack_push(vm, OBJ_VAL(array));
	reserveArray(array, length);
	uint64_t size = 4ULL * length;
	memset(array->payload, 0, size); // must fit ieee754

	array->length = length;
	return OBJ_VAL(array);
}

static LfxValue U32ArrayNative(LfxVM* vm, int argCount, LfxValue* args) {
	(void)vm;
	uint32_t length = 0;

	if (argCount >= 1 && IS_NUMBER(args[0])) {
		double size = AS_NUMBER(args[0]);

		if (size > ARRAYLIKE_MAX) {
			fprintf(stderr, "Array size overflow\n");
			exit(1);
		}
		else if (size > 0) {
			length = (uint32_t)size;
		}
		else if (size < 0) {
			return NIL_VAL;
		}
		//size == 0 -> empty array
	}
	else if (argCount >= 1) {
		return NIL_VAL;//not a number
	}

	ObjArray* array = newArray(OBJ_ARRAY_U32);
	stack_push(vm, OBJ_VAL(array));
	reserveArray(array, length);
	uint64_t size = 4ULL * length;
	memset(array->payload, 0, size);

	array->length = length;
	return OBJ_VAL(array);
}

static LfxValue I32ArrayNative(LfxVM* vm, int argCount, LfxValue* args) {
	(void)vm;
	uint32_t length = 0;

	if (argCount >= 1 && IS_NUMBER(args[0])) {
		double size = AS_NUMBER(args[0]);

		if (size > ARRAYLIKE_MAX) {
			fprintf(stderr, "Array size overflow\n");
			exit(1);
		}
		else if (size > 0) {
			length = (uint32_t)size;
		}
		else if (size < 0) {
			return NIL_VAL;
		}
		//size == 0 -> empty array
	}
	else if (argCount >= 1) {
		return NIL_VAL;//not a number
	}

	ObjArray* array = newArray(OBJ_ARRAY_I32);
	stack_push(vm, OBJ_VAL(array));
	reserveArray(array, length);
	uint64_t size = 4ULL * length;
	memset(array->payload, 0, size);

	array->length = length;
	return OBJ_VAL(array);
}

static LfxValue U16ArrayNative(LfxVM* vm, int argCount, LfxValue* args) {
	(void)vm;
	uint32_t length = 0;

	if (argCount >= 1 && IS_NUMBER(args[0])) {
		double size = AS_NUMBER(args[0]);

		if (size > ARRAYLIKE_MAX) {
			fprintf(stderr, "Array size overflow\n");
			exit(1);
		}
		else if (size > 0) {
			length = (uint32_t)size;
		}
		else if (size < 0) {
			return NIL_VAL;
		}
		//size == 0 -> empty array
	}
	else if (argCount >= 1) {
		return NIL_VAL;//not a number
	}

	ObjArray* array = newArray(OBJ_ARRAY_U16);
	stack_push(vm, OBJ_VAL(array));
	reserveArray(array, length);
	uint64_t size = 2ULL * length;
	memset(array->payload, 0, size);

	array->length = length;
	return OBJ_VAL(array);
}

static LfxValue I16ArrayNative(LfxVM* vm, int argCount, LfxValue* args) {
	(void)vm;
	uint32_t length = 0;

	if (argCount >= 1 && IS_NUMBER(args[0])) {
		double size = AS_NUMBER(args[0]);

		if (size > ARRAYLIKE_MAX) {
			fprintf(stderr, "Array size overflow\n");
			exit(1);
		}
		else if (size > 0) {
			length = (uint32_t)size;
		}
		else if (size < 0) {
			return NIL_VAL;
		}
		//size == 0 -> empty array
	}
	else if (argCount >= 1) {
		return NIL_VAL;//not a number
	}

	ObjArray* array = newArray(OBJ_ARRAY_I16);
	stack_push(vm, OBJ_VAL(array));
	reserveArray(array, length);
	uint64_t size = 2ULL * length;
	memset(array->payload, 0, size);

	array->length = length;
	return OBJ_VAL(array);
}

static LfxValue U8ArrayNative(LfxVM* vm, int argCount, LfxValue* args) {
	(void)vm;
	uint32_t length = 0;

	if (argCount >= 1 && IS_NUMBER(args[0])) {
		double size = AS_NUMBER(args[0]);

		if (size > ARRAYLIKE_MAX) {
			fprintf(stderr, "Array size overflow\n");
			exit(1);
		}
		else if (size > 0) {
			length = (uint32_t)size;
		}
		else if (size < 0) {
			return NIL_VAL;
		}
		//size == 0 -> empty array
	}
	else if (argCount >= 1) {
		return NIL_VAL;//not a number
	}

	ObjArray* array = newArray(OBJ_ARRAY_U8);
	stack_push(vm, OBJ_VAL(array));
	reserveArray(array, length);
	uint64_t size = 1ULL * length;
	memset(array->payload, 0, size);

	array->length = length;
	return OBJ_VAL(array);
}

static LfxValue I8ArrayNative(LfxVM* vm, int argCount, LfxValue* args) {
	(void)vm;
	uint32_t length = 0;

	if (argCount >= 1 && IS_NUMBER(args[0])) {
		double size = AS_NUMBER(args[0]);

		if (size > ARRAYLIKE_MAX) {
			fprintf(stderr, "Array size overflow\n");
			exit(1);
		}
		else if (size > 0) {
			length = (uint32_t)size;
		}
		else if (size < 0) {
			return NIL_VAL;
		}
		//size == 0 -> empty array
	}
	else if (argCount >= 1) {
		return NIL_VAL;//not a number
	}

	ObjArray* array = newArray(OBJ_ARRAY_I8);
	stack_push(vm, OBJ_VAL(array));
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
	return (uint32_t)min(ARRAYLIKE_MAX, capacity);
}

//The builder accepts one parameter string/stringBuilder as the initial value
static LfxValue stringBuilderNative(LfxVM* vm, int argCount, LfxValue* args) {
	(void)vm;
	ObjArray* stringBuilder = newArray(OBJ_STRING_BUILDER);
	stack_push(vm, OBJ_VAL(stringBuilder));

	//Select an adaptation scheme based on the type
	if (argCount >= 1) {
		C_STR stringPtr = NULL;
		uint32_t length = 0;

		if (IS_STRING(args[0])) {
			ObjString* string = AS_STRING(args[0]);
			stringPtr = string->chars;
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
void importNative_ctor(LfxVM* vm) {
	defineNative_ctor(vm, "Object", objectNative);

	defineNative_ctor(vm, "Array", ArrayNative);
	defineNative_ctor(vm, "F64Array", F64ArrayNative);
	defineNative_ctor(vm, "F32Array", F32ArrayNative);
	defineNative_ctor(vm, "U32Array", U32ArrayNative);
	defineNative_ctor(vm, "I32Array", I32ArrayNative);
	defineNative_ctor(vm, "U16Array", U16ArrayNative);
	defineNative_ctor(vm, "I16Array", I16ArrayNative);
	defineNative_ctor(vm, "U8Array", U8ArrayNative);
	defineNative_ctor(vm, "I8Array", I8ArrayNative);

	defineNative_ctor(vm, "StringBuilder", stringBuilderNative);
}
