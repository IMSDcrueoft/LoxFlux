#include "hash.h"
#include <stdint.h>
#include <stddef.h>
#include <string.h>

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