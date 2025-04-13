/*
* MIT License
* Copyright (c) 2025 IMSDcrueoft (https://github.com/IMSDcrueoft)
* See LICENSE file in the root directory for full license text.
*/
#include "nativeBuiltin.h"
#include "vm.h"
#include "object.h"
//Array

static Value lengthNative(int argCount, Value* args)
{
	if (argCount >= 1 && isArrayLike(args[0])) {
		return NUMBER_VAL(AS_ARRAY(args[0])->length);
	}
	else {
		return NAN_VAL;
	}
}

static Value pushNative(int argCount, Value* args) {
	//don't solve stringBuilder
	if (argCount >= 1 && isArrayLike(args[0])) {
		ObjArray* array = AS_ARRAY(args[0]);

		if (argCount >= 2) {
			uint64_t newSize = (uint64_t)array->length + argCount - 1;

			if (newSize > UINT32_MAX) {
				fprintf(stderr, "Array size overflow\n");
				exit(1);
			}

			newSize = (newSize + 7) & ~7; //align to 8

			//check size
			if (newSize > array->capacity) {
				if (array->capacity < 64) {
					newSize = max(newSize, array->capacity * 2);
				}
				else {
					newSize = max(newSize, (array->capacity * 3) >> 1);
				}

				reserveArray(array, newSize);
			}

			if (IS_ARRAY_ANY(array)) {
				//no type check,so it's faster
				for (uint32_t i = 1; i < argCount; ++i) {
					ARRAY_ELEMENT(array, Value, array->length) = args[i];
					array->length++;
				}
			}
			else {//typed array
				for (uint32_t i = 1; i < argCount; ++i) {
					Value val = IS_NUMBER(args[i]) ? args[i] : NUMBER_VAL(0);

					switch (array->obj.type) {
					case OBJ_ARRAY_F64:
						ARRAY_ELEMENT(array, double, array->length) = AS_NUMBER(val);
						break;
					case OBJ_ARRAY_F32:
						ARRAY_ELEMENT(array, float, array->length) = AS_NUMBER(val);
						break;
					case OBJ_ARRAY_U32:
						ARRAY_ELEMENT(array, uint32_t, array->length) = AS_NUMBER(val);
						break;
					case OBJ_ARRAY_I32:
						ARRAY_ELEMENT(array, int32_t, array->length) = AS_NUMBER(val);
						break;
					case OBJ_ARRAY_U16:
						ARRAY_ELEMENT(array, uint16_t, array->length) = AS_NUMBER(val);
						break;
					case OBJ_ARRAY_I16:
						ARRAY_ELEMENT(array, int16_t, array->length) = AS_NUMBER(val);
						break;
					case OBJ_ARRAY_U8:
						ARRAY_ELEMENT(array, uint8_t, array->length) = AS_NUMBER(val);
						break;
					case OBJ_ARRAY_I8:
						ARRAY_ELEMENT(array, int8_t, array->length) = AS_NUMBER(val);
						break;
					}

					array->length++;
				}
			}
		}

		return NUMBER_VAL(array->length);
	}
	else {
		return NAN_VAL;
	}
}

static Value popNative(int argCount, Value* args) {
	//don't solve stringBuilder
	if (argCount >= 1 && isArrayLike(args[0])) {
		ObjArray* array = AS_ARRAY(args[0]);

		if (array->length > 0) {
			Value value;
			if (IS_ARRAY_ANY(array)) {
				value = ARRAY_ELEMENT(array, Value, array->length - 1);
			}
			else {
				value = getTypedArrayElement(array, array->length - 1);
			}
			array->length--;
			return value;
		}
		else { //default return nil
			return NIL_VAL;
		}
	}
	else {
		return NIL_VAL;
	}
}

//resize the array,user don't care reserve ,this is not cpp vector
static Value resizeNative(int argCount, Value* args) {
	if (argCount >= 2 && isArrayLike(args[0]) && IS_NUMBER(args[1])) {
		ObjArray* array = AS_ARRAY(args[0]);

		uint64_t length = 0;
		double size = AS_NUMBER(args[1]);

		//no error
		if (size > 0 && size <= UINT32_MAX) {
			length = (uint64_t)size;

			if (length > array->length) {
				reserveArray(array, length);

				if (IS_ARRAY_ANY(array)) {
					//no type check,so it's faster
					while (array->length < length) {
						ARRAY_ELEMENT(array, Value, array->length) = NIL_VAL;
						array->length++;
					}
				}
				else {//typed array
					switch (array->obj.type) {
					case OBJ_ARRAY_F64: {//must fit ieee754
						char* beginPtr = &ARRAY_ELEMENT(array, double, array->length);
						uint64_t size = 8ULL * (length - array->length);
						memset(beginPtr, 0, size);
						break;
					}
					case OBJ_ARRAY_F32://must fit ieee754
					case OBJ_ARRAY_U32:
					case OBJ_ARRAY_I32: {
						char* beginPtr = &ARRAY_ELEMENT(array, uint32_t, array->length);
						uint64_t size = 4ULL * (length - array->length);
						memset(beginPtr, 0, size);
						break;
					}
					case OBJ_ARRAY_U16:
					case OBJ_ARRAY_I16: {
						char* beginPtr = &ARRAY_ELEMENT(array, uint16_t, array->length);
						uint64_t size = 2ULL * (length - array->length);
						memset(beginPtr, 0, size);
						break;
					}
					case OBJ_ARRAY_U8:
					case OBJ_ARRAY_I8: {
						char* beginPtr = &ARRAY_ELEMENT(array, uint8_t, array->length);
						uint64_t size = 1ULL * (length - array->length);
						memset(beginPtr, 0, size);
						break;
					}
					}

					array->length = length;
				}
			}
			else {
				array->length = length;//and do nothing
			}

			return BOOL_VAL(true);
		}
		else {
			fprintf(stderr, "Array size overflow\n");
			exit(1);
		}
	}

	return BOOL_VAL(false);
}

static Value ArrayNative(int argCount, Value* args) {
	uint32_t length = 0;

	if (argCount >= 1 && IS_NUMBER(args[0])) {
		double size = AS_NUMBER(args[0]);

		//no error
		if (size > 0 && size <= UINT32_MAX) {
			length = (uint32_t)size;
		}
		else {
			fprintf(stderr, "Array size overflow\n");
			exit(1);
		}
	}

	ObjArray* array = newArray(length);
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
		if (size > 0 && size <= UINT32_MAX) {
			length = (uint32_t)size;
		}
		else {
			fprintf(stderr, "Array size overflow\n");
			exit(1);
		}
	}

	ObjArray* array = newArrayF64(length);
	uint64_t size = 8ULL * length;
	memset(array->payload, 0, size);//must fit ieee754

	array->length = length;
	return OBJ_VAL(array);
}

static Value F32ArrayNative(int argCount, Value* args) {
	uint32_t length = 0;

	if (argCount >= 1 && IS_NUMBER(args[0])) {
		double size = AS_NUMBER(args[0]);

		if (size > 0 && size <= UINT32_MAX) {
			length = (uint32_t)size;
		}
		else {
			fprintf(stderr, "Array size overflow\n");
			exit(1);
		}
	}

	ObjArray* array = newArrayF32(length);
	uint64_t size = 4ULL * length;
	memset(array->payload, 0, size); // must fit ieee754

	array->length = length;
	return OBJ_VAL(array);
}

static Value U32ArrayNative(int argCount, Value* args) {
	uint32_t length = 0;

	if (argCount >= 1 && IS_NUMBER(args[0])) {
		double size = AS_NUMBER(args[0]);

		if (size > 0 && size <= UINT32_MAX) {
			length = (uint32_t)size;
		}
		else {
			fprintf(stderr, "Array size overflow\n");
			exit(1);
		}
	}

	ObjArray* array = newArrayU32(length);
	uint64_t size = 4ULL * length;
	memset(array->payload, 0, size);

	array->length = length;
	return OBJ_VAL(array);
}

static Value I32ArrayNative(int argCount, Value* args) {
	uint32_t length = 0;

	if (argCount >= 1 && IS_NUMBER(args[0])) {
		double size = AS_NUMBER(args[0]);

		if (size > 0 && size <= UINT32_MAX) {
			length = (uint32_t)size;
		}
		else {
			fprintf(stderr, "Array size overflow\n");
			exit(1);
		}
	}

	ObjArray* array = newArrayI32(length);
	uint64_t size = 4ULL * length;
	memset(array->payload, 0, size);

	array->length = length;
	return OBJ_VAL(array);
}

static Value U16ArrayNative(int argCount, Value* args) {
	uint32_t length = 0;

	if (argCount >= 1 && IS_NUMBER(args[0])) {
		double size = AS_NUMBER(args[0]);

		if (size > 0 && size <= UINT32_MAX) {
			length = (uint32_t)size;
		}
		else {
			fprintf(stderr, "Array size overflow\n");
			exit(1);
		}
	}

	ObjArray* array = newArrayU16(length);
	uint64_t size = 2ULL * length;
	memset(array->payload, 0, size);

	array->length = length;
	return OBJ_VAL(array);
}

static Value I16ArrayNative(int argCount, Value* args) {
	uint32_t length = 0;

	if (argCount >= 1 && IS_NUMBER(args[0])) {
		double size = AS_NUMBER(args[0]);

		if (size > 0 && size <= UINT32_MAX) {
			length = (uint32_t)size;
		}
		else {
			fprintf(stderr, "Array size overflow\n");
			exit(1);
		}
	}

	ObjArray* array = newArrayI16(length);
	uint64_t size = 2ULL * length;
	memset(array->payload, 0, size);

	array->length = length;
	return OBJ_VAL(array);
}

static Value U8ArrayNative(int argCount, Value* args) {
	uint32_t length = 0;

	if (argCount >= 1 && IS_NUMBER(args[0])) {
		double size = AS_NUMBER(args[0]);

		if (size > 0 && size <= UINT32_MAX) {
			length = (uint32_t)size;
		}
		else {
			fprintf(stderr, "Array size overflow\n");
			exit(1);
		}
	}

	ObjArray* array = newArrayU8(length);
	uint64_t size = 1ULL * length;
	memset(array->payload, 0, size);

	array->length = length;
	return OBJ_VAL(array);
}

static Value I8ArrayNative(int argCount, Value* args) {
	uint32_t length = 0;

	if (argCount >= 1 && IS_NUMBER(args[0])) {
		double size = AS_NUMBER(args[0]);

		if (size > 0 && size <= UINT32_MAX) {
			length = (uint32_t)size;
		}
		else {
			fprintf(stderr, "Array size overflow\n");
			exit(1);
		}
	}

	ObjArray* array = newArrayI8(length);
	uint64_t size = 1ULL * length;
	memset(array->payload, 0, size);

	array->length = length;
	return OBJ_VAL(array);
}

COLD_FUNCTION
void importNative_array() {
	//array
	defineNative_array("resize", resizeNative);
	defineNative_array("length", lengthNative);
	defineNative_array("pop", popNative);
	defineNative_array("push", pushNative);

	defineNative_array("Array", ArrayNative);
	defineNative_array("F64Array", F64ArrayNative);
	defineNative_array("F32Array", F32ArrayNative);
	defineNative_array("U32Array", U32ArrayNative);
	defineNative_array("I32Array", I32ArrayNative);
	defineNative_array("U16Array", U16ArrayNative);
	defineNative_array("I16Array", I16ArrayNative);
	defineNative_array("U8Array", U8ArrayNative);
	defineNative_array("I8Array", I8ArrayNative);
}