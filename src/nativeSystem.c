/*
* MIT License
* Copyright (c) 2025-2026 IMSDcrueoft (https://github.com/IMSDcrueoft)
* See LICENSE file in the root directory for full license text.
*/
#include "nativeBuiltin.h"
#include "vm.h"
#include "object.h"
#include "gc.h"
#include "allocator.h"
//System
#define KiB16 (16 * 1024)
#define GiB1 (1024 * 1024 * 1024)

//64-bit file offsets: the plain ftell/fseek use a 32-bit long on Windows,
//which misreports everything past 2GB
#if defined(_MSC_VER)
#define LOXFLUX_FSEEK_64(file, off, whence) _fseeki64((file), (off), (whence))
#define LOXFLUX_FTELL_64(file) _ftelli64((file))
#elif defined(_WIN32)
#define LOXFLUX_FSEEK_64(file, off, whence) fseeko64((file), (off), (whence))
#define LOXFLUX_FTELL_64(file) ftello64((file))
#else
#define LOXFLUX_FSEEK_64(file, off, whence) fseeko((file), (off), (whence))
#define LOXFLUX_FTELL_64(file) ftello((file))
#endif

//write payload extraction: string/stringBuilder via the safe buffer helper,
//u8 arrays raw (payload/length). any other value kind is rejected
static bool getWriteBytes(LfxValue data, const char** bytes, uint64_t* length)
{
	if (IS_STRING(data)) {
		ObjString* string = AS_STRING(data);
		*bytes = string->chars;
		*length = string->length;
		return true;
	}

	if (IS_STRING_BUILDER(data)) {
		ObjArray* builder = AS_ARRAY(data);
		*bytes = builder->payload;
		*length = builder->length;
		return true;
	}

	if (IS_OBJ(data) && AS_OBJ(data)->type == OBJ_ARRAY_U8) {
		ObjArray* array = AS_ARRAY(data);
		*bytes = array->payload;
		*length = array->length;
		return true;
	}

	return false;
}

//force do gc
static LfxValue gcNative(LfxVM* vm, int argCount, LfxValue* args) {
	(void)vm;
	garbageCollect();
	return NIL_VAL;
}

//change gc next
static LfxValue gcNextNative(LfxVM* vm, int argCount, LfxValue* args) {
	(void)vm;
	if (argCount == 1 && IS_NUMBER(args[0])) {
		double nextGC = AS_NUMBER(args[0]);
		if (nextGC < KiB16) {
			nextGC = KiB16;
		}
		else if (nextGC > GiB1) {
			nextGC = GiB1;
		}
		changeNextGC((size_t)nextGC);
		return BOOL_VAL(true);
	}
	else {
		return BOOL_VAL(false);
	}
}

//change gc begin
static LfxValue gcBeginNative(LfxVM* vm, int argCount, LfxValue* args) {
	(void)vm;
	if (argCount == 1 && IS_NUMBER(args[0])) {
		double beginGC = AS_NUMBER(args[0]);
		if (beginGC < KiB16) {
			beginGC = KiB16;
		}
		else if (beginGC > GiB1) {
			beginGC = GiB1;
		}
		changeBeginGC((size_t)beginGC);
		return BOOL_VAL(true);
	}
	else {
		return BOOL_VAL(false);
	}
}

static LfxValue allocatedBytesNative(LfxVM* vm, int argCount, LfxValue* args) {
	return NUMBER_VAL((double)vm->bytesAllocated);
}

static LfxValue pinnedBytesNative(LfxVM* vm, int argCount, LfxValue* args) {
	//no_gc book + the interned-string arena: the old heap strings were counted
	//in the no_gc book too, so they stay inside this number (exact consumption:
	//the arena is append-only, liveTop = sum of string sizes + alignment slack)
	return NUMBER_VAL((double)(vm->bytesAllocated_no_gc + arenaBump_used(&vm->stringArena)));
}

//Print all the parameters
static LfxValue logNative(LfxVM* vm, int argCount, LfxValue* args) {
	(void)vm;
	for (int i = 0; i < argCount;) {
		printValue(stdout, args[i], true);

		if (++i < argCount) {
			printf(" ");
		}
		else {
			printf("\n");
		}
	}
	return NIL_VAL;
}

static LfxValue errorNative(LfxVM* vm, int argCount, LfxValue* args) {
	(void)vm;
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

static LfxValue inputNative(LfxVM* vm, int argCount, LfxValue* args) {
	(void)vm;
	//don't need param
	ObjArray* stringBuilder = newArray(OBJ_STRING_BUILDER);
	stack_push(vm, OBJ_VAL(stringBuilder));

	//init size
	reserveArray(stringBuilder, 16);

	int c;
	while ((c = getchar()) != '\n' && c != EOF) {
		//check capacity
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
				case 't':
					input[writePos++] = '\t';
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

static LfxValue readFileNative(LfxVM* vm, int argCount, LfxValue* args) {
	(void)vm;
	if (argCount < 1) {
		fprintf(stderr, "readFile expects a path argument.\n");
		return NIL_VAL;
	}

	C_STR path = getBufferFromStringLike(args[0]);

	if (path == NULL) {
		fprintf(stderr, "readFile expects a string or stringBuilder path argument.\n");
		return NIL_VAL;
	}

	FILE* file = fopen(path, "rb");

	if (file == NULL) {
		fprintf(stderr, "Could not open file \"%s\".\n", path);
		return NIL_VAL;
	}

	LOXFLUX_FSEEK_64(file, 0, SEEK_END);
	int64_t fileSize = LOXFLUX_FTELL_64(file);
	rewind(file);

	// check file size
	if (fileSize < 0 || fileSize > (int64_t)(ARRAYLIKE_MAX - 1)) { // left 1 byte for null terminator
		fclose(file);
		fprintf(stderr, "File size exceeds maximum StringBuilder capacity.\n");
		return NIL_VAL;
	}

	ObjArray* stringBuilder = newArray(OBJ_STRING_BUILDER);
	stack_push(vm, OBJ_VAL(stringBuilder));

	// allocate memory
	reserveArray(stringBuilder, fileSize + 1);

	// read size
	uint64_t bytesRead = fread(stringBuilder->payload, sizeof(char), fileSize, file);
	fclose(file);

	if (bytesRead < (uint64_t)fileSize) {
		fprintf(stderr, "Could not read file \"%s\".\n", path);
		return NIL_VAL;
	}

	// set length and null terminator
	stringBuilder->length = (uint32_t)bytesRead;
	ARRAY_ELEMENT(stringBuilder, char, stringBuilder->length) = '\0';

	return OBJ_VAL(stringBuilder);
}

static LfxValue readBinNative(LfxVM* vm, int argCount, LfxValue* args) {
	(void)vm;
	if (argCount < 1) {
		fprintf(stderr, "readBin expects a path argument.\n");
		return NIL_VAL;
	}

	C_STR path = getBufferFromStringLike(args[0]);
	if (path == NULL) {
		fprintf(stderr, "readBin expects a string or stringBuilder path argument.\n");
		return NIL_VAL;
	}

	FILE* file = fopen(path, "rb");
	if (file == NULL) {
		fprintf(stderr, "Could not open file \"%s\".\n", path);
		return NIL_VAL;
	}

	LOXFLUX_FSEEK_64(file, 0, SEEK_END);
	int64_t fileSize = LOXFLUX_FTELL_64(file);
	rewind(file);

	if (fileSize < 0 || fileSize > (int64_t)ARRAYLIKE_MAX) {
		fclose(file);
		fprintf(stderr, "File size exceeds maximum U8Array capacity.\n");
		return NIL_VAL;
	}

	ObjArray* array = newArray(OBJ_ARRAY_U8);
	stack_push(vm, OBJ_VAL(array));

	reserveArray(array, (uint64_t)fileSize);

	uint64_t bytesRead = 0;
	if (fileSize > 0) {
		bytesRead = fread(array->payload, 1, (size_t)fileSize, file);
	}
	fclose(file);

	if (bytesRead < (uint64_t)fileSize) {
		fprintf(stderr, "Could not read file \"%s\".\n", path);
		return NIL_VAL;
	}

	array->length = (uint32_t)bytesRead;
	return OBJ_VAL(array);
}

//shared write core: data already validated, mode "wb"/"r+b"/"ab", offset
//applies only to the r+b form (sparse expansion fills the hole with zeros)
static LfxValue writeFileCore(LfxValue pathValue, LfxValue data, bool useOffset, double offset, C_STR mode)
{
	C_STR path = getBufferFromStringLike(pathValue);
	if (path == NULL) {
		fprintf(stderr, "expects a string or stringBuilder path argument.\n");
		return NIL_VAL;
	}

	const char* bytes = NULL;
	uint64_t length = 0;
	if (!getWriteBytes(data, &bytes, &length)) {
		fprintf(stderr, "expects a string, stringBuilder or u8 array as data argument.\n");
		return NIL_VAL;
	}

	FILE* file = fopen(path, mode);
	if (file == NULL) {
		fprintf(stderr, "Could not open file \"%s\".\n", path);
		return NIL_VAL;
	}

	if (useOffset && offset > 0) {
		LOXFLUX_FSEEK_64(file, (int64_t)offset, SEEK_SET);
	}

	uint64_t written = 0;
	if (length > 0) {
		written = fwrite(bytes, 1, (size_t)length, file);
	}
	fclose(file);

	if (written < length) {
		fprintf(stderr, "Could not write file \"%s\".\n", path);
		return NIL_VAL;
	}

	return NUMBER_VAL((double)written);
}

static LfxValue writeFileNative(LfxVM* vm, int argCount, LfxValue* args) {
	(void)vm;
	if (argCount < 2) {
		fprintf(stderr, "writeFile expects a path and a data argument.\n");
		return NIL_VAL;
	}

	//offset form: "r+b" positional write, negative or oversized is rejected
	if (argCount >= 3) {
		if (!IS_NUMBER(args[2])) {
			fprintf(stderr, "writeFile expects a number as offset argument.\n");
			return NIL_VAL;
		}

		double offset = AS_NUMBER(args[2]);
		if (!(offset >= 0 && offset <= (double)ARRAYLIKE_MAX)) {
			fprintf(stderr, "writeFile offset out of range.\n");
			return NIL_VAL;
		}

		return writeFileCore(args[0], args[1], true, offset, "r+b");
	}

	return writeFileCore(args[0], args[1], false, 0, "wb");
}

static LfxValue appendFileNative(LfxVM* vm, int argCount, LfxValue* args) {
	(void)vm;
	if (argCount < 2) {
		fprintf(stderr, "appendFile expects a path and a data argument.\n");
		return NIL_VAL;
	}

	return writeFileCore(args[0], args[1], false, 0, "ab");
}

static LfxValue fileExistsNative(LfxVM* vm, int argCount, LfxValue* args) {
	(void)vm;
	if (argCount < 1) {
		fprintf(stderr, "fileExists expects a path argument.\n");
		return NIL_VAL;
	}

	C_STR path = getBufferFromStringLike(args[0]);
	if (path == NULL) {
		fprintf(stderr, "fileExists expects a string or stringBuilder path argument.\n");
		return NIL_VAL;
	}

	FILE* file = fopen(path, "rb");
	if (file != NULL) {
		fclose(file);
	}

	return BOOL_VAL(file != NULL);
}

#undef KiB16
#undef GiB1

COLD_FUNCTION
void importNative_system(LfxVM* vm) {
	defineNative_system(vm, "gcRun", gcNative);
	defineNative_system(vm, "gcNext", gcNextNative);
	defineNative_system(vm, "gcBegin", gcBeginNative);
	defineNative_system(vm, "allocatedBytes", allocatedBytesNative);
	defineNative_system(vm, "pinnedBytes", pinnedBytesNative);

	defineNative_system(vm, "log", logNative);
	defineNative_system(vm, "error", errorNative);

	defineNative_system(vm, "input", inputNative);
	defineNative_system(vm, "readFile", readFileNative);
	defineNative_system(vm, "readBin", readBinNative);
	defineNative_system(vm, "writeFile", writeFileNative);
	defineNative_system(vm, "appendFile", appendFileNative);
	defineNative_system(vm, "fileExists", fileExistsNative);
}
