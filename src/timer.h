/*
 * MIT License
 * Copyright (c) 2025 IMSDcrueoft (https://github.com/IMSDcrueoft)
 * See LICENSE file in the root directory for full license text.
*/
#pragma once
#ifdef __cplusplus
#include <cstdint>
extern "C" {
#else
#include <stdint.h>

#endif
int64_t get_nanoseconds();
int64_t get_microseconds();
int64_t get_milliseconds();
int64_t get_seconds();
int64_t get_utc_milliseconds();

#ifdef __cplusplus
}
#endif