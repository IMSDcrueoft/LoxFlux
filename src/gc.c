/*
 * MIT License
 * Copyright (c) 2025-2026 IMSDcrueoft (https://github.com/IMSDcrueoft)
 * See LICENSE file in the root directory for full license text.
*/
#include "gc.h"
#include "vm.h"
#include "compiler.h"
#include "allocator.h"
#include "memory.h"
#include "timer.h"

void markValue(Value value)
{
	if (IS_OBJ(value)) markObject(AS_OBJ(value));
}

//static void markArray(ValueArray* array) {
//	for (uint32_t i = 0; i < array->count; i++) {
//		markValue(array->values[i]);
//	}
//}

// static void blackenObject(Obj* object);
//static void markConstants(ValueArray* array) {
//	for (uint32_t i = 0; i < array->count; i++) {
//		Value value = array->values[i];
//		if (IS_OBJ(value)) {
//			AS_OBJ(value)->isMarked = vm.gcMark;
//			blackenObject(AS_OBJ(value));
//		}
//	}
//}

static void markArrayAny(ObjArray* array) {
	Value* arrPtr = (Value*)array->payload;

	for (uint32_t i = 0; i < array->length; ++i) {
		Value value = arrPtr[i];

		if (IS_OBJ(value)) {
			markObject(AS_OBJ(value));
		}
	}
}

static void markRoots() {
	//do not scan the VM execution cache: the current fiber owns the live
	//stack/frames/open-upvalues, and resumer chains cover suspended callers
	if (vm.currentFiber != NULL) {
		markObject((Obj*)vm.currentFiber);
	}

	markTable(&vm.globals.fields);
	//the shared constants don't gc
	//markConstants(&vm.constants);

	//inline caches are WEAK references: invoke slots holding dead ObjClass*/ObjClosure*
	//are cleared in gc_clear_dead_ic_slots() after tracing, before sweep frees them

	markCompilerRoots();

	//markObject((Obj*)vm.initString);
}

void markObject(Obj* object)
{
	//skip the null and things that don't need mark
	if (object == NULL) return;
	//skip marked one
	if (object->isMarked == vm.gcMark) return;

	switch (object->type) {
	case OBJ_FUNCTION:
	case OBJ_NATIVE:
	case OBJ_STRING:
		//don't join gc
		//object->isMarked = true;
		return;
	}

#if DEBUG_LOG_GC
	fprintf(stderr, "[gc] %p mark ", (void*)object);
	printValue(stderr, OBJ_VAL(object), false);
	fprintf(stderr, "\n");
#endif
	object->isMarked = vm.gcMark;

	//fixed-VA gray stack, plain count logic (same model as the vm stack): the
	//hot path is one compare against the committed edge; only crossing it does
	//a lazy 64KB page commit. No realloc, no heap allocation -> cannot trigger
	//or nest a GC. Capacity exhausted or commit failure is fatal inside a GC
	//cycle (need is in bytes: one more pointer slot beyond grayCount)
	size_t need = (size_t)(vm.grayCount + 1) * sizeof(Obj*);
	if (need > vm.grayArena.commitTop) {
		if (need > vm.grayArena.capacity ||
			!arenaBump_ensureCommitted(&vm.grayArena, need)) {
			fprintf(stderr, "[gc] gray stack overflow.\n");
			exit(1);
		}
	}

	vm.grayStack[vm.grayCount++] = object;
}

static void blackenObject(Obj* object) {
#if DEBUG_LOG_GC
	fprintf(stderr, "[gc] %p blacken ", (void*)object);
	printValue(stderr, OBJ_VAL(object), false);
	fprintf(stderr, "\n");
#endif

	switch (object->type) {
	case OBJ_UPVALUE: {
		//When an upvalue is closed, it contains a reference to the closed-over value
		markValue(((ObjUpvalue*)object)->closed);
		break;
	}
	case OBJ_CLOSURE: {
		ObjClosure* closure = (ObjClosure*)object;
		//no need
		//markObject((Obj*)closure->function);

		for (uint32_t i = 0; i < closure->upvalueCount; i++) {
			markObject((Obj*)closure->upvalues[i]);
		}
		break;
	}
	case OBJ_BOUND_METHOD: {
		ObjBoundMethod* bound = (ObjBoundMethod*)object;
		markValue(bound->receiver);
		markObject((Obj*)bound->method);
		break;
	}
						 // //won't be here
						 //case OBJ_FUNCTION: {
						 //	ObjFunction* function = (ObjFunction*)object;
						 //	markObject((Obj*)function->name);

						 //	//I don't have this field in design
						 //	//markArray(&function->chunk.constants);
						 //	break;
						 //}
	case OBJ_CLASS: {
		ObjClass* klass = (ObjClass*)object;
		//markObject((Obj*)klass->name);
		markValue(klass->initializer);
		markTable(&klass->methods);
		//the superclass chain must survive as long as the subclass
		if (klass->superclass != NULL) {
			markObject((Obj*)klass->superclass);
		}
		break;
	}
	case OBJ_INSTANCE: {
		ObjInstance* instance = (ObjInstance*)object;
		//klass is never NULL: class-less instances point at vm.defaultClass
		markObject((Obj*)instance->klass);
		markTable(&instance->fields);
		break;
	}
	case OBJ_FIBER: {
		ObjFiber* fiber = (ObjFiber*)object;

		//scan only the live prefix of this fiber's stack
		for (Value* slot = fiber->core.stack; slot < fiber->core.stackTop; slot++) {
			markValue(*slot);
		}

		for (uint32_t i = 0; i < fiber->core.frameCount; i++) {
			markObject((Obj*)fiber->core.frames[i].closure);
		}

		for (ObjUpvalue* upvalue = fiber->core.openUpvalues; upvalue != NULL; upvalue = upvalue->next) {
			markObject((Obj*)upvalue);
		}

		if (fiber->resumer != NULL) {
			markObject((Obj*)fiber->resumer);
		}

		markValue(fiber->result);
		break;
	}
	case OBJ_ARRAY: //only array-any needs gc scan
		markArrayAny((ObjArray*)object);
		break;
	}
}

static void traceReferences() {
	//plain LIFO drain over the gray worklist; blackenObject may push more
	//entries, grayCount covers that naturally. Committed pages of the gray
	//arena stay resident and are reused by the next GC cycle (same grow-only
	//behavior as the old heap stack)
	while (vm.grayCount > 0) {
		Obj* object = vm.grayStack[--vm.grayCount];
		blackenObject(object);
	}
}

static void sweep() {
	Obj* previous = NULL;
	Obj* object = vm.objects;

	while (object != NULL) {
		if (object->isMarked == vm.gcMark) {
			//object->isMarked = false;//clear the mark

			previous = object;
			object = OBJ_PTR_GET_NEXT(object);
		}
		else {
			Obj* unreached = object;
			object = OBJ_PTR_GET_NEXT(object);
			if (previous != NULL) {
				OBJ_PTR_SET_NEXT(previous, object);
			}
			else {
				vm.objects = object;
			}

			freeObject(unreached);
		}
	}
}

//inline caches are weak references: clear any invoke slot whose cached ObjClass*/ObjClosure*
//did not survive this GC cycle. Runs after tracing (final mark state) and before sweep
//(so the fast path never compares against freed or reused memory; NULL extraA always misses)
static void gc_clear_dead_ic_slots() {
	for (uint32_t i = 0; i < vm.icFuncCount; i++) {
		ObjFunction* fn = vm.icFuncs[i];
		if (fn == NULL || fn->caches == NULL) continue;

		for (uint16_t j = 0; j <= fn->cacheCount; j++) {
			InlineCacheSlot* c = &fn->caches[j];
			Obj* a = (Obj*)c->extraA;
			Obj* b = (Obj*)c->extraB;
			if (a == NULL && b == NULL) continue;

			if ((a != NULL && a->isMarked != vm.gcMark) ||
				(b != NULL && b->isMarked != vm.gcMark)) {
				c->extraA = NULL;
				c->extraB = NULL;
			}
		}
	}
}

void garbageCollect()
{
#if DEBUG_LOG_GC
	fprintf(stderr, "-- gc begin\n");
#endif

#if DEBUG_LOG_GC || LOG_GC_RESULT
	uint64_t time_gc = get_nanoseconds();
	size_t before = vm.bytesAllocated;
#endif
	//mark the state
	vm.gcWorking = 1;

	vm_storeCacheToFiber();

	markRoots();
	traceReferences();
	//inline caches are weak: drop slots that reference objects which are about to die
	//(must run before sweep so the fast path never compares against freed memory)
	gc_clear_dead_ic_slots();
	//tableRemoveWhite(&vm.strings);
	sweep();

	//reset the limit
	vm.nextGC = max(vm.bytesAllocated * GC_HEAP_GROW_FACTOR, vm.beginGC);
	//flip the mark
	vm.gcMark = !vm.gcMark;
	//mark the state
	vm.gcWorking = 0;

#if DEBUG_LOG_GC
	fprintf(stderr, "-- gc end\n");
#endif

#if DEBUG_LOG_GC || LOG_GC_RESULT
	double time_ms = (get_nanoseconds() - time_gc) * 1e-6;
	fprintf(stderr, "[gc] collected %zu bytes (from %zu to %zu) next at %zu, in %g ms\n",
		before - vm.bytesAllocated, before, vm.bytesAllocated, vm.nextGC, time_ms);
#endif
}

void changeNextGC(size_t newSize)
{
	vm.nextGC = newSize;
}

void changeBeginGC(size_t newSize)
{
	vm.beginGC = newSize;
}