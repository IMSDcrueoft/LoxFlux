/*
 * MIT License
 * Copyright (c) 2025 IMSDcrueoft (https://github.com/IMSDcrueoft)
 * See LICENSE file in the root directory for full license text.
*/
#pragma once
#include <stdint.h>
#include <float.h>

_Static_assert(sizeof(void*) == 8, "This platform does not have 8-byte pointers. The program requires a 64-bit environment.");

#ifndef INT8_MIN
#error "int8_t type is not supported."
#endif

#ifndef INT16_MIN
#error "int16_t type is not supported."
#endif

#ifndef INT32_MIN
#error "int32_t type is not supported."
#endif

#ifndef INT64_MIN
#error "int64_t type is not supported."
#endif

#ifndef UINT8_MAX
#error "uint8_t type is not supported."
#endif

#ifndef UINT16_MAX
#error "uint16_t type is not supported."
#endif

#ifndef UINT32_MAX
#error "uint32_t type is not supported."
#endif

#ifndef UINT64_MAX
#error "uint64_t type is not supported."
#endif

#ifndef FLT_MIN
#error "float type is not supported."
#endif

#ifndef DBL_MIN
#error "double type is not supported."
#endif