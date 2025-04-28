/*
* MIT License
* Copyright (c) 2025 IMSDcrueoft (https://github.com/IMSDcrueoft)
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

	MODULE_GLOBAL,//as the size too
} BuiltinMouduleType;

#define BUILTIN_MODULE_COUNT MODULE_GLOBAL

void importNative_math();
void importNative_array();
void importNative_object();
void importNative_string();
void importNative_time();
void importNative_ctor();
void importNative_system();
void importNative_global();