/*
* MIT License
* Copyright (c) 2025-2026 IMSDcrueoft (https://github.com/IMSDcrueoft)
* See LICENSE file in the root directory for full license text.
*/
#include "nativeBuiltin.h"
#include "vm.h"
//fiber

//language-facing status numbers (@fiber.* constants), a stable script contract
#define FIBER_STATUS_PENDING    0.0
#define FIBER_STATUS_SETTLED    1.0
#define FIBER_STATUS_RUNNING    2.0
#define FIBER_STATUS_FAILED     3.0
#define FIBER_STATUS_CANCELLED  4.0

static LfxValue fiberStatusNative(LfxVM* vm, int argCount, LfxValue* args) {
	if (argCount >= 1 && IS_FIBER(args[0])) {
		ObjFiber* fiber = AS_FIBER(args[0]);

		//isRunning marks "executing on the active resumer chain"; a sealed
		//fiber can never run again, so the flag alone decides RUNNING
		if (fiber->isRunning) {
			return NUMBER_VAL(FIBER_STATUS_RUNNING);
		}

		switch (fiber->status) {
		case FIBER_PENDING:		return NUMBER_VAL(FIBER_STATUS_PENDING);
		case FIBER_SETTLED:		return NUMBER_VAL(FIBER_STATUS_SETTLED);
		case FIBER_FAILED:		return NUMBER_VAL(FIBER_STATUS_FAILED);
		case FIBER_CANCELLED:	return NUMBER_VAL(FIBER_STATUS_CANCELLED);
		}
	}

	fprintf(stderr, "Can't take the status of a non-coroutine value.\n");
	return NIL_VAL;
}

//pure read: unlike resume, this never executes the fiber. a suspended fiber
//reports its last yielded value, a settled one its return value, a failed one
//nil (until error propagation stores real error values)
static LfxValue fiberResultNative(LfxVM* vm, int argCount, LfxValue* args) {
	if (argCount >= 1 && IS_FIBER(args[0])) {
		return AS_FIBER(args[0])->result;
	}

	fprintf(stderr, "Can't take the result of a non-coroutine value.\n");
	return NIL_VAL;
}

//cancel a pending fiber: seals it immediately (frames freed, arena recycled,
//open upvalues closed so escaped closures keep reading valid values) and
//marks it CANCELLED. the optional second argument becomes the fiber's result,
//so a later resume reads it via the settled-result path. returns whether the
//cancel took: fibers already finalized (settled/failed/cancelled) or running
//report false instead
static LfxValue fiberCancelNative(LfxVM* vm, int argCount, LfxValue* args) {
	if (argCount < 1 || !IS_FIBER(args[0])) {
		fprintf(stderr, "Can't cancel a non-coroutine value.\n");
		return BOOL_VAL(false);
	}

	ObjFiber* fiber = AS_FIBER(args[0]);

	//a running fiber sits on the active resumer chain (possibly the caller
	//itself): cancelling it would yank the stack out from under execution
	if (fiber->isRunning || fiber->status != FIBER_PENDING) {
		return BOOL_VAL(false);
	}

	fiber->result = (argCount >= 2) ? args[1] : NIL_VAL;
	fiber->status = FIBER_CANCELLED;
	sealFiber(vm, fiber);

	return BOOL_VAL(true);
}

//the fiber executing right now (the root when no coroutine is on the chain).
//safe to expose: the resume cycle guard rejects it and result/status are pure
static LfxValue fiberCurrentNative(LfxVM* vm, int argCount, LfxValue* args) {
	return OBJ_VAL(vm->currentFiber);
}

COLD_FUNCTION
void importNative_fiber(LfxVM* vm) {
	defineNative_fiber_number(vm, "PENDING", FIBER_STATUS_PENDING);
	defineNative_fiber_number(vm, "SETTLED", FIBER_STATUS_SETTLED);
	defineNative_fiber_number(vm, "RUNNING", FIBER_STATUS_RUNNING);
	defineNative_fiber_number(vm, "FAILED", FIBER_STATUS_FAILED);
	defineNative_fiber_number(vm, "CANCELLED", FIBER_STATUS_CANCELLED);

	defineNative_fiber(vm, "status", fiberStatusNative);
	defineNative_fiber(vm, "result", fiberResultNative);
	defineNative_fiber(vm, "cancel", fiberCancelNative);
	defineNative_fiber(vm, "current", fiberCurrentNative);
}
