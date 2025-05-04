/*
* MIT License
* Copyright (c) 2025 IMSDcrueoft (https://github.com/IMSDcrueoft)
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
	for (int i = 0; i < argCount; ++i) {
		if (IS_NUMBER(args[i])) {
			val = max(val, AS_NUMBER(args[i]));
		}
		else {
			val = NAN;
			break;
		}
	}
	return NUMBER_VAL(val);
}

//max with multiple input
static Value minNative(int argCount, Value* args)
{
	double val = INFINITY;
	for (int i = 0; i < argCount; ++i) {
		if (IS_NUMBER(args[i])) {
			val = min(val, AS_NUMBER(args[i]));
		}
		else {
			val = NAN;
			break;
		}
	}
	return NUMBER_VAL(val);
}

//get a random [0,1)
static Value randomNative(int argCount, Value* args) {
	return NUMBER_VAL(well1024a_random());
}

//seed it
static Value seedNative(int argCount, Value* args) {
	if (argCount >= 1 && IS_NUMBER(args[0])) {
		uint32_t seed = (uint32_t)AS_NUMBER(args[0]);
		well1024a_init(seed);
		return BOOL_VAL(true);
	}
	else {
		return BOOL_VAL(false);
	}

	return NAN_VAL;
}

COLD_FUNCTION
static Value isNaNNative(int argCount, Value* args) {
	return BOOL_VAL(argCount >= 1 && IS_NUMBER(args[0]) && isnan(AS_NUMBER(args[0])));
}

COLD_FUNCTION
static Value isFiniteNative(int argCount, Value* args) {
	return BOOL_VAL(argCount >= 1 && IS_NUMBER(args[0]) && isfinite(AS_NUMBER(args[0])));
}

static Value absNative(int argCount, Value* args) {
	if (argCount >= 1 && IS_NUMBER(args[0])) {
		return NUMBER_VAL(fabs(AS_NUMBER(args[0])));
	}
	else {
		return NAN_VAL;
	}
}

static Value floorNative(int argCount, Value* args) {
	if (argCount >= 1 && IS_NUMBER(args[0])) {
		return NUMBER_VAL(floor(AS_NUMBER(args[0])));
	}
	else {
		return NAN_VAL;
	}
}

static Value ceilNative(int argCount, Value* args) {
	if (argCount >= 1 && IS_NUMBER(args[0])) {
		return NUMBER_VAL(ceil(AS_NUMBER(args[0])));
	}
	else {
		return NAN_VAL;
	}
}

static Value roundNative(int argCount, Value* args) {
	if (argCount >= 1 && IS_NUMBER(args[0])) {
		return NUMBER_VAL(round(AS_NUMBER(args[0])));
	}
	else {
		return NAN_VAL;
	}
}

static Value powNative(int argCount, Value* args) {
	if (argCount >= 2 && IS_NUMBER(args[0]) && IS_NUMBER(args[1])) {
		return NUMBER_VAL(pow(AS_NUMBER(args[0]), AS_NUMBER(args[1])));
	}
	else {
		return NAN_VAL;
	}
}

static Value sqrtNative(int argCount, Value* args) {
	if (argCount >= 1 && IS_NUMBER(args[0])) {
		return NUMBER_VAL(sqrt(AS_NUMBER(args[0])));
	}
	else {
		return NAN_VAL;
	}
}

static Value sinNative(int argCount, Value* args) {
	if (argCount >= 1 && IS_NUMBER(args[0])) {
		return NUMBER_VAL(sin(AS_NUMBER(args[0])));
	}
	else {
		return NAN_VAL;
	}
}

static Value asinNative(int argCount, Value* args) {
	if (argCount >= 1 && IS_NUMBER(args[0])) {
		return NUMBER_VAL(asin(AS_NUMBER(args[0])));
	}
	else {
		return NAN_VAL;
	}
}

static Value cosNative(int argCount, Value* args) {
	if (argCount >= 1 && IS_NUMBER(args[0])) {
		return NUMBER_VAL(cos(AS_NUMBER(args[0])));
	}
	else {
		return NAN_VAL;
	}
}

static Value acosNative(int argCount, Value* args) {
	if (argCount >= 1 && IS_NUMBER(args[0])) {
		return NUMBER_VAL(acos(AS_NUMBER(args[0])));
	}
	else {
		return NAN_VAL;
	}
}

static Value tanNative(int argCount, Value* args) {
	if (argCount >= 1 && IS_NUMBER(args[0])) {
		return NUMBER_VAL(tan(AS_NUMBER(args[0])));
	}
	else {
		return NAN_VAL;
	}
}

static Value atanNative(int argCount, Value* args) {
	if (argCount >= 1 && IS_NUMBER(args[0])) {
		return NUMBER_VAL(atan(AS_NUMBER(args[0])));
	}
	else {
		return NAN_VAL;
	}
}

static Value logNative(int argCount, Value* args) {
	if (argCount >= 1 && IS_NUMBER(args[0])) {
		return NUMBER_VAL(log(AS_NUMBER(args[0])));
	}
	else {
		return NAN_VAL;
	}
}

static Value log2Native(int argCount, Value* args) {
	if (argCount >= 1 && IS_NUMBER(args[0])) {
		return NUMBER_VAL(log2(AS_NUMBER(args[0])));
	}
	else {
		return NAN_VAL;
	}
}

static Value log10Native(int argCount, Value* args) {
	if (argCount >= 1 && IS_NUMBER(args[0])) {
		return NUMBER_VAL(log10(AS_NUMBER(args[0])));
	}
	else {
		return NAN_VAL;
	}
}

static Value expNative(int argCount, Value* args) {
	if (argCount >= 1 && IS_NUMBER(args[0])) {
		return NUMBER_VAL(exp(AS_NUMBER(args[0])));
	}
	else {
		return NAN_VAL;
	}
}

COLD_FUNCTION
void importNative_math() {
	well1024a_init64(get_utc_milliseconds());

	defineNative_math("max", maxNative);
	defineNative_math("min", minNative);
	defineNative_math("abs", absNative);
	defineNative_math("floor", floorNative);
	defineNative_math("ceil", ceilNative);
	defineNative_math("round", roundNative);
	defineNative_math("pow", powNative);
	defineNative_math("sqrt", sqrtNative);
	defineNative_math("sin", sinNative);
	defineNative_math("asin", asinNative);
	defineNative_math("cos", cosNative);
	defineNative_math("acos", acosNative);
	defineNative_math("tan", tanNative);
	defineNative_math("atan", atanNative);
	defineNative_math("log", logNative);
	defineNative_math("log2", log2Native);
	defineNative_math("log10", log10Native);
	defineNative_math("exp", expNative);
	defineNative_math("isNaN", isNaNNative);
	defineNative_math("isFinite", isFiniteNative);
	defineNative_math("random", randomNative);
	defineNative_math("seed", seedNative);
}