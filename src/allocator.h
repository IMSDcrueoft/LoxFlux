/*
* MIT License
* Copyright (c) 2025 IM&SD (https://github.com/IMSDcrueoft)
* See LICENSE file in the root directory for full license text.
*/
#pragma once
#include <mimalloc.h>

#define mem_alloc mi_malloc
#define mem_realloc mi_realloc
#define mem_free mi_free
#define mem_print_stats mi_stats_print