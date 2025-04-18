/*
 * MIT License
 * Copyright (c) 2025 IMSDcrueoft (https://github.com/IMSDcrueoft)
 * See LICENSE file in the root directory for full license text.
*/
#pragma once
#include "common.h"
#include "value.h"

#define GC_HEAP_GROW_FACTOR 2
#define GC_HEAP_BEGIN 1024 * 1024

//please don't modify them from outside
extern uint8_t usingMark;

void markObject(Obj* object);
void markValue(Value value);
void garbageCollect();
void changeNextGC(uint64_t newSize);
void changeBeginGC(uint64_t newSize);