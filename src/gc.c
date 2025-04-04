/*
 * MIT License
 * Copyright (c) 2025 IM&SD (https://github.com/IMSDcrueoft)
 * See LICENSE file in the root directory for full license text.
*/
#include "gc.h"
#include "vm.h"
#include "compiler.h"
#include "allocator.h"
#include "memory.h"

static void blackenObject(Obj* object);
void markValue(Value value)
{
	if (IS_OBJ(value)) markObject(AS_OBJ(value));
}

static void markArray(ValueArray* array) {
	for (uint32_t i = 0; i < array->count; i++) {
		markValue(array->values[i]);
	}
}

static void markConstants(ValueArray* array) {
	for (uint32_t i = 0; i < array->count; i++) {
		Value value = array->values[i];
		if (IS_OBJ(value)) {
			AS_OBJ(value)->isMarked = true;
			blackenObject(AS_OBJ(value));
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
	markConstants(&vm.constants);//bug here

	markCompilerRoots();
}

void markObject(Obj* object)
{
	//skip the null and things that don't need mark
	if (object == NULL) return;
	//skip marked one
	if (object->isMarked) return;

	switch (object->type) {
	case OBJ_NATIVE:
	case OBJ_STRING:
		//set black(marked and not in gray stack)
		object->isMarked = true;
		return;
	}

#if DEBUG_LOG_GC
	printf("[gc] %p mark ", (Unknown_ptr)object);
	printValue(OBJ_VAL(object));
	printf("\n");
#endif
	object->isMarked = true;

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
	printf("[gc] %p blacken ", (Unknown_ptr)object);
	printValue(OBJ_VAL(object));
	printf("\n");
#endif

	switch (object->type) {
	case OBJ_CLOSURE: {
		ObjClosure* closure = (ObjClosure*)object;
		markObject((Obj*)closure->function);

		for (int32_t i = 0; i < closure->upvalueCount; i++) {
			markObject((Obj*)closure->upvalues[i]);
		}
		break;
	}
	case OBJ_FUNCTION: {
		ObjFunction* function = (ObjFunction*)object;
		markObject((Obj*)function->name);

		//I don't have this field in design
		//markArray(&function->chunk.constants);
		break;
	}
	case OBJ_UPVALUE:
		//When an upvalue is closed, it contains a reference to the closed-over value
		markValue(((ObjUpvalue*)object)->closed);
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

	const Value* constantBegin = vm.constants.values;
	const Value* constantEnd = vm.constants.values + vm.constants.capacity;

	while (object != NULL) {
		if (object->isMarked) {
			object->isMarked = false;//clear the mark

			previous = object;
			object = object->next;
		}
		else {
			Obj* unreached = object;
			object = object->next;
			if (previous != NULL) {
				previous->next = object;
			}
			else {
				vm.objects = object;
			}

			freeObject(unreached);
			//			Value* value = GET_VALUE_CONTAINER(unreached);
			//			// if its in constants, we need remove it and mark the hole for reuse
			//			if ((constantBegin <= value) && (value < constantEnd)) {
			//				// calculate the index,ptrdiff_t is unit count
			//				ptrdiff_t index = value - constantBegin;
			//#if DEBUG_LOG_GC
			//				printf("[gc] constants %td\n", index);
			//#endif
			//				valueHoles_push(&vm.constantHoles, index);
			//				freeObject(unreached);
			//				*value = NIL_VAL;//set to nil
			//			}
			//			else {
			//				freeObject(unreached);
			//			}
		}
	}
}

void collectGarbage()
{
#if DEBUG_LOG_GC
	printf("-- gc begin\n");
	uint64_t before = vm.bytesAllocated;
#endif
	markRoots();
	traceReferences();
	tableRemoveWhite(&vm.strings);
	sweep();

	vm.nextGC = vm.bytesAllocated * GC_HEAP_GROW_FACTOR;

#if DEBUG_LOG_GC
	printf("-- gc end\n");
	printf("[gc] collected %zu bytes (from %zu to %zu) next at %zu\n",
		before - vm.bytesAllocated, before, vm.bytesAllocated, vm.nextGC);
#endif
}