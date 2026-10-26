/*
* MIT License
* Copyright (c) 2025-2026 IMSDcrueoft (https://github.com/IMSDcrueoft)
* See LICENSE file in the root directory for full license text.
*/
#pragma once

// mimalloc is vendored from source under third-party/mimalloc (include/ + src/
// + upstream CMakeLists.txt). The VS solution builds it as a static library
// (third-party/mimalloc/mimalloc-lib.vcxproj, one output per configuration:
// x86/x64 x Debug/Release under x64\mimalloc\), and CMake builds it through
// add_subdirectory; no prebuilt binaries are shipped. The MSVC gate below
// only serves non-CMake builds (MSVC projects / raw command lines): gate on
// _MSC_VER, not _WIN32 — MinGW defines _WIN32 too but cannot link the
// MSVC-ABI static lib.
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
#define mem_collect(force) mi_collect(force)

#else
#include <stdlib.h>

#define mem_alloc malloc
#define mem_realloc realloc
#define mem_free free
#define mem_print_stats
#define mem_collect(force) ((void)(force))
#endif

#undef LOXFLUX_USE_MIMALLOC