/*
 * MIT License
 * Copyright (c) 2026 IMSDcrueoft (https://github.com/IMSDcrueoft)
 * See LICENSE file in the root directory for full license text.
 * 
 * 
 * palloc_os — kernel VM syscall layer implementation
 */
#include "palloc_os.h"

#if defined(_WIN32)
#define OS_WINDOWS 1
#else
#define OS_POSIX 1
#endif

/* ------------------------------------------------------------------ */
/* Windows: VirtualAlloc / VirtualFree                                  */
/* ------------------------------------------------------------------ */
#if defined(OS_WINDOWS)

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

static size_t osPageSizeCached = 0;

uint32_t osPageSize(void) {
    if (osPageSizeCached == 0) {
        SYSTEM_INFO systemInfo;
        GetSystemInfo(&systemInfo);
        osPageSizeCached = (size_t)systemInfo.dwPageSize;
    }
    return (uint32_t)osPageSizeCached;
}

bool osPageAlloc(uint8_t **outPage, uint32_t *outSize) {
    uint32_t size = osPageSize();
    uint8_t *page = (uint8_t *)VirtualAlloc(NULL, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (page == NULL) return false;
    if (outPage != NULL) *outPage = page;
    if (outSize != NULL) *outSize = size;
    return true;
}

void osPageFree(uint8_t *page, uint32_t size) {
    (void)size;
    if (page != NULL) VirtualFree(page, 0, MEM_RELEASE);
}

/*
 * Windows VirtualAlloc has 64KB allocation granularity only, no alignment parameter, and MEM_RELEASE
 * releases a reservation only as a whole. So we over-reserve 8GB (4GB segment + up to 4GB
 * alignment slack) and use the aligned 4GB window inside; the edges stay RESERVED (pure VA
 * waste <=4GB/segment, negligible in the 128TB x64 user space; everything vanishes at process exit).
 */
bool osSegmentReserve(uint64_t pallocSegmentBytes, uint8_t **outBase, uint8_t **outReserveBase, size_t *outReserveSize) {
    const size_t overReserveSize = pallocSegmentBytes * 2;
    uint8_t *rawBase = (uint8_t *)VirtualAlloc(NULL, overReserveSize, MEM_RESERVE, PAGE_NOACCESS);
    if (rawBase == NULL) return false;

    uintptr_t rawAddress = (uintptr_t)rawBase;
    uintptr_t alignedAddress = (rawAddress + ((uintptr_t)pallocSegmentBytes - 1)) & ~((uintptr_t)pallocSegmentBytes - 1);

    *outBase = (uint8_t *)alignedAddress;
    *outReserveBase = rawBase;
    *outReserveSize = overReserveSize;
    return true;
}

bool osSegmentReserveEx(uint64_t pallocSegmentBytes, bool aligned, bool executable, uint8_t **outBase, uint8_t **outReserveBase, size_t *outReserveSize) {
    /* Windows grants EXEC later via VirtualProtect(PAGE_EXECUTE_*); nothing to do at reserve */
    (void)executable;
    if (!aligned) {
        uint8_t *base = (uint8_t *)VirtualAlloc(NULL, (size_t)pallocSegmentBytes, MEM_RESERVE, PAGE_NOACCESS);
        if (base == NULL) return false;
        *outBase = base;
        *outReserveBase = base;
        *outReserveSize = (size_t)pallocSegmentBytes;
        return true;
    }
    return osSegmentReserve(pallocSegmentBytes, outBase, outReserveBase, outReserveSize);
}

void osSegmentRelease(uint8_t *reserveBase, size_t reserveSize) {
    (void)reserveSize;
    if (reserveBase != NULL) VirtualFree(reserveBase, 0, MEM_RELEASE);
}

bool osPagesCommit(uint8_t *address, size_t length) {
    if (length == 0) return true;
    void *result = VirtualAlloc(address, length, MEM_COMMIT, PAGE_READWRITE);
    return result != NULL;
}

bool osPagesDrop(uint8_t *address, size_t length) {
    if (length == 0) return true;
    /* MEM_RESET: contents lazily dropped; pages stay committed and accessible; contents undefined on access */
    return VirtualAlloc(address, length, MEM_RESET, PAGE_READWRITE) != NULL;
}

bool osPagesReuse(uint8_t *address, size_t length) {
    (void)address;
    (void)length;
    return true; /* Windows MEM_RESET pages need no unmarking */
}

bool osPagesRecommit(uint8_t *address, size_t length) {
    if (length == 0) return true;
    if (address == NULL) return false;
    /* no reuse mark to clear on Windows: (re)committing is enough */
    return VirtualAlloc(address, length, MEM_COMMIT, PAGE_READWRITE) != NULL;
}

bool osPagesProtect(uint8_t *address, size_t length, OsPageProtection protection) {
    if (length == 0) return true;
    if (address == NULL) return false;

    DWORD newProtection;
    switch (protection) {
        case OS_PROTECT_NONE: newProtection = PAGE_NOACCESS;           break;
        case OS_PROTECT_RW:   newProtection = PAGE_READWRITE;          break;
        case OS_PROTECT_RX:   newProtection = PAGE_EXECUTE_READ;       break;
        case OS_PROTECT_RWX:  newProtection = PAGE_EXECUTE_READWRITE;  break;
        default: return false;
    }

    /* Windows grants EXEC to committed pages at any time (no MAP_JIT equivalent);
     * pages must be committed or VirtualProtect fails */
    DWORD oldProtection = 0;
    return VirtualProtect(address, length, newProtection, &oldProtection) != 0;
}

/* ------------------------------------------------------------------ */
/* POSIX: mmap / mprotect / madvise (macOS and Linux share one path)           */
/* ------------------------------------------------------------------ */
#else

#include <sys/mman.h>
#include <unistd.h>

static size_t osPageSizeCached = 0;

uint32_t osPageSize(void) {
    if (osPageSizeCached == 0) {
        long value = sysconf(_SC_PAGESIZE);
        osPageSizeCached = (value > 0) ? (size_t)value : 4096;
    }
    return (uint32_t)osPageSizeCached;
}

bool osPageAlloc(uint8_t **outPage, uint32_t *outSize) {
    uint32_t size = osPageSize();
    void *page = mmap(NULL, size, PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (page == MAP_FAILED) return false;
    if (outPage != NULL) *outPage = (uint8_t *)page;
    if (outSize != NULL) *outSize = size;
    return true;
}

void osPageFree(uint8_t *page, uint32_t size) {
    if (page != NULL && size > 0) munmap(page, size);
}

/*
 * POSIX mmap has no alignment parameter: over-reserve 8GB, then munmap the head and
 * tail outside the aligned 4GB window; the segment ends up exactly 4GB of VA, no waste.
 *
 * executable: Apple Silicon enforces userland W^X — EXEC can only be granted to
 * MAP_JIT mappings, and MAP_JIT must be passed at mmap time. Other platforms grant
 * EXEC later via mprotect, so the flag is a no-op there.
 */
static bool osSegmentReserveImpl(uint64_t pallocSegmentBytes, bool aligned, bool executable,
                                 uint8_t **outBase, uint8_t **outReserveBase, size_t *outReserveSize) {
    int mmapFlags = MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE;
#if defined(__APPLE__) && defined(__aarch64__)
    if (executable) mmapFlags |= MAP_JIT;
#else
    (void)executable;
#endif

    if (!aligned) {
        void *rawBase = mmap(NULL, (size_t)pallocSegmentBytes, PROT_NONE, mmapFlags, -1, 0);
        if (rawBase == MAP_FAILED) return false;
        *outBase = (uint8_t *)rawBase;
        *outReserveBase = (uint8_t *)rawBase;
        *outReserveSize = (size_t)pallocSegmentBytes;
        return true;
    }

    const size_t overReserveSize = (size_t)pallocSegmentBytes * 2;
    void *rawBase = mmap(NULL, overReserveSize, PROT_NONE, mmapFlags, -1, 0);
    if (rawBase == MAP_FAILED) return false;

    uintptr_t rawAddress = (uintptr_t)rawBase;
    uintptr_t alignedAddress = (rawAddress + ((uintptr_t)pallocSegmentBytes - 1)) & ~((uintptr_t)pallocSegmentBytes - 1);
    uintptr_t alignedEnd = alignedAddress + (uintptr_t)pallocSegmentBytes;
    uintptr_t rawEnd = rawAddress + overReserveSize;

    if (alignedAddress > rawAddress) {
        munmap(rawBase, (size_t)(alignedAddress - rawAddress));
    }
    if (rawEnd > alignedEnd) {
        munmap((void *)alignedEnd, (size_t)(rawEnd - alignedEnd));
    }

    *outBase = (uint8_t *)alignedAddress;
    *outReserveBase = (uint8_t *)alignedAddress;
    *outReserveSize = (size_t)pallocSegmentBytes;
    return true;
}

bool osSegmentReserve(uint64_t pallocSegmentBytes, uint8_t **outBase, uint8_t **outReserveBase, size_t *outReserveSize) {
    return osSegmentReserveImpl(pallocSegmentBytes, true, false, outBase, outReserveBase, outReserveSize);
}

bool osSegmentReserveEx(uint64_t pallocSegmentBytes, bool aligned, bool executable, uint8_t **outBase, uint8_t **outReserveBase, size_t *outReserveSize) {
    return osSegmentReserveImpl(pallocSegmentBytes, aligned, executable, outBase, outReserveBase, outReserveSize);
}

void osSegmentRelease(uint8_t *reserveBase, size_t reserveSize) {
    if (reserveBase != NULL && reserveSize > 0) {
        munmap(reserveBase, reserveSize);
    }
}

bool osPagesCommit(uint8_t *address, size_t length) {
    if (length == 0) return true;
    return mprotect(address, length, PROT_READ | PROT_WRITE) == 0;
}

bool osPagesDrop(uint8_t *address, size_t length) {
    if (length == 0) return true;
#if defined(__APPLE__)
    /* macOS MADV_DONTNEED only deactivates pages (resident_size does not drop, measured);
       MADV_FREE_REUSABLE marks them as instantly reclaimable (purgeable) */
    return madvise(address, length, MADV_FREE_REUSABLE) == 0;
#else
    /* Linux: drop contents and return the physical pages; later access yields zero pages */
    return madvise(address, length, MADV_DONTNEED) == 0;
#endif
}

bool osPagesReuse(uint8_t *address, size_t length) {
#if defined(__APPLE__)
    if (length == 0) return true;
    /* Clear the purgeable mark; if left set the kernel may reclaim pages after writes */
    return madvise(address, length, MADV_FREE_REUSE) == 0;
#else
    (void)address;
    (void)length;
    return true;
#endif
}

bool osPagesRecommit(uint8_t *address, size_t length) {
    if (length == 0) return true;
    if (address == NULL) return false;
    /* macOS: un-mark purgeable pages first (else the kernel may reclaim them after writes) */
    if (!osPagesReuse(address, length)) return false;
    return osPagesCommit(address, length);
}

bool osPagesProtect(uint8_t *address, size_t length, OsPageProtection protection) {
    if (length == 0) return true;
    if (address == NULL) return false;

    int prot;
    switch (protection) {
        case OS_PROTECT_NONE: prot = PROT_NONE;                          break;
        case OS_PROTECT_RW:   prot = PROT_READ | PROT_WRITE;             break;
        case OS_PROTECT_RX:   prot = PROT_READ | PROT_EXEC;              break;
        case OS_PROTECT_RWX:  prot = PROT_READ | PROT_WRITE | PROT_EXEC; break;
        default: return false;
    }
    if (mprotect(address, length, prot) != 0) return false;

#if defined(__APPLE__) && defined(__aarch64__)
    /*
     * Apple Silicon userland W^X: MAP_JIT pages are APRR-protected, pure mprotect is not
     * enough — writes need the per-thread toggle released, execution needs it re-armed.
     * Weak imports keep binaries linked against older deployment targets loadable; the
     * toggle is a no-op for non-JIT mappings and harmless on other threads' data pages.
     */
    extern void pthread_jit_write_protect_np(int enabled) __attribute__((weak_import));
    extern int  pthread_jit_write_protect_supported_np(void) __attribute__((weak_import));

    if (&pthread_jit_write_protect_supported_np != NULL && pthread_jit_write_protect_supported_np()) {
        switch (protection) {
            case OS_PROTECT_RW:
            case OS_PROTECT_RWX:
                pthread_jit_write_protect_np(0); /* writes allowed (code-writing phase) */
                break;
            case OS_PROTECT_RX:
            case OS_PROTECT_NONE:
                pthread_jit_write_protect_np(1); /* execution only (finalized code) */
                break;
            default:
                break;
        }
    }
#endif
    return true;
}

#endif
