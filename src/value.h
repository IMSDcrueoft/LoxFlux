/*
 * MIT License
 * Copyright (c) 2025 IMSDcrueoft (https://github.com/IMSDcrueoft)
 * See LICENSE file in the root directory for full license text.
*/
#pragma once
#include "common.h"

typedef enum {
	VAL_BOOL,
	VAL_NIL, //nil should not be zero
	VAL_NUMBER,
	VAL_OBJ,
} ValueType;

typedef struct Obj Obj;
typedef struct ObjString ObjString;

#if NAN_BOXING

typedef uint64_t Value;

HOT_FUNCTION
static inline Value numToValue(double num) {
	Value value;
	memcpy(&value, &num, sizeof(double));
	return value;
}

HOT_FUNCTION
static inline double valueToNum(Value value) {
	double num;
	memcpy(&num, &value, sizeof(Value));
	return num;
}

#define SIGN_BIT ((uint64_t)0x8000000000000000)
#define QNAN     ((uint64_t)0x7ffc000000000000)
#define TAG_NIL   1 // 01.
#define TAG_FALSE 2 // 10.
#define TAG_TRUE  3 // 11.

#define FALSE_VAL       ((Value)(uint64_t)(QNAN | TAG_FALSE))
#define TRUE_VAL        ((Value)(uint64_t)(QNAN | TAG_TRUE))

#define NUMBER_VAL(num)		numToValue(num)
#define AS_NUMBER(value)    valueToNum(value)
#define IS_NUMBER(value)    (((value) & QNAN) != QNAN)
#define AS_BINARY(value)	(value)

#define NIL_VAL         ((Value)(uint64_t)(QNAN | TAG_NIL))
#define IS_NIL(value)   ((value) == NIL_VAL)
#define NOT_NIL(value)  ((value) != NIL_VAL)

#define BOOL_VAL(b)     ((b) ? TRUE_VAL : FALSE_VAL)
#define AS_BOOL(value)  ((value) == TRUE_VAL)
#define IS_BOOL(value)  (((value) | 1) == TRUE_VAL)

#define OBJ_VAL(obj)	(Value)(SIGN_BIT | QNAN | (uint64_t)(uintptr_t)(obj))
#define AS_OBJ(value)	((Obj*)(uintptr_t)((value) & ~(SIGN_BIT | QNAN)))
#define IS_OBJ(value)	(((value) & (QNAN | SIGN_BIT)) == (QNAN | SIGN_BIT))

#else

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
} Value;

#define NUMBER_VAL(value)	((Value){VAL_NUMBER, {.number = value}})
#define AS_NUMBER(value)	((value).as.number)
#define IS_NUMBER(value)	((value).type == VAL_NUMBER)
#define AS_BINARY(value)	((value).as.binary)

#define NIL_VAL           ((Value){VAL_NIL, {.number = 0}})
#define IS_NIL(value)     ((value).type == VAL_NIL)
#define NOT_NIL(value)	  ((value).type != VAL_NIL)

#define BOOL_VAL(value)   ((Value){VAL_BOOL, {.boolean = value}})
#define AS_BOOL(value)    ((value).as.boolean)
#define IS_BOOL(value)    ((value).type == VAL_BOOL)

#define OBJ_VAL(object)   ((Value){VAL_OBJ, {.obj = (Obj*)object}})
#define AS_OBJ(value)     ((value).as.obj)
#define IS_OBJ(value)     ((value).type == VAL_OBJ)

#endif

#define NAN_VAL				NUMBER_VAL(NAN) //use this to return a NaN
#define IS_NAN(value)		(IS_NUMBER(value) && isnan(AS_NUMBER(value))))
#define IS_INFINITY(value)	(IS_NUMBER(value) && isinf(AS_NUMBER(value))))

typedef struct {
	uint32_t capacity; //limit to 4G
	uint32_t count;    //limit to 4G
	Value* values;
} ValueArray;

#define VALUEHOLES_EMPTY UINT32_MAX

typedef struct {
	uint32_t count;
	uint32_t capacity;
	uint32_t* holes;
} ValueHoles;

bool valuesEqual(Value a, Value b);
void print_adaptive_double(double value);
void printValue(Value value);
void printValue_sys(Value value);

void valueArray_init(ValueArray* array);
void valueArray_write(ValueArray* array, Value value);
void valueArray_writeAt(ValueArray* array, Value value, uint32_t index);
void valueArray_free(ValueArray* array);

void valueHoles_init(ValueHoles* holes);
void valueHoles_free(ValueHoles* holes);
void valueHoles_push(ValueHoles* holes, uint32_t index);
void valueHoles_pop(ValueHoles* holes);
uint32_t valueHoles_get(ValueHoles* holes);

#define GET_VALUE_CONTAINER(obj) ((Value*)((char*)(obj) - offsetof(Value, as)))