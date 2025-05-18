/*
* MIT License
* Copyright (c) 2025 IMSDcrueoft (https://github.com/IMSDcrueoft)
* See LICENSE file in the root directory for full license text.
*/
#include "nativeBuiltin.h"
#include "vm.h"
#include "object.h"
#include "gc.h"
//System
#define KiB16 (16 * 1024)
#define GiB1 (1024 * 1024 * 1024)

//force do gc
static Value gcNative(int argCount, Value* args) {
	garbageCollect();
	return NIL_VAL;
}

//change gc next
static Value gcNextNative(int argCount, Value* args) {
	if (argCount == 1 && IS_NUMBER(args[0])) {
		double nextGC = AS_NUMBER(args[0]);
		if (nextGC < KiB16) {
			nextGC = KiB16;
		}
		else if (nextGC > GiB1) {
			nextGC = GiB1;
		}
		changeNextGC((uint64_t)nextGC);
		return BOOL_VAL(true);
	}
	else {
		return BOOL_VAL(false);
	}
}

//change gc begin
static Value gcBeginNative(int argCount, Value* args) {
	if (argCount == 1 && IS_NUMBER(args[0])) {
		double beginGC = AS_NUMBER(args[0]);
		if (beginGC < KiB16) {
			beginGC = KiB16;
		}
		else if (beginGC > GiB1) {
			beginGC = GiB1;
		}
		changeBeginGC((uint64_t)beginGC);
		return BOOL_VAL(true);
	}
	else {
		return BOOL_VAL(false);
	}
}

static Value allocatedBytesNative(int argCount, Value* args) {
	return NUMBER_VAL((double)vm.bytesAllocated);
}

static Value staticBytesNative(int argCount, Value* args) {
	return NUMBER_VAL((double)vm.bytesAllocated_no_gc);
}

//Print all the parameters
static Value logNative(int argCount, Value* args) {
	for (int i = 0; i < argCount;) {
		printValue_sys(args[i]);

		if (++i < argCount) {
			printf(" ");
		}
		else {
			printf("\n");
		}
	}
	//no '\n'
	return NIL_VAL;
}

static Value errorNative(int argCount, Value* args) {
	if (argCount >= 1) {
		if (IS_STRING(args[0])) {
			ObjString* string = AS_STRING(args[0]);
			fprintf(stderr, "%s\n", string->chars);
		}
		else if (IS_STRING_BUILDER(args[0])) {
			ObjArray* string = AS_ARRAY(args[0]);
			fprintf(stderr, "%s\n", (char*)string->payload);
		}
	}

	return NIL_VAL;
}

static Value inputNative(int argCount, Value* args) {
	//don't need param
	ObjArray* stringBuilder = newArray(OBJ_STRING_BUILDER);
	stack_push(OBJ_VAL(stringBuilder));

	//init size
	reserveArray(stringBuilder, 16);

	int c;
	while ((c = getchar()) != '\n' && c != EOF) {
		//check capcity
		if (stringBuilder->length + 1 >= stringBuilder->capacity) {
			uint64_t newCapacity = min(ARRAYLIKE_MAX, (stringBuilder->capacity * 3) >> 1);
			reserveArray(stringBuilder, newCapacity);
		}

		// append
		ARRAY_ELEMENT(stringBuilder, char, stringBuilder->length) = (char)c;
		stringBuilder->length++;
	}

	if (stringBuilder->length + 1 >= stringBuilder->capacity) {
		uint64_t newCapacity = min(ARRAYLIKE_MAX, (stringBuilder->capacity * 3) >> 1);
		reserveArray(stringBuilder, newCapacity);
	}
	//add null
	ARRAY_ELEMENT(stringBuilder, char, stringBuilder->length) = '\0';

	//deal with escape chars
	if (stringBuilder->length > 0) {
		char* input = (char*)stringBuilder->payload;
		uint32_t readPos = 0;
		uint32_t writePos = 0;

		while (readPos < stringBuilder->length) {
			if (input[readPos] == '\\' && (readPos + 1) < stringBuilder->length) {
				readPos++;

				switch (input[readPos]) {
				case '\\': 
					input[writePos++] = '\\';
					break;
				case '\"': 
					input[writePos++] = '\"';
					break;
				case 'n':
					input[writePos++] = '\n';
					break;
				default:
					input[writePos++] = '\\';
					input[writePos++] = input[readPos];
					break;
				}
				readPos++;
			}
			else {
				input[writePos++] = input[readPos++];
			}
		}

		//update length
		stringBuilder->length = writePos;
		ARRAY_ELEMENT(stringBuilder, char, stringBuilder->length) = '\0';
	}

	return OBJ_VAL(stringBuilder);
}

static Value readFileNative(int argCount, Value* args) {
	if (argCount < 1) {
		fprintf(stderr, "readFile expects a path argument.\n");
		return NIL_VAL;
	}

	C_STR path = NULL;

	if (IS_STRING(args[0])) {
		ObjString* pathString = AS_STRING(args[0]);
		path = &pathString->chars[0];
	}
	else if (IS_STRING_BUILDER(args[0])) {
		ObjArray* pathBuilder = AS_ARRAY(args[0]);
		path = pathBuilder->payload;
	}
	else {
		fprintf(stderr, "readFile expects a string or stringBuilder path argument.\n");
		return NIL_VAL;
	}

	FILE* file = fopen(path, "rb");

	if (file == NULL) {
		fprintf(stderr, "Could not open file \"%s\".\n", path);
		return NIL_VAL;
	}

	fseek(file, 0L, SEEK_END);
	uint64_t fileSize = ftell(file);
	rewind(file);

	// check file size
	if (fileSize > ARRAYLIKE_MAX - 1) { // left 1 byte for null terminator
		fclose(file);
		fprintf(stderr, "File size exceeds maximum StringBuilder capacity.\n");
		return NIL_VAL;
	}

	ObjArray* stringBuilder = newArray(OBJ_STRING_BUILDER);
	stack_push(OBJ_VAL(stringBuilder));

	// allocate memory
	reserveArray(stringBuilder, fileSize + 1);

	// read size
	uint64_t bytesRead = fread(stringBuilder->payload, sizeof(char), fileSize, file);
	fclose(file);

	if (bytesRead < fileSize) {
		fprintf(stderr, "Could not read file \"%s\".\n", path);
		return NIL_VAL;
	}

	// set length and null terminator
	stringBuilder->length = (uint32_t)bytesRead;
	ARRAY_ELEMENT(stringBuilder, char, stringBuilder->length) = '\0';

	return OBJ_VAL(stringBuilder);
}

#undef KiB16
#undef GiB1

COLD_FUNCTION
void importNative_system() {
	defineNative_system("gc", gcNative);
	defineNative_system("gcNext", gcNextNative);
	defineNative_system("gcBegin", gcBeginNative);
	defineNative_system("allocated", allocatedBytesNative);
	defineNative_system("static", staticBytesNative);

	defineNative_system("log", logNative);
	defineNative_system("error", errorNative);

	defineNative_system("input", inputNative);
	defineNative_system("readFile", readFileNative);
}