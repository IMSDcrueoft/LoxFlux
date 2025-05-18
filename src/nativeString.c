/*
* MIT License
* Copyright (c) 2025 IMSDcrueoft (https://github.com/IMSDcrueoft)
* See LICENSE file in the root directory for full license text.
*/
#include "nativeBuiltin.h"
#include "vm.h"

//const string limit,will truncate
#define INTERN_STRING_WARN (1024)

//String
static Value lengthNative(int argCount, Value* args)
{
	if (argCount >= 1) {
		if (IS_STRING(args[0])) {
			return NUMBER_VAL(AS_STRING(args[0])->length);
		}
		else if (IS_STRING_BUILDER(args[0])) {
			return NUMBER_VAL(AS_ARRAY(args[0])->length);
		}
	}
	return NAN_VAL;
}

static Value UTF8LenNative(int argCount, Value* args) {
	if (argCount >= 1) {
		C_STR stringPtr = NULL;
		uint32_t length = 0;

		if (IS_STRING(args[0])) {
			ObjString* utf8_string = AS_STRING(args[0]);
			stringPtr = utf8_string->chars;
			length = utf8_string->length;
		}
		else if (IS_STRING_BUILDER(args[0])) {
			ObjArray* utf8_string = AS_ARRAY(args[0]);
			stringPtr = utf8_string->payload;
			length = utf8_string->length;
		}
		else {
			fprintf(stderr, "utf8Len() expects a string or stringBuilder as first argument.\n");
			return NAN_VAL;
		}

		if (stringPtr != NULL) {
			uint32_t char_count = 0;
			//get utf8 length
			for (uint32_t i = 0; i < length; char_count++) {
				uint8_t c = stringPtr[i];

				if ((c & 0x80) == 0) {       // 1 (0xxxxxxx)
					i += 1;
				}
				else if ((c & 0xE0) == 0xC0) { // 2 (110xxxxx)
					i += 2;
				}
				else if ((c & 0xF0) == 0xE0) { // 3 (1110xxxx)
					i += 3;
				}
				else if ((c & 0xF8) == 0xF0) { // 4 (11110xxx)
					i += 4;
				}
				else {
					return NAN_VAL;//invalid utf8
				}
			}
			return NUMBER_VAL((double)char_count);
		}
	}

	return NAN_VAL;
}

static Value charAtNative(int argCount, Value* args) {
	if (argCount >= 2 && IS_NUMBER(args[1])) {
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
		else {
			fprintf(stderr, "charAt() expects a string or stringBuilder as first argument.\n");
			return NIL_VAL;
		}

		if (stringPtr != NULL) {
			double indexf = AS_NUMBER(args[1]);
			if (indexf < 0 || indexf >= length) return NIL_VAL;//out of range
			uint32_t index = (uint32_t)indexf;

			return OBJ_VAL(copyString(stringPtr + index, 1, false));
		}
	}

	return NIL_VAL;
}

static Value utf8AtNative(int argCount, Value* args) {
	if (argCount >= 2 && IS_NUMBER(args[1])) {
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
		else {
			fprintf(stderr, "utf8At() expects a string or stringBuilder as first argument.\n");
			return NIL_VAL;
		}

		if (stringPtr != NULL) {
			double indexf = AS_NUMBER(args[1]);
			if (indexf < 0 || indexf >= length) return NIL_VAL;//out of range
			uint32_t index = (uint32_t)indexf;

			for (uint32_t i = 0, char_count = 0, start; i < length; char_count++) {
				uint8_t c = stringPtr[i];
				start = i;

				if ((c & 0x80) == 0) {       // 1 (0xxxxxxx)
					i += 1;
				}
				else if ((c & 0xE0) == 0xC0) { // 2 (110xxxxx)
					i += 2;
				}
				else if ((c & 0xF0) == 0xE0) { // 3 (1110xxxx)
					i += 3;
				}
				else if ((c & 0xF8) == 0xF0) { // 4 (11110xxx)
					i += 4;
				}
				else {
					return NAN_VAL;//invalid utf8
				}

				if (char_count == index) {
					return OBJ_VAL(copyString(stringPtr + start, i - start, false));
				}
			}
		}
	}

	return NIL_VAL;
}

static void growStringBuilder(ObjArray* builder, uint32_t appendLen) {
	uint64_t capacity = (uint64_t)builder->length + appendLen + 1;
	if (capacity > builder->capacity) {
		if (capacity > ARRAYLIKE_MAX) {
			fprintf(stderr, "StringBuilder size overflow");
			exit(1);
		}
		capacity = min(ARRAYLIKE_MAX, (capacity * 3) >> 1);
		reserveArray(builder, capacity);
	}
}

COLD_FUNCTION
static Value appendNative(int argCount, Value* args) {
	if (argCount == 0 || !IS_STRING_BUILDER(args[0])) {
		fprintf(stderr, "append() expects a stringBuilder as first argument.\n");
		return NIL_VAL;
	}

	ObjArray* stringBuilder = AS_ARRAY(args[0]);

	if (argCount >= 2) {
		C_STR stringPtr = NULL;
		uint32_t length = 0;

		if (IS_STRING(args[1])) {
			ObjString* string = AS_STRING(args[1]);

			length = string->length;
			growStringBuilder(stringBuilder, length);
			stringPtr = string->chars;
		}
		else if (IS_STRING_BUILDER(args[1])) {
			ObjArray* string = AS_ARRAY(args[1]);

			length = string->length;
			growStringBuilder(stringBuilder, length);
			stringPtr = string->payload;//can append self ,this will error
		}
		else {
			fprintf(stderr, "append() expects a string or stringBuilder as second argument.\n");
			return NIL_VAL;
		}

		if (stringPtr != NULL) {
			memcpy((char*)stringBuilder->payload + stringBuilder->length, stringPtr, length);
			stringBuilder->length += length;
			ARRAY_ELEMENT(stringBuilder, char, stringBuilder->length) = '\0';
		}
	}

	return args[0];
}

//make and return const string
static Value internNative(int argCount, Value* args) {
	if (argCount >= 1) {
		if (IS_STRING_BUILDER(args[0])) {
			ObjArray* stringBuilder = AS_ARRAY(args[0]);
			if (stringBuilder->length > INTERN_STRING_WARN) {
				fprintf(stderr, "Extra-long intern string of length: %d", stringBuilder->length);
			}
			return OBJ_VAL(copyString(stringBuilder->payload, stringBuilder->length, false));
		}
		else if (IS_STRING(args[0])) {
			return args[0];
		}
	}

	return NIL_VAL;
}

static Value equalsNative(int argCount, Value* args) {
	if (argCount >= 2) {
		if (IS_STRING_BUILDER(args[0])) {
			ObjArray* stringBuilderA = AS_ARRAY(args[0]);

			if (IS_STRING_BUILDER(args[1])) {
				ObjArray* stringBuilderB = AS_ARRAY(args[1]);
				return BOOL_VAL((stringBuilderA->length == stringBuilderB->length) && (memcmp(stringBuilderA->payload, stringBuilderB->payload, stringBuilderA->length) == 0));
			}
			else if (IS_STRING(args[1])) {
				ObjString* stringB = AS_STRING(args[1]);
				return BOOL_VAL((stringBuilderA->length == stringB->length) && (memcmp(stringBuilderA->payload, stringB->chars, stringBuilderA->length) == 0));
			}
		}
		else if (IS_STRING(args[0])) {
			ObjString* stringA = AS_STRING(args[0]);

			if (IS_STRING_BUILDER(args[1])) {
				ObjArray* stringBuilderB = AS_ARRAY(args[1]);
				return BOOL_VAL((stringA->length == stringBuilderB->length) && (memcmp(stringA->chars, stringBuilderB->payload, stringA->length) == 0));
			}
			else if (IS_STRING(args[1])) {
				ObjString* stringB = AS_STRING(args[1]);
				return BOOL_VAL(stringA == stringB);
			}
		}
	}

	return BOOL_VAL(false);
}

static Value parseIntNative(int argCount, Value* args) {
	if (argCount >= 1) {
		C_STR stringPtr = NULL;
		uint32_t length = 0;
		int32_t base = 0; // Default to auto-detection (decimal/octal/hexadecimal)
		bool isNegative = false;

		// Check if radix (base) argument is provided
		if (argCount >= 2 && IS_NUMBER(args[1])) {
			double baseValue = AS_NUMBER(args[1]);
			// Validate base is integer between 2-36
			if (baseValue >= 2 && baseValue <= 36 && (int32_t)baseValue == baseValue) {
				base = (int32_t)baseValue;
			}
			else {
				return NAN_VAL; // Invalid radix value
			}
		}

		// Get string pointer and length from either String or StringBuilder
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

		if (stringPtr != NULL && length > 0) {
			STR endPtr = NULL;
			C_STR startPtr = stringPtr;

			// Handle negative sign if present
			if (*startPtr == '-') {
				isNegative = true;
				startPtr++;
				length--;
			}

			// Check for binary prefix (0b or 0B or 0x or 0X)
			if (length >= 2 && startPtr[0] == '0') {
				if (startPtr[1] == 'b' || startPtr[1] == 'B') {
					// Found binary prefix - override user's radix and force base 2
					int64_t value = strtol(startPtr + 2, &endPtr, 2);
					if (endPtr != startPtr + 2) {
						if (isNegative) value = -value;
						return NUMBER_VAL((double)value);
					}
				}
				else if (startPtr[1] == 'x' || startPtr[1] == 'X') {
					// Found hex prefix - override user's radix and force base 16
					// strtol automatically handles 0x prefix when base is 16
					int64_t value = strtol(startPtr, &endPtr, 16);
					if (endPtr != startPtr) {
						if (isNegative) value = -value;
						return NUMBER_VAL((double)value);
					}
				}
			}
			else {
				// No special prefix - use user-specified radix or auto-detect
				int64_t value = strtol(startPtr, &endPtr, base);
				if (endPtr != startPtr) {
					if (isNegative) value = -value;
					return NUMBER_VAL((double)value);
				}
			}
		}
	}

	return NAN_VAL; // Return NaN if parsing fails
}

static Value parseFloatNative(int argCount, Value* args) {
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
			STR endPtr = NULL;
			double value = strtod(stringPtr, &endPtr);

			// Check if conversion was successful
			if (endPtr != stringPtr) {
				return NUMBER_VAL(value);
			}
		}
	}

	return NAN_VAL;
}

static Value sliceNative(int argCount, Value* args) {
	if (argCount < 2 || !IS_NUMBER(args[1])) {
		fprintf(stderr, "slice() expects a string or stringBuilder and at least one number argument.\n");
		return NIL_VAL;
	}

	C_STR stringPtr = NULL;
	int64_t length = 0;

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
	else {
		fprintf(stderr, "slice() expects a string or stringBuilder as first argument.\n");
		return NIL_VAL;
	}

	double beginIndexf = AS_NUMBER(args[1]);
	double endIndexf = length;

	if (argCount >= 3 && IS_NUMBER(args[2])) {
		endIndexf = AS_NUMBER(args[2]);
	}

	// neg index
	int64_t beginIndex = (int64_t)beginIndexf;
	if (beginIndex < 0) {
		beginIndex = length + beginIndex;
	}

	int64_t endIndex = (int64_t)endIndexf;
	if (endIndex < 0) {
		endIndex = length + endIndex;
	}

	// check range
	beginIndex = (beginIndex < 0) ? 0 : (beginIndex > length ? length : beginIndex);
	endIndex = (endIndex < beginIndex) ? beginIndex : (endIndex > length ? length : endIndex);

	// calc slice length
	uint32_t sliceLength = endIndex - beginIndex;

	ObjArray* stringBuilder = newArray(OBJ_STRING_BUILDER);
	stack_push(OBJ_VAL(stringBuilder));

	if (sliceLength > 0) {
		//allocate
		reserveArray(stringBuilder, sliceLength + 1);

		// copy
		memcpy(stringBuilder->payload, stringPtr + beginIndex, sliceLength);
		stringBuilder->length = sliceLength;
		ARRAY_ELEMENT(stringBuilder, char, stringBuilder->length) = '\0';
	}

	return OBJ_VAL(stringBuilder);
}

COLD_FUNCTION
void importNative_string() {
	defineNative_string("length", lengthNative);
	defineNative_string("charAt", charAtNative);
	defineNative_string("utf8Len", UTF8LenNative);
	defineNative_string("utf8At", utf8AtNative);
	defineNative_string("append", appendNative);
	defineNative_string("intern", internNative);
	defineNative_string("equals", equalsNative);
	defineNative_string("slice", sliceNative);
	defineNative_string("parseInt", parseIntNative);
	defineNative_string("parseFloat", parseFloatNative);
}