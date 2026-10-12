/*
 * MIT License
 * Copyright (c) 2026 IMSDcrueoft (https://github.com/IMSDcrueoft)
 * See LICENSE file in the root directory for full license text.
 * 
 * 
 * palloc_os — kernel VM syscall primitives (mmap/mprotect/madvise, VirtualAlloc/VirtualFree)
 *
 * This layer knows nothing about any allocator: it exposes raw reservation / commit / drop
 * primitives and nothing else.
 *
 * macOS / Linux: mmap + mprotect + madvise
 * Windows:       VirtualAlloc(MEM_RESERVE/MEM_COMMIT/MEM_RESET) + VirtualFree
 *
 */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Reserve a aligned,alingn-sized, inaccessible (PROT_NONE / PAGE_NOACCESS) address range.
 * On success *outBase is the aligned base; on the Windows over-reservation path
 * *outReserveBase / *outReserveSize return the real base and size (needed for release).
 */
bool osSegmentReserve(uint64_t pallocSegmentBytes, uint8_t **outBase, uint8_t **outReserveBase, size_t *outReserveSize);

/*
 * Like osSegmentReserve, but with an optional power-of-two alignment toggle.
 *   aligned == true : *outBase is aligned to pallocSegmentBytes (pallocSegmentBytes must be a
 *                     power of two); on the Windows over-reservation path *outReserveBase /
 *                     *outReserveSize describe the real base and size needed for release.
 *   aligned == false: plain page-aligned reservation; *outReserveBase == *outBase and
 *                     *outReserveSize == pallocSegmentBytes.
 * The original osSegmentReserve is kept unchanged for existing callers.
 *
 * executable: the pages may later be granted EXEC permission via osPagesProtect
 * (machine code). DANGEROUS — handle only trusted, self-generated code. Must be decided
 * at reserve time: Apple Silicon requires MAP_JIT at mmap and it cannot be added
 * afterwards.
 */
bool osSegmentReserveEx(uint64_t pallocSegmentBytes, bool aligned, bool executable,
                        uint8_t **outBase, uint8_t **outReserveBase, size_t *outReserveSize);

/* Release the whole reservation (must pass the reserveBase/reserveSize from osSegmentReserve) */
void osSegmentRelease(uint8_t *reserveBase, size_t reserveSize);

/* Commit [address, address+length) as read-write; repeatable (idempotent) */
bool osPagesCommit(uint8_t *address, size_t length);

/*
 * Lazily drop the contents of [address, address+length):
 *   macOS:   madvise(MADV_FREE_REUSABLE) — pages become purgeable (instantly reclaimable)
 *   Linux:   madvise(MADV_DONTNEED) — pages returned to the kernel immediately
 *   Windows: VirtualAlloc(MEM_RESET) — pages stay accessible, contents become undefined
 * Returns whether the syscall succeeded; contents are treated as garbage.
 * address and length must be aligned to osPageSize().
 */
bool osPagesDrop(uint8_t *address, size_t length);

/*
 * Clears the drop mark set by osPagesDrop (pages become normally resident again).
 * Must be called before handing previously dropped memory to the user; otherwise macOS purgeable
 * pages can still be reclaimed by the kernel after writes, corrupting live data.
 *   macOS:   madvise(MADV_FREE_REUSE)
 *   Linux/Windows: not needed (no-op)
 */
bool osPagesReuse(uint8_t *address, size_t length);

/*
 * Make [address, address+length) usable again before writing to it. Encapsulates the
 * macOS requirement that MADV_FREE_REUSABLE pages must be un-marked (MADV_FREE_REUSE)
 * before reuse; on Linux/Windows this is just a (re)commit. Idempotent.
 * address and length must be aligned to osPageSize().
 */
bool osPagesRecommit(uint8_t *address, size_t length);

typedef enum OsPageProtection {
    OS_PROTECT_NONE = 0, /* no access (guard pages)            */
    OS_PROTECT_RW   = 1, /* read-write                         */
    OS_PROTECT_RX   = 2, /* read + execute (finalized code)    */
    OS_PROTECT_RWX  = 3  /* read + write + execute (writing code) */
} OsPageProtection;

/*
 * Change the protection of already-committed pages [address, address+length). Pure
 * permission change: pages must have been committed (osPagesCommit / osPagesRecommit first).
 * Permissions are NOT sticky: any commit-family call resets pages back to RW, so re-apply
 * EXEC after every drop/recommit cycle.
 * Only meaningful for segments reserved with osSegmentReserveEx(executable=true): plain
 * mappings reject EXEC on Apple Silicon (userland W^X) and are meaningless to execute
 * elsewhere.
 * Apple Silicon (macOS arm64): the W^X write toggle (pthread_jit_write_protect_np) is
 * handled inside — write-capable protections release the toggle, read-only protections
 * re-arm it. The toggle is per-thread: every thread that writes or runs JIT code must go
 * through this API on its own.
 * address and length must be aligned to osPageSize(). Idempotent, repeatable.
 */
bool osPagesProtect(uint8_t *address, size_t length, OsPageProtection protection);

/* Runtime page size (macOS arm64 = 16KB, x86-64 = 4KB, Windows follows the system) */
uint32_t osPageSize(void);

/*
 * Allocate exactly one OS page as read-write, committed memory, independent of any
 * segment. On success *outPage receives the page base and *outSize receives osPageSize().
 * Returns false on failure.
 */
bool osPageAlloc(uint8_t **outPage, uint32_t *outSize);

/* Release a page previously returned by osPageAlloc (pass back the same size). */
void osPageFree(uint8_t *page, uint32_t size);

#ifdef __cplusplus
}
#endif

