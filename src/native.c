/*
 * MIT License
 * Copyright (c) 2025 IM&SD (https://github.com/IMSDcrueoft)
 * See LICENSE file in the root directory for full license text.
*/
#include "native.h"
#include "vm.h"
#include "timer.h"
#include "gc.h"

//return second
static Value clockNative(int argCount, Value* args, C_STR* errorInfo)
{
	if (argCount != 0) {
		*errorInfo = "clock(): Expected 0 arguments but got some";
		return NIL_VAL;
	}
	return NUMBER_VAL((double)get_nanoseconds() * 1e-9);
}

//max with multiple input
static Value maxNative(int argCount, Value* args, C_STR* errorInfo)
{
	if (argCount < 2) {
		*errorInfo = "max(): Expected at least 2 arguments";
		return NIL_VAL;
	}

	if (!(IS_NUMBER(args[0])) || !IS_NUMBER(args[1])) {
		return NUMBER_VAL(NAN);
	}

	double val = max(AS_NUMBER(args[0]), AS_NUMBER(args[1]));

	for (uint32_t i = 2; i < argCount; ++i) {
		if (!(IS_NUMBER(args[i]))) {
			val = NAN;
			break;
		}

		val = max(val, AS_NUMBER(args[i]));
	}

	return NUMBER_VAL(val);
}

//max with multiple input
static Value minNative(int argCount, Value* args, C_STR* errorInfo)
{
	if (argCount < 2) {
		*errorInfo = "min(): Expected at least 2 arguments";
		return NIL_VAL;
	}

	if (!(IS_NUMBER(args[0])) || !IS_NUMBER(args[1])) {
		return NUMBER_VAL(NAN);
	}

	double val = min(AS_NUMBER(args[0]), AS_NUMBER(args[1]));

	for (uint32_t i = 2; i < argCount; ++i) {
		if (!(IS_NUMBER(args[i]))) {
			val = NAN;
			break;
		}

		val = min(val, AS_NUMBER(args[i]));
	}

	return NUMBER_VAL(val);
}

//force do gc
static Value gcNative(int argCount, Value* args, C_STR* errorInfo) {
	if (argCount != 0) {
		*errorInfo = "gc(): Expected 0 arguments but got some";
		return NIL_VAL;
	}
	garbageCollect();
	return NIL_VAL;
}

//change gc next
static Value gcNextNative(int argCount, Value* args, C_STR* errorInfo) {
	if (argCount != 1) {
		*errorInfo = (argCount == 0)
			? "gcNext(): Expected 1 argument but got none"
			: "gcNext(): Expected 1 argument but got more";
		return NIL_VAL;
	}

	if (args[0].type != VAL_NUMBER) {
		*errorInfo = "gcNext(): The parameter must be a number";
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
static Value gcBeginNative(int argCount, Value* args, C_STR* errorInfo) {
	if (argCount != 1) {
		*errorInfo = (argCount == 0)
			? "gcBegin(): Expected 1 argument but got none"
			: "gcBegin(): Expected 1 argument but got more";
		return NIL_VAL;
	}

	if (args[0].type != VAL_NUMBER) {
		*errorInfo = "gcBegin(): The parameter must be a number";
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

void importNative()
{
	defineNative("clock", clockNative);
	defineNative("max", maxNative);
	defineNative("min", minNative);
	defineNative("gc", gcNative);
	defineNative("gcNext", gcNextNative);
	defineNative("gcBegin", gcBeginNative);
}
