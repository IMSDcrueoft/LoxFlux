/*
 * MIT License
 * Copyright (c) 2025 IM&SD (https://github.com/IMSDcrueoft)
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

typedef struct {
	uint32_t capacity; //limit to 4GB
	uint32_t count;    //limit to 4GB
	Value* values;
} ValueArray;

#define VALUEHOLES_EMPTY UINT32_MAX

typedef struct {
	uint32_t count;
	uint32_t capacity;
	uint32_t* holes;
} ValueHoles;

#define IS_BOOL(value)    ((value).type == VAL_BOOL)
#define IS_NIL(value)     ((value).type == VAL_NIL)
#define IS_NUMBER(value)  ((value).type == VAL_NUMBER)
#define IS_OBJ(value)     ((value).type == VAL_OBJ)

#define AS_BOOL(value)    ((value).as.boolean)
#define AS_NUMBER(value)  ((value).as.number)
#define AS_OBJ(value)     ((value).as.obj)
#define AS_BINARY(value)  ((value).as.binary)

#define BOOL_VAL(value)   ((Value){VAL_BOOL, {.boolean = value}})
#define NIL_VAL           ((Value){VAL_NIL, {.number = 0}})
#define NUMBER_VAL(value) ((Value){VAL_NUMBER, {.number = value}})
#define OBJ_VAL(object)   ((Value){VAL_OBJ, {.obj = (Obj*)object}})

#define IS_NAN(value)		(IS_NUMBER(value) && isnan(AS_NUMBER(value)))
#define IS_INFINITY(value)	  (IS_NUMBER(value) && isinf(AS_NUMBER(value)))

bool valuesEqual(Value a, Value b);

void printValue(Value value);

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