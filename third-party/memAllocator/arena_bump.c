/*
 * MIT License
 * Copyright (c) 2026 IMSDcrueoft (https://github.com/IMSDcrueoft)
 * See LICENSE file in the root directory for full license text.
 *
 *
 * arenaBump — LIFO bump allocator implementation (palloc_os backing)
 */
#include "arena_bump.h"
#include "palloc_os.h"

/* live reservation count: init increments on success, destroy decrements when
 * it actually releases; consumed by leak asserts in owners (vm_free) */
static uint32_t arenaBumpLiveSegments = 0;

uint32_t arenaBump_liveSegments(void) {
    return arenaBumpLiveSegments;
}

static size_t arenaBumpAlignUp(size_t x, size_t a) {
    return (x + (a - 1)) & ~(a - 1);
}

static bool arenaBumpIsPow2(size_t x) {
    return x != 0 && (x & (x - 1)) == 0;
}

bool arenaBump_init(ArenaBumpAllocator *arena, size_t segmentBytes, bool aligned, size_t alignBytes, size_t commitChunkSize) {
    if (arena == NULL || segmentBytes == 0) return false;
    if (aligned && !arenaBumpIsPow2(segmentBytes)) return false;
    if (alignBytes < ARENA_BUMP_MIN_ALIGN) alignBytes = ARENA_BUMP_MIN_ALIGN;
    if (!arenaBumpIsPow2(alignBytes)) return false;
    if (alignBytes > UINT32_MAX) return false;   /* must fit the stored field */

    uint8_t *base = NULL, *reserveBase = NULL;
    size_t reserveSize = 0;
    if (!osSegmentReserveEx((uint64_t)segmentBytes, aligned, false, &base, &reserveBase, &reserveSize)) {
        return false;
    }

    arena->base = base;
    arena->reserveBase = reserveBase;
    arena->reserveSize = reserveSize;
    arena->capacity = segmentBytes;
    arena->commitTop = 0;
    arena->dropFrom = 0;   /* dropFrom >= dropTop => no dropped tail */
    arena->dropTop = 0;
    arena->liveTop = 0;
    arena->peak = 0;
    arena->page = osPageSize();
    if (arena->page == 0) arena->page = 4096;
    arena->alignBytes = (uint32_t)alignBytes;

    if (commitChunkSize == 0) commitChunkSize = ARENA_BUMP_COMMIT_CHUNK;
    if (commitChunkSize > segmentBytes) commitChunkSize = segmentBytes;
    if (commitChunkSize < (size_t)arena->page) commitChunkSize = (size_t)arena->page;
    commitChunkSize = arenaBumpAlignUp(commitChunkSize, (size_t)arena->page);  /* keep commitTop page aligned */
    if (commitChunkSize > segmentBytes) commitChunkSize = segmentBytes;
    arena->commitChunkSize = commitChunkSize;
    ++arenaBumpLiveSegments;
    return true;
}

void arenaBump_destroy(ArenaBumpAllocator *arena) {
    if (arena == NULL || arena->base == NULL) return;
    osSegmentRelease(arena->reserveBase, arena->reserveSize);
    arena->base = NULL;
    arena->capacity = arena->commitTop = arena->liveTop = arena->peak = 0;
    arena->dropFrom = arena->dropTop = 0;
    --arenaBumpLiveSegments;
}

/* Commit up to `end`, reactivating externally dropped pages along the way. */
static bool arenaBumpEnsure(ArenaBumpAllocator *arena, size_t end) {
    const size_t page = arena->page;
    const size_t want = arenaBumpAlignUp(end, page);

    if (want > arena->commitTop) {
        size_t target = arena->commitTop + arena->commitChunkSize;
        if (target < want) target = want;
        if (target > arena->capacity) target = arena->capacity;
        if (!osPagesRecommit(arena->base + arena->commitTop, target - arena->commitTop)) return false;
        arena->commitTop = target;
    }

    if (arena->dropFrom < arena->dropTop) {
        size_t b = arenaBumpAlignUp(end, page);
        if (b > arena->dropTop) b = arena->dropTop;
        if (b > arena->dropFrom) {
            if (!osPagesRecommit(arena->base + arena->dropFrom, b - arena->dropFrom)) return false;
            arena->dropFrom = b;
            if (arena->dropFrom >= arena->dropTop) arena->dropFrom = arena->dropTop = 0;
        }
    }
    return true;
}

void *arenaBump_alloc(ArenaBumpAllocator *arena, size_t bytes) {
    if (arena == NULL || bytes == 0) return NULL;

    size_t off = arenaBumpAlignUp(arena->liveTop, arena->alignBytes);
    if (off < arena->liveTop) return NULL;    /* overflow */
    size_t end = off + bytes;
    if (end < off) return NULL;               /* overflow */
    if (end > arena->capacity) return NULL;   /* capacity exceeded */

    if (!arenaBumpEnsure(arena, end)) return NULL;

    arena->liveTop = end;
    if (end > arena->peak) arena->peak = end;
    return arena->base + off;
}

size_t arenaBump_mark(const ArenaBumpAllocator *arena) {
    return arena ? arena->liveTop : 0;
}

void arenaBump_rewind(ArenaBumpAllocator *arena, size_t mark) {
    if (arena == NULL) return;
    if (mark <= arena->liveTop) arena->liveTop = mark;
}

bool arenaBump_shrink(ArenaBumpAllocator *arena, const void *ptr) {
    if (arena == NULL || arena->base == NULL || ptr == NULL) return false;
    uintptr_t b = (uintptr_t)arena->base;
    uintptr_t x = (uintptr_t)ptr;

    if (x < b || x > b + arena->liveTop) return false;   /* outside the live range */

    size_t off = (size_t)(x - b);
    if (arena->alignBytes == 0 || (off & (arena->alignBytes - 1)) != 0) return false;  /* not an allocation boundary */

    arena->liveTop = off;
    return true;
}

bool arenaBump_ensureCommitted(ArenaBumpAllocator *arena, size_t byteEnd) {
    if (arena == NULL || arena->base == NULL) return false;
    if (byteEnd > arena->capacity) byteEnd = arena->capacity;
    return arenaBumpEnsure(arena, byteEnd);
}

size_t arenaBump_used(const ArenaBumpAllocator *arena) {
    return arena ? arena->liveTop : 0;
}

size_t arenaBump_capacity(const ArenaBumpAllocator *arena) {
    return arena ? arena->capacity : 0;
}

size_t arenaBump_committed(const ArenaBumpAllocator *arena) {
    return arena ? arena->commitTop : 0;
}

bool arenaBump_owns(const ArenaBumpAllocator *arena, const void *ptr) {
    if (arena == NULL || arena->base == NULL || ptr == NULL) return false;
    uintptr_t x = (uintptr_t)ptr;
    uintptr_t b = (uintptr_t)arena->base;
    return x >= b && x < b + arena->capacity;
}
