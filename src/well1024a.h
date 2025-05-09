/*
 * MIT License
 * Copyright (c) 2025 IMSDcrueoft (https://github.com/IMSDcrueoft)
 * See LICENSE file in the root directory for full license text.
*/
#pragma once
#include <stdint.h>

#define STATE_SIZE 32

typedef struct {
    uint32_t state[STATE_SIZE];
    uint32_t index;
} well1024a;

// init by array
void well1024a_initArray(const uint32_t* init_keys, uint32_t keys_length);
// init by seed
void well1024a_init(uint32_t seed);
void well1024a_init64(uint64_t seed);

uint32_t well1024a_rand();
double well1024a_random();
double well1024a_random53();

#undef STATE_SIZE