#include "hash.h"
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <assert.h>

static uint64_t rotl64(uint64_t x, int8_t r) {
    return (x << r) | (x >> (64 - r));
}

static uint64_t fmix64(uint64_t k) {
    k ^= k >> 33;
    k *= 0xff51afd7ed558ccdULL;
    k ^= k >> 33;
    k *= 0xc4ceb9fe1a85ec53ULL;
    k ^= k >> 33;
    return k;
}

uint64_t MurmurHash64Bits(const void* key, size_t len) {
    const uint8_t* data = (const uint8_t*)key;
    const size_t nblocks = len / 8;

    uint64_t h = 0x9368e53c2f6af274ULL ^ len;
    const uint64_t c1 = 0x87c37b91114253d5ULL;
    const uint64_t c2 = 0x4cf5ad432745937fULL;

    const uint64_t* blocks = (const uint64_t*)(data);

    for (size_t i = 0; i < nblocks; i++) {
        uint64_t k = blocks[i];

        k *= c1;
        k = rotl64(k, 31);
        k *= c2;

        h ^= k;
        h = rotl64(h, 27);
        h = h * 5 + 0x52dce729;
    }

    const uint8_t* tail = (const uint8_t*)(data + nblocks * 8);
    uint64_t k = 0;

    switch (len & 7) {
    case 7: k ^= (uint64_t)tail[6] << 48;
    case 6: k ^= (uint64_t)tail[5] << 40;
    case 5: k ^= (uint64_t)tail[4] << 32;
    case 4: k ^= (uint64_t)tail[3] << 24;
    case 3: k ^= (uint64_t)tail[2] << 16;
    case 2: k ^= (uint64_t)tail[1] << 8;
    case 1: k ^= (uint64_t)tail[0];
        k *= c1;
        k = rotl64(k, 31);
        k *= c2;
        h ^= k;
    }

    h ^= len;
    h = fmix64(h);

    return h;
}

// ---------------------------------------------------------------------------
// streaming hash: begin(totalLength) + N * update + finalize must reproduce
// HASH_64bits over the concatenated bytes bit-for-bit (dedup pool consistency)
// ---------------------------------------------------------------------------

#if !HASH_BACKEND_XXHASH

//mix one full 8-byte block into the rolling state (mirrors the one-shot loop)
static void murmurStreamBlock(HashStream* stream, uint64_t k) {
    const uint64_t c1 = 0x87c37b91114253d5ULL;
    const uint64_t c2 = 0x4cf5ad432745937fULL;

    k *= c1;
    k = rotl64(k, 31);
    k *= c2;

    stream->h ^= k;
    stream->h = rotl64(stream->h, 27);
    stream->h = stream->h * 5 + 0x52dce729;
}

#endif

void hashStreamBegin(HashStream* stream, uint64_t totalLength) {
#if HASH_BACKEND_XXHASH
    (void)totalLength;//xxh3 derives the length from the updates themselves
    XXH3_64bits_reset(&stream->state);
#else
    stream->h = 0x9368e53c2f6af274ULL ^ totalLength;
    stream->totalLen = totalLength;
    stream->block = 0;
    stream->buffered = 0;
#endif
}

void hashStreamUpdate(HashStream* stream, const void* data, size_t length) {
#if HASH_BACKEND_XXHASH
    XXH3_64bits_update(&stream->state, data, length);
#else
    //bytes are assembled little-endian so the assembled block/tail values
    //match the one-shot reads (the one-shot is LE-based as well)
    const uint8_t* byte = (const uint8_t*)data;

    while (length-- > 0) {
        stream->block |= (uint64_t)(*byte++) << (stream->buffered * 8);
        ++stream->buffered;

        if (stream->buffered == 8) {
            murmurStreamBlock(stream, stream->block);
            stream->block = 0;
            stream->buffered = 0;
        }
    }
#endif
}

uint64_t hashStreamFinalize(HashStream* stream) {
#if HASH_BACKEND_XXHASH
    return XXH3_64bits_digest(&stream->state);//non-destructive, repeatable
#else
    const uint64_t c1 = 0x87c37b91114253d5ULL;
    const uint64_t c2 = 0x4cf5ad432745937fULL;

    //whatever is left in the buffer is exactly the one-shot tail: each byte
    //already sits at its shift position, absent high bytes are zero; an empty
    //buffer (len % 8 == 0) leaves k = 0, matching the one-shot's skipped tail
    //works on a local copy so finalize is non-destructive, repeatable, and
    //contract-identical to the xxh3 backend
    uint64_t h = stream->h;
    uint64_t k = stream->block;

    k *= c1;
    k = rotl64(k, 31);
    k *= c2;

    h ^= k;
    h ^= stream->totalLen;

    return fmix64(h);
#endif
}

//debug self-check: streaming must be bit-identical to the one-shot over the
//same bytes, incl. every two-segment split, double finalize, state reuse and
//unaligned data starts; no-op under NDEBUG
void hashStreamSelfTest() {
#ifndef NDEBUG
    uint8_t buffer[513 + 8];
    for (uint32_t i = 0; i < sizeof(buffer); ++i) {
        buffer[i] = (uint8_t)(i * 131u + 7u);
    }

    static const uint32_t sizes[] = { 0, 1, 2, 7, 8, 9, 15, 16, 63, 64, 65, 127, 128, 255, 256, 300, 511, 512, 513, (uint32_t)sizeof(buffer) };

    for (uint32_t s = 0; s < sizeof(sizes) / sizeof(sizes[0]); ++s) {
        const uint32_t length = sizes[s];
        const uint64_t oneShot = HASH_64bits(buffer, length);

        //single segment
        HashStream stream;
        hashStreamBegin(&stream, length);
        hashStreamUpdate(&stream, buffer, length);
        assert(hashStreamFinalize(&stream) == oneShot);

        //finalize is repeatable (xxh3 digest is non-destructive by contract)
        assert(hashStreamFinalize(&stream) == oneShot);

        //state reuse: a second begin/update/finalize cycle on the same stream
        hashStreamBegin(&stream, length);
        hashStreamUpdate(&stream, buffer, length);
        assert(hashStreamFinalize(&stream) == oneShot);

        //unaligned data starts
        static const uint32_t offsets[] = { 1, 3, 7 };
        for (uint32_t o = 0; o < sizeof(offsets) / sizeof(offsets[0]); ++o) {
            const uint64_t unaligned = HASH_64bits(buffer + offsets[o], length);
            hashStreamBegin(&stream, length);
            hashStreamUpdate(&stream, buffer + offsets[o], length);
            assert(hashStreamFinalize(&stream) == unaligned);
        }

        //two segments split at every position (covers block/tail boundaries)
        for (uint32_t split = 0; split <= length; ++split) {
            hashStreamBegin(&stream, length);
            hashStreamUpdate(&stream, buffer, split);
            hashStreamUpdate(&stream, buffer + split, length - split);
            assert(hashStreamFinalize(&stream) == oneShot);
        }
    }
#endif
}