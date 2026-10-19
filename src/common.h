/*
 * MIT License
 * Copyright (c) 2025-2026 IMSDcrueoft (https://github.com/IMSDcrueoft)
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

#if UINTPTR_MAX == UINT64_MAX
#define LOXFLUX_BUILD_64BIT 1
#else
#define LOXFLUX_BUILD_64BIT 0
#endif

#define UINT8_COUNT 0x100
#define UINT24_MAX 0xffffff
#define UINT24_COUNT 0x1000000

#ifndef max
#define max(a,b) (((a) > (b)) ? (a) : (b))
#endif

#ifndef min
#define min(a,b) (((a) < (b)) ? (a) : (b))
#endif

typedef char* STR;
typedef const char* C_STR;

#include "options.h"
#include "optimize.h"