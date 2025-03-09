/*
 * MIT License
 * Copyright (c) 2025 IM&SD (https://github.com/IMSDcrueoft)
 * See LICENSE file in the root directory for full license text.
*/
#pragma once
#include "common.h"

typedef enum {
	MODULE_MATH,
	MODULE_ARRAY,
	MODULE_OBJECT,
	MODULE_STRING,
	MODULE_TIME,
	MODULE_FILE,
	MODULE_SYSTEM,

	MODULE_GLOBAL,//as the size too
} BuiltinMouduleType;

#define BUILTIN_MODULE_COUNT MODULE_GLOBAL