/*
* MIT License
* Copyright (c) 2025-2026 IMSDcrueoft (https://github.com/IMSDcrueoft)
* See LICENSE file in the root directory for full license text.
*/
#include "nativeBuiltin.h"
#include "vm.h"
#include "timer.h"

//Time
//return nano second
static LfxValue nanoNative(LfxVM* vm, int argCount, LfxValue* args)
{
	(void)vm;
	return NUMBER_VAL((double)get_nanoseconds());
}

//return micro second
static LfxValue microNative(LfxVM* vm, int argCount, LfxValue* args)
{
	(void)vm;
	return NUMBER_VAL((double)get_microseconds());
}

//return milli second-
static LfxValue milliNative(LfxVM* vm, int argCount, LfxValue* args)
{
	(void)vm;
	return NUMBER_VAL((double)get_milliseconds());
}

//return second
static LfxValue secondNative(LfxVM* vm, int argCount, LfxValue* args)
{
	(void)vm;
	return NUMBER_VAL((double)get_seconds());
}

static LfxValue utcMilliNative(LfxVM* vm, int argCount, LfxValue* args) {
	(void)vm;
	return NUMBER_VAL((double)get_utc_milliseconds());
}

COLD_FUNCTION
void importNative_time(LfxVM* vm) {
	defineNative_time(vm, "nano", nanoNative);
	defineNative_time(vm, "micro", microNative);
	defineNative_time(vm, "milli", milliNative);
	defineNative_time(vm, "second", secondNative);
	defineNative_time(vm, "utc", utcMilliNative);
}
