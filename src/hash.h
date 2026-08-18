/*
 * MIT License
 * Copyright (c) 2025 IMSDcrueoft (https://github.com/IMSDcrueoft)
 * See LICENSE file in the root directory for full license text.
*/
#pragma once
#define USE_XXHASH 1

#include <stdint.h>
#include <stddef.h>

#if USE_XXHASH

#include <xxh3.h>
#define HASH_64bits(str,len) XXH3_64bits(str, len)

#else

#define HASH_64bits(str,len) MurmurHash64Bits(str, len)
uint64_t MurmurHash64Bits(const void* key, size_t len);
#endif

#undef USE_XXHASH