/*
 * MIT License
 * Copyright (c) 2025-2026 IMSDcrueoft (https://github.com/IMSDcrueoft)
 * See LICENSE file in the root directory for full license text.
*/
#pragma once
#include "common.h"
#include "value.h"

#define GROW_CAPACITY(capacity) \
	((capacity) < 8 ? 8 : (capacity << 1))

//safety cap for the few GROW_CAPACITY users without their own upper bound
//(chunk bytecode, opStack, lineArray): doubling a uint32_t capacity at or
//past this point would wrap to 0 and free the backing storage
#define GROW_CAPACITY_MAX 0x40000000u

//size_t (not uint64_t): all call sites pass sizeof products, keep the
//bookkeeping arithmetic native-width on 32-bit
void* reallocate(LfxVM* vm, void* pointer, size_t oldSize, size_t newSize);

#define GROW_ARRAY(vm, type, pointer, oldCount, newCount) \
	((type*)reallocate(vm, pointer, sizeof(type) * (oldCount), sizeof(type) * (newCount)))

#define FREE_ARRAY(vm, type, pointer, oldCount) \
	reallocate(vm, pointer, sizeof(type) * (oldCount), 0)

#define ALLOCATE(vm, type, count) \
	(type*)reallocate(vm, NULL, 0, sizeof(type) * (count))

//used to free single object
#define FREE(vm, type, pointer) reallocate(vm, pointer, sizeof(type), 0)
//used to free flex object
#define FREE_FLEX(vm,type,pointer,flexType,count) reallocate(vm, pointer, sizeof(type) + sizeof(flexType) * count, 0)

void* reallocate_no_gc(LfxVM* vm, void* pointer, size_t oldSize, size_t newSize);

#define GROW_ARRAY_NO_GC(vm, type, pointer, oldCount, newCount) \
	((type*)reallocate_no_gc(vm, pointer, sizeof(type) * (oldCount), sizeof(type) * (newCount)))

#define FREE_ARRAY_NO_GC(vm, type, pointer, oldCount) \
	reallocate_no_gc(vm, pointer, sizeof(type) * (oldCount), 0)

#define ALLOCATE_NO_GC(vm, type, count) \
	(type*)reallocate_no_gc(vm, NULL, 0, sizeof(type) * (count))

#define FREE_NO_GC(vm, type, pointer) reallocate_no_gc(vm, pointer, sizeof(type), 0)

#define FREE_FLEX_NO_GC(vm,type,pointer,flexType,count) reallocate_no_gc(vm, pointer, sizeof(type) + sizeof(flexType) * count, 0)

void freeObject(LfxVM* vm, Obj* object);
void freeObjects(LfxVM* vm);

struct ObjFiber;
struct ObjFiber* allocFiber(LfxVM* vm);

void freeFiberFreeList(LfxVM* vm);

void log_malloc_info(LfxVM* vm);