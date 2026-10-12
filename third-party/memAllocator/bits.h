/*
 * MIT License
 * Copyright (c) 2026 IMSDcrueoft (https://github.com/IMSDcrueoft)
 * See LICENSE file in the root directory for full license text.
*/
#pragma once
#ifdef __cplusplus
#include <cstddef>
#include <cstdint>
#include<type_traits>

template <typename T, typename = std::enable_if<std::is_integral_v<T>>>
static inline T bits_ceil(T x) {
	if (x == 0) return 1;

	--x;

	x |= x >> 1;
	x |= x >> 2;
	x |= x >> 4;

	if constexpr (sizeof(T) > 1) x |= x >> 8;
	if constexpr (sizeof(T) > 2) x |= x >> 16;
	if constexpr (sizeof(T) > 4) x |= x >> 32;

	return x + 1;
}
#else
#include <stddef.h>
#include <stdint.h>

size_t bits_ceil8(uint8_t x);
size_t bits_ceil16(uint16_t x);
size_t bits_ceil32(uint32_t x);
size_t bits_ceil64(uint64_t x);

#define bits_ceil(x) _Generic((x), \
    uint8_t:  bits_ceil8,  \
    uint16_t: bits_ceil16, \
    uint32_t: bits_ceil32, \
    uint64_t: bits_ceil64  \
)(x)
#endif

#if defined(__clang__) || defined(__GNUC__)
	/* single-instruction inline for GCC/Clang builds; on BMI1 targets tzcnt(0) == 64,
	 * so the zero guard of ctz/clz folds away entirely (no call, no branch) */
	static inline size_t bits_popcnt64(uint64_t x) { return (size_t)__builtin_popcountll(x); }
	static inline size_t bits_ctz64(uint64_t x) { return x ? (size_t)__builtin_ctzll(x) : 64; }
	static inline size_t bits_clz64(uint64_t x) { return x ? (size_t)__builtin_clzll(x) : 64; }
#else
#	ifdef __cplusplus
extern "C" {
#	endif
	size_t bits_popcnt64(uint64_t x);
	size_t bits_ctz64(uint64_t x);
	size_t bits_clz64(uint64_t x);
#	ifdef __cplusplus
}
#	endif
#endif

#define bits_set_one(value, bitIdx) ((value) |= ((uint64_t)1u << (bitIdx)))
#define bits_set_zero(value, bitIdx) ((value) &= ~((uint64_t)1u << (bitIdx)))
#define bits_get(value, bitIdx) (((value) >> (bitIdx)) & (uint64_t)1u)

#if defined(__clang__) || defined(__GNUC__)  
// GCC / Clang / Linux / macOS / iOS / Android  
#define OFFSET_OF(type, member) __builtin_offsetof(type, member)
#else
#define OFFSET_OF(type, member) offsetof(type, member)
#endif