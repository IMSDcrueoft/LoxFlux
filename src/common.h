/*
 * MIT License
 * Copyright (c) 2025 IM&SD (https://github.com/IMSDcrueoft)
 * See LICENSE file in the root directory for full license text.
*/
#pragma once
#include <stddef.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdarg.h>
//check the type
#include "check.h"
#include "options.h"

typedef char* STR;
typedef const char* C_STR;
typedef void* Unknown_ptr;
typedef float float32_t;
typedef double float64_t;

#define UINT8_COUNT 0x100
#define UINT10_MAX 0x3ff
#define UINT10_COUNT 0x400
#define UINT18_MAX 0x3ffff
#define UINT24_MAX 0xffffff
#define UINT24_COUNT 0x1000000

#define min_2(x,y) (((x) < (y)) ? (x): (y))
#define max_2(x,y) (((x) > (y)) ? (x): (y))