/*
 * MIT License
 * Copyright (c) 2025-2026 IMSDcrueoft (https://github.com/IMSDcrueoft)
 * See LICENSE file in the root directory for full license text.
*/
#pragma once
//backend toggle; overridable from the build command line (/DUSE_XXHASH=0)
//for testing the murmur fallback
#ifndef LOXFLUX_USE_XXHASH
#define LOXFLUX_USE_XXHASH 1
#endif

#include <stdint.h>
#include <stddef.h>

#if LOXFLUX_USE_XXHASH

#include <xxh3.h>
#define HASH_64bits(str,len) XXH3_64bits(str, len)

//xxh3.h inlines everything (XXH_INLINE_ALL shim), so XXH3_state_t is fully
//defined and can live on the stack / embedded in other structs; a stack
//state needs no XXH3_INITSTATE because the first reset is default-mode
//(only _withSeed requires it, see xxhash.h XXH3_INITSTATE docs)
#define HASH_BACKEND_XXHASH 1

typedef struct {
	XXH3_state_t state;
} HashStream;

#else

#define HASH_64bits(str,len) MurmurHash64Bits(str, len)
uint64_t MurmurHash64Bits(const void* key, size_t len);

#define HASH_BACKEND_XXHASH 0

//the murmur math needs the total length twice (h = seed ^ len at the start,
//h ^= len before the final mix), so streaming must be told the total upfront;
//trailing bytes (<8) are buffered at their natural LSB shift positions,
//which mirrors the one-shot tail layout exactly
typedef struct {
	uint64_t h;        //rolling state, seeded with seed ^ totalLength
	uint64_t totalLen;
	uint64_t block;    //byte assembly buffer
	uint32_t buffered; //0..7
} HashStream;

#endif

//streaming hash: hashStreamBegin(totalLength) + N * hashStreamUpdate +
//hashStreamFinalize is bit-identical to HASH_64bits over the concatenated
//bytes (dedup pool consistency depends on this invariant)
void hashStreamBegin(HashStream* stream, uint64_t totalLength);
void hashStreamUpdate(HashStream* stream, const void* data, size_t length);
uint64_t hashStreamFinalize(HashStream* stream);

//debug self-check: streaming == one-shot over boundary sizes and every
//two-segment split; no-op under NDEBUG
void hashStreamSelfTest();

#undef LOXFLUX_USE_XXHASH