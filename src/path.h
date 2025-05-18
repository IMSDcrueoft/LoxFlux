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
#include <stdbool.h>
#include <stdint.h>
#endif

/**
 * @brief get absolute path and return it
 */
char* getAbsolutePath(const char* pathIn);
/**
 * @brief free pathOut from getAbsolutePath
 */
void freePathOut(char** const pathOut);

#ifdef __cplusplus
}
#endif