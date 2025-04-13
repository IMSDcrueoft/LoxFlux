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
//force do gc
static Value gcNative(int argCount, Value* args) {
	garbageCollect();
	return NIL_VAL;
}

//change gc next
static Value gcNextNative(int argCount, Value* args) {
	if (argCount == 1 && IS_NUMBER(args[0])) {
		double nextGC = AS_NUMBER(args[0]);
		if (nextGC < 1024) {
			nextGC = 1024;
		}
		else if (nextGC > (1024 * 1024 * 1024)) {
			nextGC = (1024 * 1024 * 1024);
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
		if (beginGC < 1024) {
			beginGC = 1024;
		}
		else if (beginGC > (1024 * 1024 * 1024)) {
			beginGC = (1024 * 1024 * 1024);
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

COLD_FUNCTION
void importNative_system() {
	defineNative_system("gc", gcNative);
	defineNative_system("gcNext", gcNextNative);
	defineNative_system("gcBegin", gcBeginNative);
	defineNative_system("allocated", allocatedBytesNative);
	defineNative_system("static", staticBytesNative);

	defineNative_system("log", logNative);
}