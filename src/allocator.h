/*
* MIT License
* Copyright (c) 2025 IMSDcrueoft (https://github.com/IMSDcrueoft)
* See LICENSE file in the root directory for full license text.
*/
#pragma once

// mimalloc is cross-platform, but this project only ships a prebuilt MSVC
// static library (third-party/mimalloc) to avoid cross-compiling one per
// platform; no library files are provided for other toolchains/platforms,
// so those fall back to the standard library allocator (install a system
// mimalloc and build through CMake with LOXFLUX_USE_MIMALLOC=ON to opt in).
// CMake always defines LOXFLUX_USE_MIMALLOC=0/1 explicitly, so the platform
// check below only serves non-CMake builds (MSVC projects / raw command
// lines): gate on _MSC_VER, not _WIN32 — MinGW defines _WIN32 too but cannot
// link the MSVC-ABI .lib.
#ifndef LOXFLUX_USE_MIMALLOC
#if defined(_MSC_VER)
#define LOXFLUX_USE_MIMALLOC 1
#else
#define LOXFLUX_USE_MIMALLOC 0
#endif
#endif

#if LOXFLUX_USE_MIMALLOC
#include <mimalloc.h>

#define mem_alloc mi_malloc
#define mem_realloc mi_realloc
#define mem_free mi_free
#define mem_print_stats mi_stats_print

#else
#include <stdlib.h>

#define mem_alloc malloc
#define mem_realloc realloc
#define mem_free free
#define mem_print_stats
#endif

#undef LOXFLUX_USE_MIMALLOC