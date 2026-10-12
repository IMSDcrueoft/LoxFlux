/*
 * MIT License
 * Copyright (c) 2026 IMSDcrueoft (https://github.com/IMSDcrueoft)
 * See LICENSE file in the root directory for full license text.
*/
#include "bits.h"
#if defined(_MSC_VER)
#include <intrin.h> // Ensure this header is included for MSVC intrinsic functions  
#endif

size_t bits_ceil8(uint8_t x)
{
    if (x == 0) return 1;

	--x;

	x |= x >> 1;
	x |= x >> 2;
	x |= x >> 4;

	return x + 1;
}

size_t bits_ceil16(uint16_t x)
{
	if (x == 0) return 1;

	--x;

	x |= x >> 1;
	x |= x >> 2;
	x |= x >> 4;
	x |= x >> 8;

	return x + 1;
}

size_t bits_ceil32(uint32_t x)
{
	if (x == 0) return 1;

	--x;

	x |= x >> 1;
	x |= x >> 2;
	x |= x >> 4;
	x |= x >> 8;
	x |= x >> 16;

	return x + 1;
}

size_t bits_ceil64(uint64_t x)
{
	if (x == 0) return 1;

	--x;

	x |= x >> 1;
	x |= x >> 2;
	x |= x >> 4;
	x |= x >> 8;
	x |= x >> 16;
	x |= x >> 32;

	return x + 1;
}

/* popcnt/ctz/clz are static-inlined in bits.h for GCC/Clang builds; the extern
 * definitions below exist only for compilers without the header inline */
#if !defined(__clang__) && !defined(__GNUC__)

size_t bits_popcnt64(uint64_t x)
{
#if defined(_MSC_VER) && defined(_M_X64)
	return __popcnt64(x);
#else
	/* portable software implementation: no CPU feature dependency (POPCNT
	 * requires SSE4.2 hardware; the 32-bit MSVC target has no 64-bit intrinsic) */
	x = (x & 0x5555555555555555ULL) + ((x >> 1) & 0x5555555555555555ULL);
	x = (x & 0x3333333333333333ULL) + ((x >> 2) & 0x3333333333333333ULL);
	x = (x & 0x0F0F0F0F0F0F0F0FULL) + ((x >> 4) & 0x0F0F0F0F0F0F0F0FULL);
	x = (x * 0x0101010101010101ULL) >> 56;
	return (size_t)x;
#endif
}

size_t bits_ctz64(uint64_t x)
{
#if defined(_MSC_VER) && defined(_M_X64)
	unsigned long index;
	if (_BitScanForward64(&index, x))
		return index;
	else
		return 64;
#elif defined(_MSC_VER)
	/* 32-bit split: _BitScanForward exists on x86 (and ARM); the 64-bit variant does not */
	unsigned long index;
	uint32_t low = (uint32_t)x;
	if (low != 0) {
		_BitScanForward(&index, low);
		return (size_t)index;
	}
	uint32_t high = (uint32_t)(x >> 32);
	if (high != 0) {
		_BitScanForward(&index, high);
		return (size_t)index + 32;
	}
	return 64;
#else
	if (x == 0) return 64;
	uint8_t n = 0;
	if ((x & 0xFFFFFFFF) == 0) { n += 32; x >>= 32; }
	if ((x & 0xFFFF) == 0) { n += 16; x >>= 16; }
	if ((x & 0xFF) == 0) { n += 8; x >>= 8; }
	if ((x & 0xF) == 0) { n += 4; x >>= 4; }
	if ((x & 0x3) == 0) { n += 2; x >>= 2; }
	if ((x & 0x1) == 0) { n += 1; }
	return n;
#endif
}

size_t bits_clz64(uint64_t x)
{
#if defined(_MSC_VER) && defined(_M_X64)
	unsigned long index;
	if (_BitScanReverse64(&index, x))
		return (63 - index);
	else
		return 64;
#elif defined(_MSC_VER)
	/* 32-bit split: _BitScanReverse exists on x86 (and ARM); the 64-bit variant does not */
	unsigned long index;
	uint32_t high = (uint32_t)(x >> 32);
	if (high != 0) {
		_BitScanReverse(&index, high);
		return 31 - (size_t)index;
	}
	uint32_t low = (uint32_t)x;
	if (low != 0) {
		_BitScanReverse(&index, low);
		return 63 - (size_t)index;
	}
	return 64;
#else
	if (x == 0) return 64;
	uint8_t n = 0;
	if ((x >> 32) == 0) { n += 32; }
	else { x >>= 32; }
	if ((x >> 16) == 0) { n += 16; }
	else { x >>= 16; }
	if ((x >> 8) == 0) { n += 8; }
	else { x >>= 8; }
	if ((x >> 4) == 0) { n += 4; }
	else { x >>= 4; }
	if ((x >> 2) == 0) { n += 2; }
	else { x >>= 2; }
	if ((x >> 1) == 0) { n += 1; }
	return n;
#endif
}

#endif /* !__clang__ && !__GNUC__ */