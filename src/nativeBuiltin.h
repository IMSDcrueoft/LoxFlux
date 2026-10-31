/*
* MIT License
* Copyright (c) 2025-2026 IMSDcrueoft (https://github.com/IMSDcrueoft)
* See LICENSE file in the root directory for full license text.
*/
#pragma once
#include "common.h"
#include "object.h"
#include "table.h"

typedef enum {
	MODULE_MATH,
	MODULE_ARRAY,
	MODULE_OBJECT,
	MODULE_STRING,
	MODULE_TIME,
	MODULE_CTOR,
	MODULE_SYSTEM,

	MODULE_COUNT,//as the size
} BuiltinMouduleType;

void importNative_math(LfxVM* vm);
void importNative_array(LfxVM* vm);
void importNative_object(LfxVM* vm);
void importNative_string(LfxVM* vm);
void importNative_time(LfxVM* vm);
void importNative_ctor(LfxVM* vm);
void importNative_system(LfxVM* vm);
void importNative_global(LfxVM* vm);