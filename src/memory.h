/*
 * MIT License
 * Copyright (c) 2025 IM&SD (https://github.com/IMSDcrueoft)
 * See LICENSE file in the root directory for full license text.
*/
#pragma once
#include "common.h"

Unknown_ptr reallocate(Unknown_ptr pointer, size_t oldSize, size_t newSize);

#define GROW_CAPACITY(capacity) \
	((capacity) < 16 ? 16 : (capacity << 1))

#define GROW_ARRAY(type, pointer, oldCount, newCount) \
	((type*)reallocate(pointer, sizeof(type) * (oldCount), sizeof(type) * (newCount)))

#define FREE_ARRAY(type, pointer, oldCount) \
	reallocate(pointer, sizeof(type) * (oldCount), 0)

#define ALLOCATE(type, count) \
	(type*)reallocate(NULL, 0, sizeof(type) * (count))

//used to free single object
#define FREE(type, pointer) reallocate(pointer, sizeof(type), 0)

void freeObjects();

void log_malloc_info();