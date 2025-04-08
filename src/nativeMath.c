/*
* MIT License
* Copyright (c) 2025 IM&SD (https://github.com/IMSDcrueoft)
* See LICENSE file in the root directory for full license text.
*/
#include "nativeBuiltin.h"
#include "well1024a.h"
#include "timer.h"
#include "vm.h"

//Math
//max with multiple input
static Value maxNative(int argCount, Value* args, C_STR* errorInfo)
{
	if (argCount < 2) {
		*errorInfo = "max(): Expected at least 2 arguments";
		return NAN_VAL;
	}

	if (!(IS_NUMBER(args[0])) || !IS_NUMBER(args[1])) {
		return NAN_VAL;
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
		return NAN_VAL;
	}

	if (!(IS_NUMBER(args[0])) || !IS_NUMBER(args[1])) {
		return NAN_VAL;
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

//get a random [0,1)
static Value randomNative(int argCount, Value* args, C_STR* errorInfo) {
	if (argCount != 0) {
		*errorInfo = "random(): Expected 0 arguments but got some";
		return NAN_VAL;
	}

	return NUMBER_VAL(well1024a_random());
}

//seed it
static Value seedNative(int argCount, Value* args, C_STR* errorInfo) {
	if (argCount != 1) {
		if (argCount == 0) {
			*errorInfo = "seed(): Expected 1 arguments but got none";
		}
		else {
			*errorInfo = "seed(): Expected 1 arguments but got more";
		}
		return NIL_VAL;
	}

	if (args[0].type != VAL_NUMBER) {
		*errorInfo = "seed(): The parameter must be a number";
		return NIL_VAL;
	}
	uint32_t seed = (uint32_t)AS_NUMBER(args[0]);
	well1024a_init(seed);

	return NIL_VAL;
}

COLD_FUNCTION
void importNative_math() {
	well1024a_init64(get_utc_milliseconds());

	defineNative_math("max", maxNative);
	defineNative_math("min", minNative);
	defineNative_math("random", randomNative);
	defineNative_math("seed", seedNative);
}