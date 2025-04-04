/*
 * MIT License
 * Copyright (c) 2025 IM&SD (https://github.com/IMSDcrueoft)
 * See LICENSE file in the root directory for full license text.
*/
#pragma once
#include "common.h"
#include "value.h"

#define GC_HEAP_GROW_FACTOR 2

void markObject(Obj* object);
void markValue(Value value);
void collectGarbage();