/*
 * MIT License
 * Copyright (c) 2025-2026 IMSDcrueoft (https://github.com/IMSDcrueoft)
 * See LICENSE file in the root directory for full license text.
*/
#pragma once
#include "common.h"

//use nan boxing
#ifndef NAN_BOXING
#define NAN_BOXING 1
#endif // !NAN_BOXING

typedef struct Obj Obj;
typedef struct ObjString ObjString;

#if NAN_BOXING
typedef uint64_t LfxValue;

HOT_FUNCTION
static inline LfxValue numToValue(double num) {
	LfxValue value;
	memcpy(&value, &num, sizeof(double));
	return value;
}

HOT_FUNCTION
static inline double valueToNum(LfxValue value) {
	double num;
	memcpy(&num, &value, sizeof(LfxValue));
	return num;
}

#define SIGN_BIT ((uint64_t)0x8000000000000000)
#define QNAN     ((uint64_t)0x7ffc000000000000)
#define TAG_NIL   1 // 01.
#define TAG_FALSE 2 // 10.
#define TAG_TRUE  3 // 11.

#define FALSE_VAL       ((LfxValue)(uint64_t)(QNAN | TAG_FALSE))
#define TRUE_VAL        ((LfxValue)(uint64_t)(QNAN | TAG_TRUE))

#define NUMBER_VAL(num)		numToValue(num)
#define AS_NUMBER(value)    valueToNum(value)
#define IS_NUMBER(value)    (((value) & QNAN) != QNAN)
#define AS_BINARY(value)	(value)

#define NIL_VAL         ((LfxValue)(uint64_t)(QNAN | TAG_NIL))
#define IS_NIL(value)   ((value) == NIL_VAL)
#define NOT_NIL(value)  ((value) != NIL_VAL)

#define BOOL_VAL(b)     ((b) ? TRUE_VAL : FALSE_VAL)
#define AS_BOOL(value)  ((value) == TRUE_VAL)
#define IS_BOOL(value)  (((value) | 1) == TRUE_VAL)

#define OBJ_VAL(obj)	(LfxValue)(SIGN_BIT | QNAN | (uint64_t)(uintptr_t)(obj))
#define AS_OBJ(value)	((Obj*)(uintptr_t)((value) & ~(SIGN_BIT | QNAN)))
#define IS_OBJ(value)	(((value) & (QNAN | SIGN_BIT)) == (QNAN | SIGN_BIT))

#else

typedef enum {
	VAL_BOOL,
	VAL_NIL, //nil should not be zero
	VAL_NUMBER,
	VAL_OBJ,
} ValueType;

//the dynamic value
typedef struct {
	ValueType type;
	union {
		bool boolean;
		double number;

		Obj* obj;

		//convert to binary
		uint64_t binary;
	} as;
} LfxValue;

#define NUMBER_VAL(value)	((LfxValue){VAL_NUMBER, {.number = value}})
#define AS_NUMBER(value)	((value).as.number)
#define IS_NUMBER(value)	((value).type == VAL_NUMBER)
#define AS_BINARY(value)	((value).as.binary)

#define NIL_VAL           ((LfxValue){VAL_NIL, {.number = 0}})
#define IS_NIL(value)     ((value).type == VAL_NIL)
#define NOT_NIL(value)	  ((value).type != VAL_NIL)

#define BOOL_VAL(value)   ((LfxValue){VAL_BOOL, {.boolean = value}})
#define AS_BOOL(value)    ((value).as.boolean)
#define IS_BOOL(value)    ((value).type == VAL_BOOL)

#define OBJ_VAL(object)   ((LfxValue){VAL_OBJ, {.obj = (Obj*)object}})
#define AS_OBJ(value)     ((value).as.obj)
#define IS_OBJ(value)     ((value).type == VAL_OBJ)

#endif

#define NAN_VAL				NUMBER_VAL(NAN) //use this to return a NaN

typedef struct {
	uint32_t capacity; //limit to 4G
	uint32_t count;    //limit to 4G (high-water mark, holes included)
	LfxValue* values;

	//internal: head of the hole freeList; a hole slot stashes the next hole
	//index as raw bytes (VALUEHOLES_EMPTY = no holes)
	uint32_t valueHoleIndex;
} ValueArray;

_Static_assert(sizeof(LfxValue) >= 4, "In order to accommodate the reuse holes, the value needs to be at least 4B in size.");

bool valuesEqual(LfxValue a, LfxValue b);
bool valuesEqual_number(LfxValue a, double b);
void convert_adaptive_double(double value, char* buffer, uint32_t bufferSize);//ECMAScript type Number; buffer size 40 is enough
//all rendering goes through this: stream and expansion are explicit at every call site
void printValue(FILE* stream, LfxValue value, bool isExpand);

void valueArray_init(ValueArray* array);
//returns the index the value was written to: a reusable hole when one exists,
//appended at the end otherwise (hole logic is fully internal)
uint32_t valueArray_write(ValueArray* array, LfxValue value);
void valueArray_writeAt(ValueArray* array, LfxValue value, uint32_t index);
void valueArray_free(ValueArray* array);
//dig a hole: mark the slot at index as reusable; the slot is treated as raw
//memory from here on and will be overwritten by the next write
void valueArray_erase(ValueArray* array, uint32_t index);

//cold fallback for finite |d| >= 2^63 (and all non-finite values): every double
//in that range is integral, so fmod leaves no fractional part and the final
//cast already matches ToUint32. A direct caller that can pass fractional values
//must trunc() first; the hot paths never reach here with one.
NOINLINE_FUNCTION
static inline uint32_t double_to_u32_slow(double d) {
	if (!isfinite(d)) return 0;
	d = fmod(d, 0x1p32);
	if (d < 0.0) d += 0x1p32;
	return (uint32_t)d;
}

//JS ToUint32; the int64 fast path covers the usual range, 2^63+ / non-finite
//values fall back to the slow path
HOT_FUNCTION
static inline uint32_t double_to_u32(double d) {
	if (fabs(d) < 0x1p63) return (uint32_t)(int64_t)d;
	return double_to_u32_slow(d);
}

//JS ToInt32; common small values convert directly in 32-bit, wider values keep
//the int64 fast path
HOT_FUNCTION
static inline int32_t double_to_i32(double d) {
	if (fabs(d) < 0x1p31) return (int32_t)d;
	if (fabs(d) < 0x1p63) return (int32_t)(int64_t)d;
	return (int32_t)double_to_u32_slow(d);
}

//shift count = ToUint32(d) & 31
HOT_FUNCTION
static inline uint32_t double_to_shift32(double d) {
	if (fabs(d) < 0x1p63) return (uint32_t)(int64_t)d & 31u;
	return 0u;
}

//float conversion is UB for finite values outside FLT_MAX; clamp to IEEE infinity instead
HOT_FUNCTION
static inline float double_to_f32(double d) {
	if (isnan(d)) return NAN;
	if (d > FLT_MAX) return INFINITY;
	if (d < -FLT_MAX) return -INFINITY;
	return (float)d;
}

//JS ToIntegerOrInfinity + relative-index clamp, without ever casting an
//out-of-range double (NaN/Inf/huge values are clamped before the int64 cast)
HOT_FUNCTION
static inline int64_t double_to_slice_index(double d, int64_t length) {
	if (isnan(d)) return 0;
	d = trunc(d);
	if (d < 0.0) d += (double)length;
	if (d < 0.0) return 0;
	if (d > (double)length) return length;
	return (int64_t)d;
}