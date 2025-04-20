/*
* MIT License
* Copyright (c) 2025 IMSDcrueoft (https://github.com/IMSDcrueoft)
* See LICENSE file in the root directory for full license text.
*/
#include "nativeBuiltin.h"
#include "vm.h"

//String
COLD_FUNCTION
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

COLD_FUNCTION
static Value UTF8LenNative(int argCount, Value* args) {
	if (argCount >= 1) {
		C_STR stringPtr = NULL;
		uint32_t length = 0;

		if (IS_STRING(args[0])) {
			ObjString* utf8_string = AS_STRING(args[0]);
			stringPtr = &utf8_string->chars[0];
			length = utf8_string->length;
		}
		else if (IS_STRING_BUILDER(args[0])) {
			ObjArray* utf8_string = AS_ARRAY(args[0]);
			stringPtr = utf8_string->payload;
			length = utf8_string->length;
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

COLD_FUNCTION
static Value charAtNative(int argCount, Value* args) {
	if (argCount >= 2 && IS_NUMBER(args[1])) {
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
			double indexf = AS_NUMBER(args[1]);
			if (indexf < 0 || indexf >= length) return NIL_VAL;//out of range
			uint32_t index = (uint32_t)indexf;

			return OBJ_VAL(copyString(stringPtr + index, 1, false));
		}
	}

	return NIL_VAL;
}

COLD_FUNCTION
static Value utf8AtNative(int argCount, Value* args) {
	if (argCount >= 2 && IS_NUMBER(args[1])) {
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

static uint32_t calculateBuilderCapacity(uint64_t initialLength) {
	uint64_t capacity = initialLength + 1;
	if (capacity > ARRAYLIKE_MAX) {
		fprintf(stderr, "StringBuilder size overflow");
		exit(1);
	}
	capacity = max(16, (((capacity * 3) >> 1) + 7) & ~7);
	return min(ARRAYLIKE_MAX, capacity);
}

//The builder accepts one parameter string/stringBuilder as the initial value
static Value builderNative(int argCount, Value* args) {
	ObjArray* stringBuilder = newStringBuilder();
	
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
			stringBuilder->payload[stringBuilder->length] = '\0';
		}
	}

	if (stringBuilder->capacity == 0) {
		stringBuilder->length = 0;
		stringBuilder->capacity = 16;
		stringBuilder->payload = ALLOCATE(char, 16);
		stringBuilder->payload[stringBuilder->length] = '\0';
	}

	return OBJ_VAL(stringBuilder);
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
	if (argCount >= 1 && IS_STRING_BUILDER(args[0])) {
		ObjArray* stringBuilder = AS_ARRAY(args[0]);

		if (argCount >= 2) {
			C_STR stringPtr = NULL;
			uint32_t length = 0;

			if (IS_STRING(args[1])) {
				ObjString* string = AS_STRING(args[1]);
				stringPtr = &string->chars[0];
				length = string->length;
			}
			else if (IS_STRING_BUILDER(args[1])) {
				ObjArray* string = AS_ARRAY(args[1]);
				stringPtr = string->payload;
				length = string->length;
			}

			if (stringPtr != NULL) {
				growStringBuilder(stringBuilder, length);
				memcpy(stringBuilder->payload + stringBuilder->length, stringPtr, length);
				stringBuilder->length += length;
				stringBuilder->payload[stringBuilder->length] = '\0';
			}
		}

		return args[0];
	}

	return NIL_VAL;
}

//make and return const string
static Value internNative(int argCount, Value* args) {
	if (argCount >= 1) {
		if (IS_STRING_BUILDER(args[0])) {
			ObjArray* stringBuilder = AS_ARRAY(args[0]);
			return OBJ_VAL(copyString(stringBuilder->payload, stringBuilder->length, false));
		}
		else if (IS_STRING(args[0])) {
			return args[0];
		}
	}

	return NIL_VAL;
}

static uint32_t calculateBuilderCapacity(uint64_t initialLength) {
	uint64_t capacity = initialLength + 1;
	if (capacity > ARRAYLIKE_MAX) {
		fprintf(stderr, "StringBuilder size overflow");
		exit(1);
	}
	capacity = max(16, (((capacity * 3) >> 1) + 7) & ~7);
	return min(ARRAYLIKE_MAX, capacity);
}

//The builder accepts one parameter string/stringBuilder as the initial value
static Value builderNative(int argCount, Value* args) {
	ObjArray* stringBuilder = newStringBuilder();
	
	//Select an adaptation scheme based on the type
	if (argCount >= 1) {
		if (IS_STRING(args[0])) {
			ObjString* string = AS_STRING(args[0]);
			uint32_t sourceLen = string->length;
			uint32_t capacity = calculateBuilderCapacity(sourceLen);

			stringBuilder->length = string->length;
			stringBuilder->capacity = capacity;
			stringBuilder->payload = ALLOCATE(char, capacity);
			memcpy(stringBuilder->payload, string->chars, sourceLen);
			stringBuilder->payload[stringBuilder->length] = '\0';
		}
		else if (IS_STRING_BUILDER(args[0])) {
			ObjArray* string = AS_ARRAY(args[0]);
			uint32_t sourceLen = string->length;
			uint32_t capacity = calculateBuilderCapacity(sourceLen);

			stringBuilder->length = sourceLen;
			stringBuilder->capacity = capacity;
			stringBuilder->payload = ALLOCATE(char, capacity);
			memcpy(stringBuilder->payload, string->payload, sourceLen);
			stringBuilder->payload[stringBuilder->length] = '\0';
		}
	}

	if (stringBuilder->capacity == 0) {
		stringBuilder->length = 0;
		stringBuilder->capacity = 16;
		stringBuilder->payload = ALLOCATE(char, 16);
		stringBuilder->payload[stringBuilder->length] = '\0';
	}

	return OBJ_VAL(stringBuilder);
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

static Value appendNative(int argCount, Value* args) {
	if (argCount >= 1 && IS_STRING_BUILDER(args[0])) {
		ObjArray* stringBuilder = AS_ARRAY(args[0]);

		if (argCount >= 2) {
			if (IS_STRING(args[1])) {
				ObjString* string = AS_STRING(args[1]);
				uint32_t sourceLen = string->length;
				growStringBuilder(stringBuilder, sourceLen);

				memcpy(stringBuilder->payload + stringBuilder->length, string->chars, sourceLen);
				stringBuilder->length += string->length;
				stringBuilder->payload[stringBuilder->length] = '\0';
			}
			else if (IS_STRING_BUILDER(args[1])) {
				ObjArray* string = AS_ARRAY(args[1]);
				uint32_t sourceLen = string->length;
				growStringBuilder(stringBuilder, sourceLen);

				memcpy(stringBuilder->payload + stringBuilder->length, string->payload, sourceLen);
				stringBuilder->length += string->length;
				stringBuilder->payload[stringBuilder->length] = '\0';
			}
		}

		return args[0];
	}

	return NIL_VAL;
}

//make and return const string
static Value internNative(int argCount, Value* args) {
	if (argCount >= 1) {
		if (IS_STRING_BUILDER(args[0])) {
			ObjArray* stringBuilder = AS_ARRAY(args[0]);
			return OBJ_VAL(copyString(stringBuilder->payload, stringBuilder->length, false));
		}
		else if (IS_STRING(args[0])) {
			return args[0];
		}
	}

	return NIL_VAL;
}

COLD_FUNCTION
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

COLD_FUNCTION
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

COLD_FUNCTION
void importNative_string() {
	defineNative_string("length", lengthNative);
	defineNative_string("charAt", charAtNative);
	defineNative_string("utf8Len", UTF8LenNative);
	defineNative_string("utf8At", utf8AtNative);
	defineNative_string("Builder", builderNative);
	defineNative_string("append", appendNative);
	defineNative_string("intern", internNative);
	defineNative_string("equals", equalsNative);
}