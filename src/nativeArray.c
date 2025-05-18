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
	if (argCount == 0 || !isArrayLike(args[0])) {
		fprintf(stderr, "push() expects a array like as first argument.\n");
		return NAN_VAL;
	}

	ObjArray* array = AS_ARRAY(args[0]);

	if (argCount >= 2) {
		uint64_t newSize = (uint64_t)array->length + argCount - 1;

		if (newSize > ARRAYLIKE_MAX) {
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

		if (OBJ_IS_TYPE(array, OBJ_ARRAY)) {
			//no type check,so it's faster
			for (uint32_t i = 1; i < argCount; ++i) {
				ARRAY_ELEMENT(array, Value, array->length) = args[i];
				array->length++;
			}
		}
		else {//typed array
			for (uint32_t i = 1; i < argCount; ++i) {
				Value val = IS_NUMBER(args[i]) ? args[i] : NUMBER_VAL(0);

				switch (OBJ_GET_TYPE(array->obj)) {
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

static Value popNative(int argCount, Value* args) {
	//don't solve stringBuilder
	if (argCount == 0 || !isArrayLike(args[0])) {
		fprintf(stderr, "pop() expects a array like as first argument.\n");
		return NAN_VAL;
	}

	ObjArray* array = AS_ARRAY(args[0]);

	if (array->length > 0) {
		Value value;
		if (OBJ_IS_TYPE(array, OBJ_ARRAY)) {
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

//resize the array,user don't care reserve ,this is not cpp vector
COLD_FUNCTION
static Value resizeNative(int argCount, Value* args) {
	if (argCount < 2 || !isArrayLike(args[0]) || !IS_NUMBER(args[1])) {
		fprintf(stderr, "resize() expects a array like as first argument and a number as second argument.\n");
		return NAN_VAL;
	}

	ObjArray* array = AS_ARRAY(args[0]);

	uint64_t length = 0;
	double size = AS_NUMBER(args[1]);

	//no error
	if (size > 0 && size <= ARRAYLIKE_MAX) {
		length = (uint64_t)size;

		if (length > array->length) {
			reserveArray(array, length);

			if (OBJ_IS_TYPE(array, OBJ_ARRAY)) {
				//no type check,so it's faster
				while (array->length < length) {
					ARRAY_ELEMENT(array, Value, array->length) = NIL_VAL;
					array->length++;
				}
			}
			else {//typed array
				switch (OBJ_GET_TYPE(array->obj)) {
				case OBJ_ARRAY_F64: {//must fit ieee754
					void* beginPtr = &ARRAY_ELEMENT(array, double, array->length);
					memset(beginPtr, 0, sizeof(double) * (length - array->length));
					break;
				}
				case OBJ_ARRAY_F32://must fit ieee754
				case OBJ_ARRAY_U32:
				case OBJ_ARRAY_I32: {
					void* beginPtr = &ARRAY_ELEMENT(array, uint32_t, array->length);
					memset(beginPtr, 0, sizeof(uint32_t) * (length - array->length));
					break;
				}
				case OBJ_ARRAY_U16:
				case OBJ_ARRAY_I16: {
					void* beginPtr = &ARRAY_ELEMENT(array, uint16_t, array->length);
					memset(beginPtr, 0, sizeof(uint16_t) * (length - array->length));
					break;
				}
				case OBJ_ARRAY_U8:
				case OBJ_ARRAY_I8: {
					void* beginPtr = &ARRAY_ELEMENT(array, uint8_t, array->length);
					memset(beginPtr, 0, sizeof(uint8_t) * (length - array->length));
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

static Value sliceNative(int argCount, Value* args) {
	if (argCount == 0 || !isArrayLike(args[0])) {
		fprintf(stderr, "slice() expects a array like as first argument.\n");
		return NIL_VAL;
	}

	ObjArray* array = AS_ARRAY(args[0]);
	int64_t length = array->length;

	int64_t start = 0;
	int64_t end = length;

	if (argCount >= 2 && IS_NUMBER(args[1])) {
		start = (int64_t)AS_NUMBER(args[1]);

		// neg index
		if (start < 0) {
			start = length + start;
		}

		// range check
		if (start < 0) start = 0;
		if (start > length) start = length;
	}

	if (argCount >= 3 && IS_NUMBER(args[2])) {
		end = (int64_t)AS_NUMBER(args[2]);

		// neg index 
		if (end < 0) {
			end = length + end;
		}

		// range check
		if (end < 0) end = 0;
		if (end > length) end = length;
	}

	if (start > end) start = end;

	uint32_t newLength = end - start;
	ObjArray* result = newArray(OBJ_GET_TYPE(array->obj));
	stack_push(OBJ_VAL(result));

	// allocate
	if (newLength > 0) {
		reserveArray(result, newLength);

		switch (OBJ_GET_TYPE(array->obj)) {
		case OBJ_ARRAY:
			memcpy(result->payload, (Value*)array->payload + start, newLength * sizeof(Value));
			break;
		case OBJ_ARRAY_F64:
			memcpy(result->payload, (double*)array->payload + start, newLength * sizeof(double));
			break;
		case OBJ_ARRAY_F32:
			memcpy(result->payload, (float*)array->payload + start, newLength * sizeof(float));
			break;
		case OBJ_ARRAY_U32:
		case OBJ_ARRAY_I32:
			memcpy(result->payload, (uint32_t*)array->payload + start, newLength * sizeof(uint32_t));
			break;
		case OBJ_ARRAY_U16:
		case OBJ_ARRAY_I16:
			memcpy(result->payload, (uint16_t*)array->payload + start, newLength * sizeof(uint16_t));
			break;
		case OBJ_ARRAY_U8:
		case OBJ_ARRAY_I8:
			memcpy(result->payload, (uint8_t*)array->payload + start, newLength * sizeof(uint8_t));
			break;
		}

		result->length = newLength;
	}

	return OBJ_VAL(result);
}

COLD_FUNCTION
void importNative_array() {
	//array
	defineNative_array("resize", resizeNative);
	defineNative_array("length", lengthNative);
	defineNative_array("pop", popNative);
	defineNative_array("push", pushNative);
	defineNative_array("slice", sliceNative);
}