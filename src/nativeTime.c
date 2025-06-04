/*
* MIT License
* Copyright (c) 2025 IMSDcrueoft (https://github.com/IMSDcrueoft)
* See LICENSE file in the root directory for full license text.
*/
#include "nativeBuiltin.h"
#include "vm.h"
#include "timer.h"

//Time
//return nano second
static Value nanoNative(int argCount, Value* args)
{
	return NUMBER_VAL((double)get_nanoseconds());
}

//return micro second
static Value microNative(int argCount, Value* args)
{
	return NUMBER_VAL((double)get_microseconds());
}

//return utc second
static Value utcSecondNative(int argCount, Value* args)
{
	return NUMBER_VAL((double)get_utc_seconds());
}

//return utc milli
static Value utcMilliNative(int argCount, Value* args) {
	return NUMBER_VAL((double)get_utc_milliseconds());
}

COLD_FUNCTION
void importNative_time() {
	defineNative_time("nano", nanoNative);
	defineNative_time("micro", microNative);
	defineNative_time("utc_milli", utcMilliNative);
	defineNative_time("utc_second", utcSecondNative);
}