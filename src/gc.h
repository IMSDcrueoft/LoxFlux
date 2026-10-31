/*
 * MIT License
 * Copyright (c) 2025-2026 IMSDcrueoft (https://github.com/IMSDcrueoft)
 * See LICENSE file in the root directory for full license text.
*/
#pragma once
#include "common.h"
#include "value.h"

#define LOXFLUX_GC_HEAP_GROW_FACTOR 2
#define LOXFLUX_GC_HEAP_BEGIN ((size_t)1 << 20)

void markObject(LfxVM* vm, Obj* object);
void markValue(LfxVM* vm, LfxValue value);
void garbageCollect(LfxVM* vm);
//callers clamp the native fn argument to [16KiB, 1GiB], so size_t is safe
void changeNextGC(LfxVM* vm, size_t newSize);
void changeBeginGC(LfxVM* vm, size_t newSize);