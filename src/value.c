#include "value.h"
#include "object.h"

bool valuesEqual(Value a, Value b)
{
	if (a.type != b.type) return false;
	switch (a.type) {
	case VAL_BOOL:   return AS_BOOL(a) == AS_BOOL(b);
	case VAL_NUMBER: return AS_NUMBER(a) == AS_NUMBER(b);
	case VAL_NIL:    return true;
	case VAL_OBJ:    return AS_OBJ(a) == AS_OBJ(b);
	default:         return false; // Unreachable.
	}
}
#include "vm.h"
void printValue(Value value) {
	switch (value.type) {
	case VAL_BOOL:
		printf(AS_BOOL(value) ? "true" : "false");
		break;
	case VAL_NIL: printf("nil"); break;
	case VAL_NUMBER: printf("%g", AS_NUMBER(value)); break;
	case VAL_OBJ: printObject(value); break;
	}
}

void valueArray_init(ValueArray* array) {
	array->values = NULL;
	array->capacity = 0u;
	array->count = 0u;
}

void valueArray_write(ValueArray* array, Value value) {
	if (array->capacity < array->count + 1) {
		uint32_t oldCapacity = array->capacity;
		array->capacity = GROW_CAPACITY(oldCapacity);
		array->values = GROW_ARRAY(Value, array->values, oldCapacity, array->capacity);
	}

	array->values[array->count] = value;
	array->count++;
}

void valueArray_free(ValueArray* array) {
	FREE_ARRAY(Value, array->values, array->capacity);
	valueArray_init(array);
}