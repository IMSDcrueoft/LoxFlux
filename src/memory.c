/*
* MIT License
* Copyright (c) 2025 IMSDcrueoft (https://github.com/IMSDcrueoft)
* See LICENSE file in the root directory for full license text.
*/
#include "memory.h"  
#include "object.h"  
#include "vm.h"
#include "allocator.h"
#include "gc.h"

Unknown_ptr reallocate_no_gc(Unknown_ptr pointer, size_t oldSize, size_t newSize)
{
	vm.bytesAllocated_no_gc += newSize - oldSize;

	if (newSize == 0) {
		if (pointer != NULL) {
#if LOG_EACH_MALLOC_INFO
			printf("[mem] free %p\n", pointer);
#endif
			mem_free(pointer);
		}

		return NULL;
	}

	Unknown_ptr result = mem_realloc(pointer, newSize);

#if LOG_EACH_MALLOC_INFO
	printf("[mem] realloc %p -> %p, %zu\n", pointer, result, newSize);
#endif

	if (result == NULL) {
		fprintf(stderr, "Memory reallocation failed!\n");
		exit(1);
	}

	return result;
}

Unknown_ptr reallocate(Unknown_ptr pointer, size_t oldSize, size_t newSize)
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
			printf("[mem] free %p\n", pointer);
#endif
			mem_free(pointer);
		}

		return NULL;
	}

	Unknown_ptr result = mem_realloc(pointer, newSize);

#if LOG_EACH_MALLOC_INFO
	printf("[mem] realloc %p -> %p, %zu\n", pointer, result, newSize);
#endif

	if (result == NULL) {
		fprintf(stderr, "Memory reallocation failed!\n");
		exit(1);
	}

	return result;
}

void freeObject(Obj* object) {
#if DEBUG_LOG_GC
	printf("[gc] %p free (%s)\n", (Unknown_ptr)object, objTypeInfo[object->type]);
#endif

	switch (object->type) {
	case OBJ_CLASS: {
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
	case OBJ_UPVALUE:
		FREE(ObjUpvalue, object);
		break;
	case OBJ_FUNCTION: {
		ObjFunction* function = (ObjFunction*)object;
		chunk_free(&function->chunk);
		FREE_NO_GC(ObjFunction, object);
		break;
	}
	case OBJ_NATIVE:
		FREE_NO_GC(ObjNative, object);
		break;
	case OBJ_STRING: {
		ObjString* string = (ObjString*)object;
		FREE_FLEX_NO_GC(ObjString, string, char, string->length + 1);//FAM object include'\0
		break;
	case OBJ_ARRAY:
	case OBJ_ARRAY_F64: {
		//they share the same struct
		ObjArray* array = (ObjArray*)object;
		FREE_ARRAY(char, array->payload, array->capacity * 8); //size 8 byte
		FREE(OBJ_ARRAY, object);
		break;
	}
	case OBJ_ARRAY_F32:
	case OBJ_ARRAY_U32:
	case OBJ_ARRAY_I32: {
		//they share the same struct
		ObjArray* array = (ObjArray*)object;
		FREE_ARRAY(char, array->payload, array->capacity * 4); //size 4 byte
		FREE(OBJ_ARRAY, object);
		break;
	}
	case OBJ_ARRAY_U16:
	case OBJ_ARRAY_I16: {
		//they share the same struct
		ObjArray* array = (ObjArray*)object;
		FREE_ARRAY(char, array->payload, array->capacity * 2); //size 2 byte
		FREE(OBJ_ARRAY, object);
		break;
	}
	case OBJ_ARRAY_U8:
	case OBJ_ARRAY_I8:
	case OBJ_STRING_BUILDER: {
		//they share the same struct
		ObjArray* array = (ObjArray*)object;
		FREE_ARRAY(char, array->payload, array->capacity * 1); //size 1 byte
		FREE(OBJ_ARRAY, object);
		break;
	}
	}
	}
}

void freeObjects()
{

#if DEBUG_LOG_GC
	printf("-- free dynamic objects\n");
#endif
	Obj* object = vm.objects;
	while (object != NULL) {
		Obj* next = object->next;
		freeObject(object);
		object = next;
	}

	if (vm.grayStack != NULL) {
		mem_free(vm.grayStack);
	}

#if DEBUG_LOG_GC
	printf("-- free static objects\n");
#endif
	Obj* object_no_gc = vm.objects_no_gc;
	while (object_no_gc != NULL) {
		Obj* next = object_no_gc->next;
		freeObject(object_no_gc);
		object_no_gc = next;
	}
}

void log_malloc_info()
{
	mi_stats_print(NULL);
}