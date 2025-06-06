/*
 * MIT License
 * Copyright (c) 2025 IMSDcrueoft (https://github.com/IMSDcrueoft)
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
	for (Value* slot = vm.stack; slot < vm.stackTop; slot++) {
		markValue(*slot);
	}

	for (int32_t i = 0; i < vm.frameCount; i++) {
		markObject((Obj*)vm.frames[i].closure);
	}

	for (ObjUpvalue* upvalue = vm.openUpvalues; upvalue != NULL; upvalue = upvalue->next) {
		markObject((Obj*)upvalue);
	}

	markTable(&vm.globals);
	//the shared constants don't gc
	//markConstants(&vm.constants);

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
	printf("[gc] %p mark ", (void*)object);
	printValue(OBJ_VAL(object));
	printf("\n");
#endif
	object->isMarked = vm.gcMark;

	if (vm.grayCapacity < vm.grayCount + 1) {
		vm.grayCapacity = GROW_CAPACITY(vm.grayCapacity);
		vm.grayStack = (Obj**)mem_realloc(vm.grayStack, sizeof(Obj*) * vm.grayCapacity);
		//We don’t want growing the gray stack during a GC to cause the GC to recursively start a new GC
		if (vm.grayStack == NULL) exit(1);//realloc failed
	}

	vm.grayStack[vm.grayCount++] = object;
}

static void blackenObject(Obj* object) {
#if DEBUG_LOG_GC
	printf("[gc] %p blacken ", (void*)object);
	printValue(OBJ_VAL(object));
	printf("\n");
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
		break;
	}
	case OBJ_INSTANCE: {
		ObjInstance* instance = (ObjInstance*)object;
		ObjClass* klass = instance->klass;
		if (klass != NULL) {
			markObject((Obj*)klass);
			markTable(&instance->fields);
		}
		break;
	}
	case OBJ_ARRAY: //only array-any needs gc scan
		markArrayAny((ObjArray*)object);
		break;
	}
}

static void traceReferences() {
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

void garbageCollect()
{
#if DEBUG_LOG_GC
	printf("-- gc begin\n");
#endif

#if DEBUG_LOG_GC || LOG_GC_RESULT
	uint64_t time_gc = get_nanoseconds();
	uint64_t before = vm.bytesAllocated;
#endif
	//mark the state
	vm.gcWorking = 1;

	markRoots();
	traceReferences();
	//tableRemoveWhite(&vm.strings);
	sweep();

	//reset the limit
	vm.nextGC = max(vm.bytesAllocated * GC_HEAP_GROW_FACTOR, vm.beginGC);
	//flip the mark
	vm.gcMark = !vm.gcMark;
	//mark the state
	vm.gcWorking = 0;

#if DEBUG_LOG_GC
	printf("-- gc end\n");
#endif

#if DEBUG_LOG_GC || LOG_GC_RESULT
	double time_ms = (get_nanoseconds() - time_gc) * 1e-6;
	printf("[gc] collected %zu bytes (from %zu to %zu) next at %zu, in %g ms\n",
		before - vm.bytesAllocated, before, vm.bytesAllocated, vm.nextGC, time_ms);
#endif
}

void changeNextGC(uint64_t newSize)
{
	vm.nextGC = newSize;
}

void changeBeginGC(uint64_t newSize)
{
	vm.beginGC = newSize;
}