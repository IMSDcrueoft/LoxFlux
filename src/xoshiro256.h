/*
 * MIT License
 * Copyright (c) 2025 IMSDcrueoft (https://github.com/IMSDcrueoft)
 * See LICENSE file in the root directory for full license text.
*/
#pragma once
#include<stdint.h>

typedef struct {
	uint64_t state[4];
} Xoshiro256StarStar;

void xoshiro256starstar_init(Xoshiro256StarStar* rng, uint64_t seed);
uint64_t xoshiro256starstar_next(Xoshiro256StarStar* rng);
double xoshiro256starstar_random(Xoshiro256StarStar* rng);