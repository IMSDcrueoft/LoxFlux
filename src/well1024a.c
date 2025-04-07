/*
 * MIT License
 * Copyright (c) 2025 IM&SD (https://github.com/IMSDcrueoft)
 * See LICENSE file in the root directory for full license text.
*/
#include "well1024a.h"

#define STATE_SIZE 32

//the global shared random engine
well1024a well1024;

// init
void well1024a_initArray(const uint32_t* init_keys, uint32_t keys_length) {
	uint32_t* state = well1024.state;

	keys_length = (keys_length < STATE_SIZE) ? keys_length : STATE_SIZE;
	for (uint32_t i = 0, s_; i < STATE_SIZE; ++i) {
		if (i < keys_length) {
			state[i] = init_keys[i];
		}
		else {
			s_ = state[i - 1] ^ (state[i - 1] >> 30);
			state[i] = (((((s_ & 0xffff0000) >> 16) * 1812433253) << 16) + ((s_ & 0x0000ffff) * 1812433253)) + i;
		}
	}
}

void well1024a_init(uint32_t seed) {
	well1024a_initArray((uint32_t[]) { seed }, 1);
	well1024.index = 0; // reset
}

void well1024a_init64(uint64_t seed) {
	well1024a_initArray((uint32_t[]) { seed >> 32, seed & UINT32_MAX }, 2);
	well1024.index = 0; // reset
}

uint32_t well1024a_rand() {
	uint32_t* state = well1024.state;
	uint32_t index = well1024.index;

	uint32_t v_m1 = state[(index + 3) & 31];
	uint32_t v_m2 = state[(index + 24) & 31];
	uint32_t v_m3 = state[(index + 10) & 31];
	uint32_t z0 = state[(index + 31) & 31];

	uint32_t z1 = z0 ^ (v_m1 ^ (v_m1 >> 8));
	uint32_t z2 = v_m2 ^ (v_m2 << 19) ^ v_m3 ^ (v_m3 << 14);

	state[index] = z1 ^ z2;
	uint32_t nextIndex = (index + 31) & 31;
	state[nextIndex] = (z0 ^ (z0 << 11) ^ z1 ^ (z1 << 7) ^ z2 ^ (z2 << 13));

	well1024.index = nextIndex;

	return state[nextIndex];
}

double well1024a_random() {
	return  well1024a_rand() * (1.0 / 4294967296.0);
}

double well1024a_random53() {
	uint64_t a = well1024a_rand() >> 5;  // 27
	uint64_t b = well1024a_rand() >> 6;  // 26

	uint64_t combined = (a << 26) | b;

	// 53 bit to [0, 1)
	return (double)combined * (1.0 / 9007199254740992.0); // 1.0 / (2^53)
}

#undef STATE_SIZE