/*
 * MIT License
 * Copyright (c) 2025 IM&SD (https://github.com/IMSDcrueoft)
 * See LICENSE file in the root directory for full license text.
*/
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

static void remove_trailing_zeros(STR str) {
	STR dot = strchr(str, '.');
	if (!dot) return;

	STR end = str + strlen(str) - 1;
	while (end > dot && *end == '0') {
		*end-- = '\0';
	}

	if (*end == '.') {
		*end = '\0';
	}
}

static void remove_scientific_zeros(STR str) {
	STR e_pos = strchr(str, 'e');
	if (!e_pos) return;

	*e_pos = '\0';
	remove_trailing_zeros(str);
	*e_pos = 'e';
}

static void print_adaptive_double(double value) {
	double int_part;
	double fractional = modf(value, &int_part);

	if (fabs(fractional) < 1e-10) {
		printf("%.0f", int_part);
		return;
	}

	char buffer[50];
	const double abs_value = fabs(value);

	if (abs_value >= 1e9 || (abs_value <= 1e-5 && value != 0.0)) {
		snprintf(buffer, sizeof(buffer), "%.15e", value);
		remove_scientific_zeros(buffer);
	}
	else {
		snprintf(buffer, sizeof(buffer), "%.15f", value);
		remove_trailing_zeros(buffer);
	}

	printf("%s", buffer);
}

void printValue(Value value) {
	switch (value.type) {
	case VAL_BOOL:
		printf(AS_BOOL(value) ? "true" : "false");
		break;
	case VAL_NIL: printf("nil"); break;
	case VAL_NUMBER: {
		print_adaptive_double(AS_NUMBER(value)); break;
	}
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

void valueArray_writeAt(ValueArray* array, Value value, uint32_t index)
{
	array->values[index] = value;
}

void valueArray_free(ValueArray* array) {
	FREE_ARRAY(Value, array->values, array->capacity);
	valueArray_init(array);
}

void valueHoles_init(ValueHoles* holes) {
	holes->holes = NULL;
	holes->count = 0;
	holes->capacity = 0;
}

void valueHoles_free(ValueHoles* holes) {
	FREE_ARRAY(uint32_t, holes->holes, holes->capacity);
	holes->holes = NULL;
	holes->count = 0;
	holes->capacity = 0;
}

void valueHoles_push(ValueHoles* holes, uint32_t index) {
	if (holes->count == holes->capacity) {
		uint32_t oldCapacity = holes->capacity;
		holes->capacity = GROW_CAPACITY(oldCapacity);
		holes->holes = GROW_ARRAY(uint32_t, holes->holes, oldCapacity, holes->capacity);
	}
	holes->holes[holes->count++] = index;
}

void valueHoles_pop(ValueHoles* holes)
{
	if (holes->count > 0) {
		holes->count--;
	}
}

uint32_t valueHoles_get(ValueHoles* holes) {
	if (holes->count == 0) {
		return VALUEHOLES_EMPTY;
	}
	return holes->holes[--holes->count];
}