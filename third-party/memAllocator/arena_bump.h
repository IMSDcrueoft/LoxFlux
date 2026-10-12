/*
 * MIT License
 * Copyright (c) 2026 IMSDcrueoft (https://github.com/IMSDcrueoft)
 * See LICENSE file in the root directory for full license text.
 *
 *
 * arenaBump — LIFO bump allocator over palloc_os segments.
 *
 * VA is reserved once and never moves; memory is committed lazily in
 * commitChunkSize steps. Release is tail-only (mark/rewind/shrink). Page trimming
 * is the caller's job: set dropFrom/dropTop + osPagesDrop, the arena re-commits
 * those pages lazily on the next touching allocation.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    ARENA_BUMP_COMMIT_CHUNK = 64 * 1024,  /* default commitChunkSize   */
    ARENA_BUMP_MIN_ALIGN    = 8           /* floor for alignBytes      */
};

typedef struct ArenaBumpAllocator {
    uint8_t *base;            /* allocation base                          */
    uint8_t *reserveBase;     /* pass to osSegmentRelease                 */
    size_t   reserveSize;     /* pass to osSegmentRelease                 */
    size_t   capacity;        /* usable bytes                             */
    size_t   commitTop;       /* committed watermark, page aligned        */
    size_t   dropFrom;        /* externally dropped tail; empty if >= dropTop */
    size_t   dropTop;         /*                                          */
    size_t   liveTop;         /* bump cursor                              */
    size_t   peak;            /* liveTop high-water mark                  */
    uint32_t page;            /* OS page size                             */
    uint32_t alignBytes;      /* allocation alignment, 2^n, >= ARENA_BUMP_MIN_ALIGN */
    size_t   commitChunkSize; /* commit step, page multiple               */
} ArenaBumpAllocator;

/*
 * segmentBytes    : reservation size; must be 2^n when aligned
 * aligned         : base aligned to segmentBytes, else page aligned
 * alignBytes      : alignment of every allocation; 2^n, raised to ARENA_BUMP_MIN_ALIGN
 * commitChunkSize : commit step; 0 => ARENA_BUMP_COMMIT_CHUNK, rounded up to a page
 *                   multiple, clamped to capacity
 */
bool arenaBump_init(ArenaBumpAllocator *arena, size_t segmentBytes, bool aligned, size_t alignBytes, size_t commitChunkSize);

/* Releases the reservation. */
void arenaBump_destroy(ArenaBumpAllocator *arena);

/* Reservations currently alive (init without destroy); leak diagnostics. */
uint32_t arenaBump_liveSegments(void);

/* Returns NULL on overflow, capacity exceeded, or commit failure. */
void *arenaBump_alloc(ArenaBumpAllocator *arena, size_t bytes);

/* Save/restore the cursor; rewind is a no-op if mark > liveTop. */
size_t arenaBump_mark(const ArenaBumpAllocator *arena);
void   arenaBump_rewind(ArenaBumpAllocator *arena, size_t mark);

/* Makes ptr the new live top. False (arena unchanged) if ptr is outside
 * [base, base + used] or not alignBytes-aligned. */
bool arenaBump_shrink(ArenaBumpAllocator *arena, const void *ptr);

/* Commits pages until the byte range [0, byteEnd) is backed; reactivates
 * externally dropped pages in the way. Does NOT move liveTop. byteEnd is
 * clamped to capacity. Returns false on commit failure. */
bool arenaBump_ensureCommitted(ArenaBumpAllocator *arena, size_t byteEnd);

size_t arenaBump_used(const ArenaBumpAllocator *arena);       /* liveTop   */
size_t arenaBump_capacity(const ArenaBumpAllocator *arena);   /* capacity  */
size_t arenaBump_committed(const ArenaBumpAllocator *arena);  /* commitTop */
bool   arenaBump_owns(const ArenaBumpAllocator *arena, const void *ptr);

#ifdef __cplusplus
}
#endif
