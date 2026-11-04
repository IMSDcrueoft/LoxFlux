/*
 * MIT License
 * Copyright (c) 2025-2026 IMSDcrueoft (https://github.com/IMSDcrueoft)
 * See LICENSE file in the root directory for full license text.
*/
#pragma once
#include "common.h"
#include "value.h"

#define LOXFLUX_GC_HEAP_BEGIN ((size_t)1 << 20) //also the policy's minBytes floor

//headroom policy tuning: nextGC = live + clamp(target, floor, ceiling)
#define LOXFLUX_GC_HEADROOM_RATIO      1.0                //target = ratio * live (matches the old GROW_FACTOR 2)
#define LOXFLUX_GC_MIN_HEADROOM_RATIO  0.5                //floor ratio at survival = 0
#define LOXFLUX_GC_MAX_HEADROOM_RATIO  2.0                //floor ratio clamp for high survival
#define LOXFLUX_GC_SLACK_BUDGET        ((size_t)64 << 20) //caps the K*live target: bounds the garbage accumulated per cycle

/*
 * headroom = clamp(target, floor, ceiling)
 *   target  = ratio * live
 *   floor   = max(minBytes,
 *                 clamp(minRatio / (1 - survival),
 *                       ..maxRatio) * live)
 *   ceiling = max(slackBudgetBytes, floor)   //only caps the ratio target: bounds per-cycle garbage
 *
 * survival = live growth / bytes allocated during the cycle, smoothed slowly.
 * A high survival rate raises the floor so that every collection still makes
 * progress instead of thrashing.
 */
typedef struct {
	//config, read-only after initialize
	double ratio;			//nominal: headroom = live * ratio
	double minRatio;		//lower bound on the headroom ratio, high survival
	double maxRatio;		//upper bound on the headroom ratio, high survival
	size_t minBytes;		//absolute lower bound on headroom, small heaps
	size_t slackBudgetBytes;//caps the K*live target on large heaps

	//algorithm state, refreshed once per update
	double survivalRatio;
	size_t previousLiveBytes;
} LfxHeadroomPolicy;

size_t headroomPolicy_update(LfxHeadroomPolicy* policy, size_t liveBytes, size_t allocatedBytes);
//survival sampling from two heap snapshots: the bytes allocated during the
//cycle are approximated by the growth since the previous update's live mark
size_t headroomPolicy_updateFromHeap(LfxHeadroomPolicy* policy, size_t heapBefore, size_t heapAfter);

void markObject(LfxVM* vm, Obj* object);
void markValue(LfxVM* vm, LfxValue value);
void garbageCollect(LfxVM* vm);