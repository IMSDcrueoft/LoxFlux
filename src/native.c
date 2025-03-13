/*
 * MIT License
 * Copyright (c) 2025 IM&SD (https://github.com/IMSDcrueoft)
 * See LICENSE file in the root directory for full license text.
*/
#include "native.h"
#include "vm.h"
#include <time.h>

//return second
static Value clockNative(int argCount, Value* args, C_STR* errorInfo)
{
	if (argCount != 0) {
		*errorInfo = "clock(): Expected 0 arguments but got some";
		return NIL_VAL;
	}
	return NUMBER_VAL((double)clock() / CLOCKS_PER_SEC);
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

void importNative()
{
	defineNative("clock", clockNative);
	defineNative("max", maxNative);
	defineNative("min", minNative);
}
