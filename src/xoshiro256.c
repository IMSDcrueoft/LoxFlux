/*
 * MIT License
 * Copyright (c) 2025 IMSDcrueoft (https://github.com/IMSDcrueoft)
 * See LICENSE file in the root directory for full license text.
*/
#include "xoshiro256.h"

void xoshiro256starstar_init(Xoshiro256StarStar* rng, uint64_t seed) {
    uint64_t* s = rng->state;

    //SplitMix64 
    for (int i = 0; i < 4; ++i) {
        seed += 0x9E3779B97F4A7C15;
        uint64_t z = seed;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EB;
        s[i] = z ^ (z >> 31);
    }
}

// rotate left
static inline uint64_t rotl(const uint64_t x, int k) {
    return (x << k) | (x >> (64 - k));
}

// gen rand
uint64_t xoshiro256starstar_next(Xoshiro256StarStar* rng) {
    uint64_t* s = rng->state;

    const uint64_t result = rotl(s[1] * 5, 7) * 9;
    const uint64_t t = s[1] << 17;

    s[2] ^= s[0];
    s[3] ^= s[1];
    s[1] ^= s[2];
    s[0] ^= s[3];
    s[2] ^= t;
    s[3] = rotl(s[3], 45);

    return result;
}

double xoshiro256starstar_random(Xoshiro256StarStar* rng)
{
	uint64_t v = xoshiro256starstar_next(rng);
	return (double)(v >> 11) * 0x1.0p-53; // 1.0 / (2^53)
}