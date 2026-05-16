/*
* MIT License
* Copyright (c) 2025 IMSDcrueoft (https://github.com/IMSDcrueoft)
* See LICENSE file in the root directory for full license text.
*/
#pragma once

#define USE_MIMALLOC 1

#if USE_MIMALLOC
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

#undef USE_MIMALLOC