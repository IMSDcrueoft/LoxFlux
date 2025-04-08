/*
* MIT License
* Copyright (c) 2025 IM&SD (https://github.com/IMSDcrueoft)
* See LICENSE file in the root directory for full license text.
*/
#include "nativeBuiltin.h"
#include "vm.h"
#include "gc.h"

//System
//force do gc
static Value gcNative(int argCount, Value* args) {
	garbageCollect();
	return NIL_VAL;
}

//change gc next
static Value gcNextNative(int argCount, Value* args) {
	if (argCount != 1 || (args[0].type != VAL_NUMBER)) {
		return NIL_VAL;
	}
	double nextGC = AS_NUMBER(args[0]);
	if (nextGC < 1024) {
		nextGC = 1024;
	}
	else if (nextGC > (1024 * 1024 * 1024)) {
		nextGC = (1024 * 1024 * 1024);
	}
	changeNextGC((uint64_t)nextGC);
	return NIL_VAL;
}

//change gc begin
static Value gcBeginNative(int argCount, Value* args) {
	if (argCount != 1 || (args[0].type != VAL_NUMBER)) {
		return NIL_VAL;
	}
	double beginGC = AS_NUMBER(args[0]);
	if (beginGC < 1024) {
		beginGC = 1024;
	}
	else if (beginGC > (1024 * 1024 * 1024)) {
		beginGC = (1024 * 1024 * 1024);
	}
	changeBeginGC((uint64_t)beginGC);
	return NIL_VAL;
}

COLD_FUNCTION
void importNative_system() {
	defineNative_system("gc", gcNative);
	defineNative_system("gcNext", gcNextNative);
	defineNative_system("gcBegin", gcBeginNative);
}