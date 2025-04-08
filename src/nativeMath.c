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
static Value maxNative(int argCount, Value* args)
{
	double val = -INFINITY;
	for (uint32_t i = 0; i < argCount; ++i) {
		if (!(IS_NUMBER(args[i]))) {
			val = NAN;
			break;
		}

		val = max(val, AS_NUMBER(args[i]));
	}
	return NUMBER_VAL(val);
}

//max with multiple input
static Value minNative(int argCount, Value* args)
{
	double val = INFINITY;
	for (uint32_t i = 0; i < argCount; ++i) {
		if (!(IS_NUMBER(args[i]))) {
			val = NAN;
			break;
		}

		val = min(val, AS_NUMBER(args[i]));
	}

	return NUMBER_VAL(val);
}

//get a random [0,1)
static Value randomNative(int argCount, Value* args) {
	return NUMBER_VAL(well1024a_random());
}

//seed it
static Value seedNative(int argCount, Value* args) {
	if (argCount != 1 || (args[0].type != VAL_NUMBER)) {
		return NIL_VAL;
	}

	uint32_t seed = (uint32_t)AS_NUMBER(args[0]);
	well1024a_init(seed);

	return NIL_VAL;
}

static Value isNaNNative(int argCount, Value* args) {
	return BOOL_VAL((argCount >= 1 || (args[0].type == VAL_NUMBER)) && isnan(AS_NUMBER(args[0])));
}

static Value isInf(int argCount, Value* args) {
	return BOOL_VAL((argCount >= 1 || (args[0].type == VAL_NUMBER)) && isinf(AS_NUMBER(args[0])));
}

COLD_FUNCTION
void importNative_math() {
	well1024a_init64(get_utc_milliseconds());

	defineNative_math("max", maxNative);
	defineNative_math("min", minNative);
	defineNative_math("random", randomNative);
	defineNative_math("seed", seedNative);
	defineNative_math("isNaN", isNaNNative);
	defineNative_math("isInf", isInf);
}