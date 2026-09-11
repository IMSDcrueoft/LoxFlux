/*
* MIT License
* Copyright (c) 2025 IMSDcrueoft (https://github.com/IMSDcrueoft)
* See LICENSE file in the root directory for full license text.
*/
#pragma once

// mimalloc is used on Windows where a prebuilt static library is shipped
// (see third-party/mimalloc). On other platforms no sources are provided,
// so we fall back to the standard library allocator.
// The value can be overridden externally (e.g. via CMake define
// LOXFLUX_USE_MIMALLOC=0/1) to force enable/disable.
#ifndef LOXFLUX_USE_MIMALLOC
#if defined(_WIN32)
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