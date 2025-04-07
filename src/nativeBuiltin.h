/*
* MIT License
* Copyright (c) 2025 IM&SD (https://github.com/IMSDcrueoft)
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
	MODULE_FILE,
	MODULE_SYSTEM,

	MODULE_GLOBAL,//as the size too
} BuiltinMouduleType;

#define BUILTIN_MODULE_COUNT MODULE_GLOBAL

// you must define the size of table in the module ,must be 2^n,and enough for improtNative_xxx
#define BUILTIN_MATH_TABLE_SIZE 16
#define BUILTIN_ARRAY_TABLE_SIZE 16
#define BUILTIN_OBJECT_TABLE_SIZE 16
#define BUILTIN_STRING_TABLE_SIZE 16
#define BUILTIN_FILE_TABLE_SIZE 16
#define BUILTIN_TIME_TABLE_SIZE 8
#define BUILTIN_SYSTEM_TABLE_SIZE 8

void importNative_math();
void importNative_array();
void importNative_object();
void importNative_string();
void importNative_time();
void importNative_file();
void importNative_system();

void importNative_global();