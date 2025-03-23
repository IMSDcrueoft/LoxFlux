/*
* MIT License
* Copyright (c) 2025 IM&SD (https://github.com/IMSDcrueoft)
* See LICENSE file in the root directory for full license text.
*/
#include "memory.h"  
#include "object.h"  
#include "vm.h"
#include "allocator.h"

Unknown_ptr reallocate(Unknown_ptr pointer, size_t oldSize, size_t newSize)
{
	if (newSize == 0) {
		if (pointer != NULL) {
#if LOG_EACH_MALLOC_INFO
			printf("[mem_free] %p\n", pointer);
#endif
			mem_free(pointer);
		}

		return NULL;
	}

	Unknown_ptr result = mem_realloc(pointer, newSize);

#if LOG_EACH_MALLOC_INFO
	printf("[mem_realloc] %p -> %p, %zu\n", pointer, result, newSize);
#endif

	if (result == NULL) {
		fprintf(stderr, "Memory reallocation failed!\n");
		exit(1);
	}

	return result;
}

static void freeObject(Obj* object) {
	switch (object->type) {
	case OBJ_CLOSURE: {
		FREE(ObjClosure, object);
		break;
	}
	case OBJ_FUNCTION: {
		ObjFunction* function = (ObjFunction*)object;
		chunk_free(&function->chunk);
		FREE(ObjFunction, object);
		break;
	}
	case OBJ_NATIVE:
		FREE(ObjNative, object);
		break;
	case OBJ_STRING: {
		ObjString* string = (ObjString*)object;
		FREE(ObjString, object);//FAM object  
		break;
	}
	}
}

void freeObjects()
{
	Obj* object = vm.objects;
	while (object != NULL) {
		Obj* next = object->next;
		freeObject(object);
		object = next;
	}
}

void log_malloc_info()
{
	mi_stats_print(NULL);
}