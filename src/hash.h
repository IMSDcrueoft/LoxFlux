/*
 * MIT License
 * Copyright (c) 2025 IM&SD (https://github.com/IMSDcrueoft)
 * See LICENSE file in the root directory for full license text.
*/
#pragma once
#include <xxh3.h>

#define HASH_64bits(str,len) XXH3_64bits(str, len)