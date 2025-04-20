/*
 * MIT License
 * Copyright (c) 2025 IMSDcrueoft (https://github.com/IMSDcrueoft)
 * See LICENSE file in the root directory for full license text.
*/
#pragma once
#include "common.h"
#include "value.h"

#define GROW_CAPACITY(capacity) \
	((capacity) < 16 ? 16 : (capacity << 1))

void* reallocate(void* pointer, uint64_t oldSize, uint64_t newSize);

#define GROW_ARRAY(type, pointer, oldCount, newCount) \
	((type*)reallocate(pointer, sizeof(type) * (oldCount), sizeof(type) * (newCount)))

#define FREE_ARRAY(type, pointer, oldCount) \
	reallocate(pointer, sizeof(type) * (oldCount), 0)

#define ALLOCATE(type, count) \
	(type*)reallocate(NULL, 0, sizeof(type) * (count))

//used to free single object
#define FREE(type, pointer) reallocate(pointer, sizeof(type), 0)
//used to free flex object
#define FREE_FLEX(type,pointer,flexType,count) reallocate(pointer, sizeof(type) + sizeof(flexType) * count, 0)

void* reallocate_no_gc(void* pointer, uint64_t oldSize, uint64_t newSize);

#define GROW_ARRAY_NO_GC(type, pointer, oldCount, newCount) \
	((type*)reallocate_no_gc(pointer, sizeof(type) * (oldCount), sizeof(type) * (newCount)))

#define FREE_ARRAY_NO_GC(type, pointer, oldCount) \
	reallocate_no_gc(pointer, sizeof(type) * (oldCount), 0)

#define ALLOCATE_NO_GC(type, count) \
	(type*)reallocate_no_gc(NULL, 0, sizeof(type) * (count))

#define FREE_NO_GC(type, pointer) reallocate_no_gc(pointer, sizeof(type), 0)

#define FREE_FLEX_NO_GC(type,pointer,flexType,count) reallocate_no_gc(pointer, sizeof(type) + sizeof(flexType) * count, 0)

void freeObject(Obj* object);
void freeObjects();

void log_malloc_info();