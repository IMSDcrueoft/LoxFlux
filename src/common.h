/*
 * MIT License
 * Copyright (c) 2025 IMSDcrueoft (https://github.com/IMSDcrueoft)
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
#include <float.h>

_Static_assert(sizeof(void*) == 8, "This platform does not have 8-byte pointers. The program requires a 64-bit environment.");

#include "options.h"
#include "optimize.h"

typedef char* STR;
typedef const char* C_STR;

#define UINT8_COUNT 0x100
#define UINT10_MAX 0x3ff
#define UINT10_COUNT 0x400
#define UINT24_MAX 0xffffff
#define UINT24_COUNT 0x1000000