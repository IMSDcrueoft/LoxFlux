/*
* MIT License
* Copyright (c) 2025 IM&SD (https://github.com/IMSDcrueoft)
* See LICENSE file in the root directory for full license text.
*/
#include "nativeBuiltin.h"
#include "vm.h"
#include "timer.h"

//Time
//return nano second
static Value nanoNative(int argCount, Value* args, C_STR* errorInfo)
{
	if (argCount != 0) {
		*errorInfo = "nano(): Expected 0 arguments but got some";
		return NAN_VAL;
	}
	return NUMBER_VAL((double)get_nanoseconds());
}

//return micro second
static Value microNative(int argCount, Value* args, C_STR* errorInfo)
{
	if (argCount != 0) {
		*errorInfo = "micro(): Expected 0 arguments but got some";
		return NAN_VAL;
	}
	return NUMBER_VAL((double)get_nanoseconds() * 1e-3);
}

//return milli second-
static Value milliNative(int argCount, Value* args, C_STR* errorInfo)
{
	if (argCount != 0) {
		*errorInfo = "milli(): Expected 0 arguments but got some";
		return NAN_VAL;
	}
	return NUMBER_VAL((double)get_nanoseconds() * 1e-6);
}

//return second
static Value secondNative(int argCount, Value* args, C_STR* errorInfo)
{
	if (argCount != 0) {
		*errorInfo = "second(): Expected 0 arguments but got some";
		return NAN_VAL;
	}
	return NUMBER_VAL((double)get_nanoseconds() * 1e-9);
}

COLD_FUNCTION
void importNative_time() {
	defineNative_time("nano", nanoNative);
	defineNative_time("micro", microNative);
	defineNative_time("milli", milliNative);
	defineNative_time("second", secondNative);
}