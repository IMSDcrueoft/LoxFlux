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

//append chars with bounds guard (output never truncates with the documented 40-byte buffer, this is defensive)
static void append_chars(char* buffer, uint32_t bufferSize, uint32_t* pos, const char* src, uint32_t length) {
	for (uint32_t i = 0; i < length; i++) {
		if (*pos + 1 >= bufferSize) break;
		buffer[(*pos)++] = src[i];
	}
}

/**
 * ECMAScript-aligned double to string (identical output to JS String(number)).
 * Reference: tc39/ecma262 6.1.6.1.20 Number::toString; cross-checked with node.
 *
 * - shortest round-trip digits: minimal significant-digit %Ne representation
 *   that parses back to the exact same double (17 digits always round-trip)
 * - notation per spec: plain decimal when -6 < n <= 21 (n = decimal exponent),
 *   otherwise "d.ddd e±x" with signed, zero-unpadded exponent
 * - NaN -> "NaN", ±0 -> "0", ±Inf -> "Infinity"/"-Infinity", -0.0 has no sign
 *
 * @param bufferSize should be at least 40 (max output is 25 bytes + NUL)
 */
void convert_adaptive_double(double value, char* buffer, uint32_t bufferSize) {
	if (isnan(value)) {
		snprintf(buffer, bufferSize, "NaN");
		return;
	}
	if (value == 0.0) {//covers +0.0 and -0.0 (JS prints "0" for both)
		snprintf(buffer, bufferSize, "0");
		return;
	}
	bool negative = (value < 0.0);
	if (isinf(value)) {
		snprintf(buffer, bufferSize, negative ? "-Infinity" : "Infinity");
		return;
	}
	if (negative) value = -value;

	//shortest round-trip search: p = significant digit count of "%.(p-1)e"
	char sci[40];
	int precision;
	for (precision = 1; precision < 17; precision++) {
		snprintf(sci, sizeof(sci), "%.*e", precision - 1, value);
		if (strtod(sci, NULL) == value) break;
	}
	if (precision == 17) snprintf(sci, sizeof(sci), "%.16e", value);

	//parse "d.ddd...e±XX" into digit sequence + decimal exponent
	//n is the spec's exponent: value = D[0].D[1..] * 10^(n-1), i.e. 0.<digits> * 10^n
	char digits[24];
	int digitCount = 0;
	char* e = strchr(sci, 'e');
	int exponent = atoi(e + 1);
	for (char* p = sci; p < e; p++) {
		if (*p >= '0' && *p <= '9') digits[digitCount++] = *p;
	}
	//defensive: shortest search never yields trailing zeros, trim them anyway
	while (digitCount > 1 && digits[digitCount - 1] == '0') digitCount--;

	const int n = exponent + 1;
	const int k = digitCount;

	uint32_t pos = 0;
	if (negative) buffer[pos++] = '-';

	if (k <= n && n <= 21) {//integer-shaped: digits + (n-k) trailing zeros
		append_chars(buffer, bufferSize, &pos, digits, (uint32_t)k);
		for (int i = k; i < n; i++) {
			append_chars(buffer, bufferSize, &pos, "0", 1);
		}
	}
	else if (0 < n && n <= 21) {//decimal point inside the digits
		append_chars(buffer, bufferSize, &pos, digits, (uint32_t)n);
		append_chars(buffer, bufferSize, &pos, ".", 1);
		append_chars(buffer, bufferSize, &pos, digits + n, (uint32_t)(k - n));
	}
	else if (-6 < n && n <= 0) {//"0." + (-n) zeros + digits
		append_chars(buffer, bufferSize, &pos, "0.", 2);
		for (int i = 0; i < -n; i++) {
			append_chars(buffer, bufferSize, &pos, "0", 1);
		}
		append_chars(buffer, bufferSize, &pos, digits, (uint32_t)k);
	}
	else {//exponential notation: "d[.rest]e±(n-1)", exponent never zero here
		append_chars(buffer, bufferSize, &pos, digits, 1);
		if (k > 1) {
			append_chars(buffer, bufferSize, &pos, ".", 1);
			append_chars(buffer, bufferSize, &pos, digits + 1, (uint32_t)(k - 1));
		}
		char expText[8];
		int written = snprintf(expText, sizeof(expText), "e%c%d", (n - 1 >= 0) ? '+' : '-', (n - 1 >= 0) ? (n - 1) : -(n - 1));
		append_chars(buffer, bufferSize, &pos, expText, (uint32_t)written);
	}

	buffer[pos] = '\0';
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