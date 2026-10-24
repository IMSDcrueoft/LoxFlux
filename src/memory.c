/*
* MIT License
* Copyright (c) 2025-2026 IMSDcrueoft (https://github.com/IMSDcrueoft)
* See LICENSE file in the root directory for full license text.
*/
#include "memory.h"
#include "object.h"
#include "vm.h"
#include "allocator.h"
#include "gc.h"

void* reallocate_no_gc(void* pointer, size_t oldSize, size_t newSize)
{
	vm.bytesAllocated_no_gc += newSize - oldSize;

	if (newSize == 0) {
		if (pointer != NULL) {
#if LOG_EACH_MALLOC_INFO
			fprintf(stderr, "[mem] free %p\n", pointer);
#endif
			mem_free(pointer);
		}

		return NULL;
	}

	void* result = mem_realloc(pointer, newSize);

#if LOG_EACH_MALLOC_INFO
	fprintf(stderr, "[mem] realloc %p -> %p, %zu\n", pointer, result, newSize);
#endif

	if (result == NULL) {
		fprintf(stderr, "Memory reallocation failed!\n");
		exit(1);
	}

	return result;
}

void* reallocate(void* pointer, size_t oldSize, size_t newSize)
{
	vm.bytesAllocated += newSize - oldSize;

	if (newSize > oldSize) {
#if DEBUG_STRESS_GC
		garbageCollect();
#endif
		if (vm.bytesAllocated > vm.nextGC) {
			garbageCollect();
		}
	}

	if (newSize == 0) {
		if (pointer != NULL) {
#if LOG_EACH_MALLOC_INFO
			fprintf(stderr, "[mem] free %p\n", pointer);
#endif
			mem_free(pointer);
		}

		return NULL;
	}

	void* result = mem_realloc(pointer, newSize);

#if LOG_EACH_MALLOC_INFO
	fprintf(stderr, "[mem] realloc %p -> %p, %zu\n", pointer, result, newSize);
#endif

	if (result == NULL) {
		fprintf(stderr, "Memory reallocation failed!\n");
		exit(1);
	}

	return result;
}

//unlink one object from the immortal no-GC list
//(prev/cur walk, works with both Obj header layouts; must run before freeObject on no-GC objects)
void unlinkObj_no_gc(Obj* object) {
	Obj* prev = NULL;
	Obj* cur = vm.objects_no_gc;

	while (cur != NULL) {
		Obj* next = OBJ_PTR_GET_NEXT(cur);

		if (cur == object) {
			if (prev == NULL) {
				vm.objects_no_gc = next;
			}
			else {
				OBJ_PTR_SET_NEXT(prev, next);
			}
			return;
		}

		prev = cur;
		cur = next;
	}
}

void freeObject(Obj* object) {
#if DEBUG_LOG_GC
	fprintf(stderr, "[gc] %p free (%s)\n", (void*)object, objTypeInfo[object->type]);
#endif

	switch (object->type) {
	case OBJ_CLASS: {
		ObjClass* klass = (ObjClass*)object;
		table_free(&klass->methods);
		FREE(ObjClass, object);
		break;
	}
	case OBJ_INSTANCE: {
		ObjInstance* instance = (ObjInstance*)object;
		table_free(&instance->fields);
		FREE(ObjInstance, object);
		break;
	}
	case OBJ_CLOSURE: {
		ObjClosure* closure = (ObjClosure*)object;
		FREE_ARRAY(ObjUpvalue*, closure->upvalues, closure->upvalueCount);

		FREE(ObjClosure, object);
		break;
	}
	case OBJ_BOUND_METHOD: {
		FREE(ObjBoundMethod, object);
		break;
	}
	case OBJ_FIBER: {
		ObjFiber* fiber = (ObjFiber*)object;

		//frames are ordinary no-GC heap memory and are always released
		FREE_ARRAY_NO_GC(CallFrame, fiber->core.frames, fiber->core.frameCapacity);

		if (vm.fiberFreeCount < FIBER_CACHE_MAX) {
			assert(vm.bytesAllocated >= sizeof(ObjFiber));
			vm.bytesAllocated -= sizeof(ObjFiber);
			//reuse the object header as the free-list link
			OBJ_PTR_SET_NEXT(&fiber->obj, vm.fiberFreeList);
			vm.fiberFreeList = fiber;
			vm.fiberFreeCount++;
		}
		else {
			arenaBump_destroy(&fiber->stackArena);
			FREE(ObjFiber, fiber);
		}

		break;
	}
	case OBJ_UPVALUE:
		FREE(ObjUpvalue, object);
		break;
	case OBJ_FUNCTION: {
		ObjFunction* function = (ObjFunction*)object;
		chunk_free(&function->chunk);
		//inline cache slots are owned by the object (post-endCompiler size is exactly cacheCount+1;
		//FAM-sealed scratch objects arrive with caches == NULL, so they skip this)
		if (function->caches != NULL) {
			FREE_ARRAY_NO_GC(InlineCacheSlot, function->caches, (uint32_t)function->cacheCount + 1);
			function->caches = NULL;
		}
		FREE_FLEX_NO_GC(ObjFunction, function, Value, function->constantsCount);
		break;
	}
	case OBJ_NATIVE:
		FREE_NO_GC(ObjNative, object);
		break;
	case OBJ_STRING:
		//strings are arena-allocated (fixed-VA bump, released at vm exit):
		//never linked into the object lists, so this case is unreachable;
		//kept only for switch coverage
		break;
	case OBJ_ARRAY: {
		//they share the same struct
		ObjArray* array = (ObjArray*)object;
		FREE_ARRAY(Value, array->payload, array->capacity);
#if DEBUG_LOG_GC
		fprintf(stderr, "[gc] %p free buffer : %zu\n", (void*)array->payload, array->capacity * sizeof(Value));
#endif
		FREE(ObjArray, object);
		break;
	}
	case OBJ_ARRAY_F64: {
		//they share the same struct
		ObjArray* array = (ObjArray*)object;
		FREE_ARRAY(double, array->payload, array->capacity); //size 8 byte
#if DEBUG_LOG_GC
		fprintf(stderr, "[gc] %p free buffer : %zu\n", (void*)array->payload, array->capacity * sizeof(double));
#endif
		FREE(ObjArray, object);
		break;
	}
	case OBJ_ARRAY_F32:
	case OBJ_ARRAY_U32:
	case OBJ_ARRAY_I32: {
		//they share the same struct
		ObjArray* array = (ObjArray*)object;
		FREE_ARRAY(uint32_t, array->payload, array->capacity); //size 4 byte
#if DEBUG_LOG_GC
		fprintf(stderr, "[gc] %p free buffer : %zu\n", (void*)array->payload, array->capacity * sizeof(uint32_t));
#endif
		FREE(ObjArray, object);
		break;
	}
	case OBJ_ARRAY_U16:
	case OBJ_ARRAY_I16: {
		//they share the same struct
		ObjArray* array = (ObjArray*)object;
		FREE_ARRAY(uint16_t, array->payload, array->capacity); //size 2 byte
#if DEBUG_LOG_GC
		fprintf(stderr, "[gc] %p free buffer : %zu\n", (void*)array->payload, array->capacity * sizeof(uint16_t));
#endif
		FREE(ObjArray, object);
		break;
	}
	case OBJ_ARRAY_U8:
	case OBJ_ARRAY_I8:
	case OBJ_STRING_BUILDER: {
		//they share the same struct
		ObjArray* array = (ObjArray*)object;
		FREE_ARRAY(uint8_t, array->payload, array->capacity); //size 1 byte
#if DEBUG_LOG_GC
		fprintf(stderr, "[gc] %p free buffer : %zu\n", (void*)array->payload, (size_t)array->capacity);
#endif
		FREE(ObjArray, object);
		break;
	}
	}
}

void freeObjects()
{
#if DEBUG_LOG_GC
	fprintf(stderr, "-- free dynamic objects\n");
#endif
	Obj* object = vm.objects;
	while (object != NULL) {
		Obj* next = OBJ_PTR_GET_NEXT(object);
		freeObject(object);
		object = next;
	}

#if DEBUG_LOG_GC
	fprintf(stderr, "-- free static objects\n");
#endif
	Obj* object_no_gc = vm.objects_no_gc;
	while (object_no_gc != NULL) {
		Obj* next = OBJ_PTR_GET_NEXT(object_no_gc);
		freeObject(object_no_gc);
		object_no_gc = next;
	}
}

//Allocate or recycle the raw ObjFiber storage. This deliberately does not
//initialize fiber payload state; object.c owns that constructor logic.
struct ObjFiber* allocFiber()
{
	ObjFiber* fiber = NULL;

	if (vm.fiberFreeList != NULL) {
		//cached fibers are linked through the object header; their allocator
		//memory was removed from GC accounting when they entered the cache
		fiber = vm.fiberFreeList;
		vm.fiberFreeList = (ObjFiber*)OBJ_PTR_GET_NEXT((Obj*)fiber);
		vm.fiberFreeCount--;

		vm.bytesAllocated += sizeof(ObjFiber);

		//relink the recycled object into the live object list
		OBJ_PTR_SET_NEXT(&fiber->obj, vm.objects);
		fiber->obj.isMarked = !vm.gcMark;
		vm.objects = &fiber->obj;
	}
	else {
		fiber = (ObjFiber*)reallocate(NULL, 0, sizeof(ObjFiber));

#if DEBUG_LOG_GC
		fprintf(stderr, "[gc] %p allocate %zu for (%s)\n",
			(void*)fiber, sizeof(ObjFiber), objTypeInfo[OBJ_FIBER]);
#endif

		OBJ_PTR_SET_NEXT(&fiber->obj, vm.objects);
		fiber->obj.type = OBJ_FIBER;
		fiber->obj.isMarked = !vm.gcMark;
		vm.objects = &fiber->obj;

		memset(&fiber->stackArena, 0, sizeof(fiber->stackArena));

		if (!arenaBump_init(&fiber->stackArena, STACK_ARENA_SIZE, false, 0, 16 << 10)) {
			fprintf(stderr, "Failed to reserve the fiber stack arena.\n");
			exit(1);
		}
	}

	return fiber;
}

void freeFiberFreeList()
{
	while (vm.fiberFreeList != NULL) {
		ObjFiber* fiber = vm.fiberFreeList;
		vm.fiberFreeList = (ObjFiber*)OBJ_PTR_GET_NEXT((Obj*)fiber);

		//cached fibers were already removed from GC accounting when enqueued
		arenaBump_destroy(&fiber->stackArena);
		mem_free(fiber);
	}

	vm.fiberFreeCount = 0;
}

//format bytes with an auto-scaled unit, mimalloc table style ("64.0 KiB")
COLD_FUNCTION
static void formatMemSize(char* buffer, size_t bufferSize, size_t bytes)
{
	double value = (double)bytes;
	const char* unit = "B";

	if (bytes >= 1024ull * 1024 * 1024) {
		value /= 1024.0 * 1024 * 1024;
		unit = "GiB";
	}
	else if (bytes >= 1024 * 1024) {
		value /= 1024.0 * 1024;
		unit = "MiB";
	}
	else if (bytes >= 1024) {
		value /= 1024.0;
		unit = "KiB";
	}

	snprintf(buffer, bufferSize, "%.1f %s", value, unit);
}

COLD_FUNCTION
void log_malloc_info()
{
	//the interned strings and the gc gray worklist live in OS VA arenas outside
	//the allocator, so their bytes never show up in mem_print_stats: report them
	//in the same table style. Fiber stacks are intentionally not aggregated here.
	char strCommitted[32];
	char strReserved[32];
	formatMemSize(strCommitted, sizeof(strCommitted), vm.stringArena.commitTop);
	formatMemSize(strReserved, sizeof(strReserved), arenaBump_capacity(&vm.stringArena));

	char grayCommitted[32];
	char grayReserved[32];
	formatMemSize(grayCommitted, sizeof(grayCommitted), arenaBump_committed(&vm.grayArena));
	formatMemSize(grayReserved, sizeof(grayReserved), arenaBump_capacity(&vm.grayArena));

	fprintf(stderr, " gc gray             current\n");
	fprintf(stderr, "  committed   : %12s\n", grayCommitted);
	fprintf(stderr, "  reserved    : %12s\n", grayReserved);
	fprintf(stderr, "\n");
	fprintf(stderr, " vm strings          current\n");
	fprintf(stderr, "  committed   : %12s\n", strCommitted);
	fprintf(stderr, "  reserved    : %12s\n", strReserved);
	fprintf(stderr, "\n");

	mem_print_stats(stderr);
}