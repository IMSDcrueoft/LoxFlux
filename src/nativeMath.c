/*
* MIT License
* Copyright (c) 2025-2026 IMSDcrueoft (https://github.com/IMSDcrueoft)
* See LICENSE file in the root directory for full license text.
*/
#include "nativeBuiltin.h"
#include "xoshiro256.h"
#include "timer.h"
#include "vm.h"

Xoshiro256StarStar rng;

//Math
//max with multiple input
static LfxValue maxNative(LfxVM* vm, int argCount, LfxValue* args)
{
	(void)vm;
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

//min with multiple input
static LfxValue minNative(LfxVM* vm, int argCount, LfxValue* args)
{
	(void)vm;
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
static LfxValue randomNative(LfxVM* vm, int argCount, LfxValue* args) {
	(void)vm;
	return NUMBER_VAL(xoshiro256starstar_random(&rng));
}

//seed it
static LfxValue seedNative(LfxVM* vm, int argCount, LfxValue* args) {
	(void)vm;
	if (argCount >= 1 && IS_NUMBER(args[0])) {
		uint32_t seed = double_to_u32(AS_NUMBER(args[0]));
		xoshiro256starstar_init(&rng, seed);
		return BOOL_VAL(true);
	}
	else {
		return BOOL_VAL(false);
	}
}

COLD_FUNCTION
static LfxValue isNaNNative(LfxVM* vm, int argCount, LfxValue* args) {
	(void)vm;
	return BOOL_VAL(argCount >= 1 && IS_NUMBER(args[0]) && isnan(AS_NUMBER(args[0])));
}

COLD_FUNCTION
static LfxValue isFiniteNative(LfxVM* vm, int argCount, LfxValue* args) {
	(void)vm;
	return BOOL_VAL(argCount >= 1 && IS_NUMBER(args[0]) && isfinite(AS_NUMBER(args[0])));
}

static LfxValue absNative(LfxVM* vm, int argCount, LfxValue* args) {
	(void)vm;
	if (argCount >= 1 && IS_NUMBER(args[0])) {
		return NUMBER_VAL(fabs(AS_NUMBER(args[0])));
	}
	else {
		return NAN_VAL;
	}
}

static LfxValue floorNative(LfxVM* vm, int argCount, LfxValue* args) {
	(void)vm;
	if (argCount >= 1 && IS_NUMBER(args[0])) {
		return NUMBER_VAL(floor(AS_NUMBER(args[0])));
	}
	else {
		return NAN_VAL;
	}
}

static LfxValue ceilNative(LfxVM* vm, int argCount, LfxValue* args) {
	(void)vm;
	if (argCount >= 1 && IS_NUMBER(args[0])) {
		return NUMBER_VAL(ceil(AS_NUMBER(args[0])));
	}
	else {
		return NAN_VAL;
	}
}

static LfxValue roundNative(LfxVM* vm, int argCount, LfxValue* args) {
	(void)vm;
	if (argCount >= 1 && IS_NUMBER(args[0])) {
		return NUMBER_VAL(round(AS_NUMBER(args[0])));
	}
	else {
		return NAN_VAL;
	}
}

static LfxValue powNative(LfxVM* vm, int argCount, LfxValue* args) {
	(void)vm;
	if (argCount >= 2 && IS_NUMBER(args[0]) && IS_NUMBER(args[1])) {
		return NUMBER_VAL(pow(AS_NUMBER(args[0]), AS_NUMBER(args[1])));
	}
	else {
		return NAN_VAL;
	}
}

static LfxValue sqrtNative(LfxVM* vm, int argCount, LfxValue* args) {
	(void)vm;
	if (argCount >= 1 && IS_NUMBER(args[0])) {
		return NUMBER_VAL(sqrt(AS_NUMBER(args[0])));
	}
	else {
		return NAN_VAL;
	}
}

static LfxValue sinNative(LfxVM* vm, int argCount, LfxValue* args) {
	(void)vm;
	if (argCount >= 1 && IS_NUMBER(args[0])) {
		return NUMBER_VAL(sin(AS_NUMBER(args[0])));
	}
	else {
		return NAN_VAL;
	}
}

static LfxValue asinNative(LfxVM* vm, int argCount, LfxValue* args) {
	(void)vm;
	if (argCount >= 1 && IS_NUMBER(args[0])) {
		return NUMBER_VAL(asin(AS_NUMBER(args[0])));
	}
	else {
		return NAN_VAL;
	}
}

static LfxValue cosNative(LfxVM* vm, int argCount, LfxValue* args) {
	(void)vm;
	if (argCount >= 1 && IS_NUMBER(args[0])) {
		return NUMBER_VAL(cos(AS_NUMBER(args[0])));
	}
	else {
		return NAN_VAL;
	}
}

static LfxValue acosNative(LfxVM* vm, int argCount, LfxValue* args) {
	(void)vm;
	if (argCount >= 1 && IS_NUMBER(args[0])) {
		return NUMBER_VAL(acos(AS_NUMBER(args[0])));
	}
	else {
		return NAN_VAL;
	}
}

static LfxValue tanNative(LfxVM* vm, int argCount, LfxValue* args) {
	(void)vm;
	if (argCount >= 1 && IS_NUMBER(args[0])) {
		return NUMBER_VAL(tan(AS_NUMBER(args[0])));
	}
	else {
		return NAN_VAL;
	}
}

static LfxValue atanNative(LfxVM* vm, int argCount, LfxValue* args) {
	(void)vm;
	if (argCount >= 1 && IS_NUMBER(args[0])) {
		return NUMBER_VAL(atan(AS_NUMBER(args[0])));
	}
	else {
		return NAN_VAL;
	}
}

static LfxValue logNative(LfxVM* vm, int argCount, LfxValue* args) {
	(void)vm;
	if (argCount >= 1 && IS_NUMBER(args[0])) {
		return NUMBER_VAL(log(AS_NUMBER(args[0])));
	}
	else {
		return NAN_VAL;
	}
}

static LfxValue log2Native(LfxVM* vm, int argCount, LfxValue* args) {
	(void)vm;
	if (argCount >= 1 && IS_NUMBER(args[0])) {
		return NUMBER_VAL(log2(AS_NUMBER(args[0])));
	}
	else {
		return NAN_VAL;
	}
}

static LfxValue log10Native(LfxVM* vm, int argCount, LfxValue* args) {
	(void)vm;
	if (argCount >= 1 && IS_NUMBER(args[0])) {
		return NUMBER_VAL(log10(AS_NUMBER(args[0])));
	}
	else {
		return NAN_VAL;
	}
}

static LfxValue expNative(LfxVM* vm, int argCount, LfxValue* args) {
	(void)vm;
	if (argCount >= 1 && IS_NUMBER(args[0])) {
		return NUMBER_VAL(exp(AS_NUMBER(args[0])));
	}
	else {
		return NAN_VAL;
	}
}

COLD_FUNCTION
void importNative_math(LfxVM* vm) {
	xoshiro256starstar_init(&rng, get_utc_milliseconds());

	defineNative_math_number(vm, "NaN", NAN);
	defineNative_math_number(vm, "Infinity", INFINITY);
	defineNative_math_number(vm, "EPSILON", DBL_EPSILON);

	defineNative_math_number(vm, "MAX_VALUE", DBL_MAX);
	defineNative_math_number(vm, "MIN_VALUE", DBL_TRUE_MIN);   // C11, 5e-324
	defineNative_math_number(vm, "MAX_SAFE_INTEGER", 9007199254740991.0);   // 2^53 - 1
	defineNative_math_number(vm, "MIN_SAFE_INTEGER", -9007199254740991.0);  // -(2^53 - 1)

	defineNative_math_number(vm, "E", 2.7182818284590452354);
	defineNative_math_number(vm, "LN2", 0.69314718055994530942);
	defineNative_math_number(vm, "LN10", 2.30258509299404568402);
	defineNative_math_number(vm, "LOG2E", 1.4426950408889634074);
	defineNative_math_number(vm, "LOG10E", 0.43429448190325182765);
	defineNative_math_number(vm, "PI", 3.14159265358979323846);
	defineNative_math_number(vm, "SQRT1_2", 0.70710678118654752440);
	defineNative_math_number(vm, "SQRT2", 1.41421356237309504880);

	defineNative_math(vm, "max", maxNative);
	defineNative_math(vm, "min", minNative);
	defineNative_math(vm, "abs", absNative);
	defineNative_math(vm, "floor", floorNative);
	defineNative_math(vm, "ceil", ceilNative);
	defineNative_math(vm, "round", roundNative);
	defineNative_math(vm, "pow", powNative);
	defineNative_math(vm, "sqrt", sqrtNative);
	defineNative_math(vm, "sin", sinNative);
	defineNative_math(vm, "asin", asinNative);
	defineNative_math(vm, "cos", cosNative);
	defineNative_math(vm, "acos", acosNative);
	defineNative_math(vm, "tan", tanNative);
	defineNative_math(vm, "atan", atanNative);
	defineNative_math(vm, "log", logNative);
	defineNative_math(vm, "log2", log2Native);
	defineNative_math(vm, "log10", log10Native);
	defineNative_math(vm, "exp", expNative);
	defineNative_math(vm, "isNaN", isNaNNative);
	defineNative_math(vm, "isFinite", isFiniteNative);
	defineNative_math(vm, "random", randomNative);
	defineNative_math(vm, "seed", seedNative);
}
