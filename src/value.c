/*
 * MIT License
 * Copyright (c) 2025 IMSDcrueoft (https://github.com/IMSDcrueoft)
 * See LICENSE file in the root directory for full license text.
*/
#include "value.h"
#include "object.h"

bool valuesEqual(Value a, Value b)
{
#if NAN_BOXING
	if (IS_NUMBER(a) && IS_NUMBER(b)) {
		return AS_NUMBER(a) == AS_NUMBER(b);
	}
	else {
		return a == b;
	}
#else
	if (a.type != b.type) return false;
	switch (a.type) {
	case VAL_BOOL:   return AS_BOOL(a) == AS_BOOL(b);
	case VAL_NUMBER: return AS_NUMBER(a) == AS_NUMBER(b);
	case VAL_NIL:    return true;
	case VAL_OBJ:    return AS_OBJ(a) == AS_OBJ(b);
	default:         return false; // Unreachable.
	}
#endif
}

/* Remove redundant zeros from scientific notation (internal helper) */
static void remove_scientific_zeros(char* buffer) {
	char* e = strchr(buffer, 'e');
	if (!e) return;

	// Trim trailing zeros before exponent
	char* p = e - 1;
	while (p > buffer && *p == '0') p--;
	if (*p == '.') p--;  // Keep digit before decimal if needed

	memmove(p + 1, e, strlen(e) + 1);
}

/* Remove trailing zeros after decimal point (internal helper) */
static void remove_trailing_zeros(char* buffer) {
	char* p = strchr(buffer, '.');
	if (!p) return;

	// Scan from end to first non-zero
	p = buffer + strlen(buffer) - 1;
	while (p > buffer && *p == '0') p--;
	if (*p == '.') p--;  // Trim decimal if nothing after it

	*(p + 1) = '\0';
}

/**
 * Converts double to optimal string representation
 * @param value Input double value
 * @param buffer Output buffer
 * @param bufferSize Size of output buffer
 *
 * Features:
 * - Auto-detects pure integers (prints without decimal)
 * - Chooses between scientific and standard notation
 * - Handles special values (NaN, Inf)
 * - Removes unnecessary zeros
 */
void convert_adaptive_double(double value, char* buffer, uint32_t bufferSize) {
	// Handle special floating-point values
	if (isnan(value)) {
		snprintf(buffer, bufferSize, "NaN");
		return;
	}
	if (isinf(value)) {
		snprintf(buffer, bufferSize, value > 0 ? "Infinity" : "-Infinity");
		return;
	}

	// Check for integer values
	double int_part;
	if (fabs(modf(value, &int_part)) < DBL_EPSILON) {
		snprintf(buffer, bufferSize, "%.0f", int_part);
		return;
	}

	// Generate both possible representations
	char sci_buf[32], float_buf[32];
	snprintf(sci_buf, sizeof(sci_buf), "%.15e", value);
	snprintf(float_buf, sizeof(float_buf), "%.15f", value);
	remove_scientific_zeros(sci_buf);
	remove_trailing_zeros(float_buf);

	// Select the more compact representation
	const uint32_t sci_len = strlen(sci_buf);
	const uint32_t float_len = strlen(float_buf);
	const char* chosen = (sci_len < float_len) ? sci_buf : float_buf;

	snprintf(buffer, bufferSize, "%s", chosen);
}

void printValue(Value value) {
#if NAN_BOXING
	if (IS_BOOL(value)) {
		printf(AS_BOOL(value) ? "true" : "false");
	}
	else if (IS_NIL(value)) {
		printf("nil");
	}
	else if (IS_NUMBER(value)) {
		char buffer[40];
		convert_adaptive_double(AS_NUMBER(value), buffer, sizeof(buffer));
		printf("%s", buffer);
	}
	else if (IS_OBJ(value)) {
		printObject(value, false);
	}
#else
	switch (value.type) {
	case VAL_BOOL:
		printf(AS_BOOL(value) ? "true" : "false");
		break;
	case VAL_NIL: printf("nil"); break;
	case VAL_NUMBER: {
		char buffer[40];
		convert_adaptive_double(AS_NUMBER(value), buffer, sizeof(buffer));
		printf("%s", buffer);
		break;
	}
	case VAL_OBJ: printObject(value, false); break;
	}
#endif
}

void printValue_sys(Value value)
{
#if NAN_BOXING
	if (IS_BOOL(value)) {
		printf(AS_BOOL(value) ? "true" : "false");
	}
	else if (IS_NIL(value)) {
		printf("nil");
	}
	else if (IS_NUMBER(value)) {
		char buffer[40];
		convert_adaptive_double(AS_NUMBER(value), buffer, sizeof(buffer));
		printf("%s", buffer);
	}
	else if (IS_OBJ(value)) {
		printObject(value, true);
	}
#else
	switch (value.type) {
	case VAL_BOOL:
		printf(AS_BOOL(value) ? "true" : "false");
		break;
	case VAL_NIL: printf("nil"); break;
	case VAL_NUMBER: {
		char buffer[40];
		convert_adaptive_double(AS_NUMBER(value), buffer, sizeof(buffer));
		printf("%s", buffer);
		break;
	}
	case VAL_OBJ: printObject(value, true); break;
	}
#endif
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
		array->values = GROW_ARRAY_NO_GC(Value, array->values, oldCapacity, array->capacity);
	}

	array->values[array->count] = value;
	array->count++;
}

void valueArray_writeAt(ValueArray* array, Value value, uint32_t index)
{
	array->values[index] = value;
}

void valueArray_free(ValueArray* array) {
	FREE_ARRAY_NO_GC(Value, array->values, array->capacity);
	valueArray_init(array);
}

void valueHoles_init(ValueHoles* holes) {
	holes->holes = NULL;
	holes->count = 0;
	holes->capacity = 0;
}

void valueHoles_free(ValueHoles* holes) {
	FREE_ARRAY_NO_GC(uint32_t, holes->holes, holes->capacity);
	holes->holes = NULL;
	holes->count = 0;
	holes->capacity = 0;
}

void valueHoles_push(ValueHoles* holes, uint32_t index) {
	if (holes->count == holes->capacity) {
		uint32_t oldCapacity = holes->capacity;
		holes->capacity = GROW_CAPACITY(oldCapacity);
		holes->holes = GROW_ARRAY_NO_GC(uint32_t, holes->holes, oldCapacity, holes->capacity);
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