#include "memory.h"
#include "object.h"
#include "vm.h"
#include <mimalloc.h>

Unknown_ptr reallocate(Unknown_ptr pointer, size_t oldSize, size_t newSize)
{
	if (newSize == 0) {
		if (pointer != NULL) {
			mi_free(pointer);
		}

		return NULL;
	}

	Unknown_ptr result = mi_realloc(pointer, newSize);

	if (result == NULL) {
		fprintf(stderr, "Memory reallocation failed!\n");
		exit(1);
	}

	return result;
}

static void freeObject(Obj* object) {
	switch (object->type) {
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
	mi_version();
	mi_stats_print(NULL);
}