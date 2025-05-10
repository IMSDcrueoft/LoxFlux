/*
 * MIT License
 * Copyright (c) 2025 IMSDcrueoft (https://github.com/IMSDcrueoft)
 * See LICENSE file in the root directory for full license text.
*/
#pragma once
#include "chunk.h"
#include "compiler.h"
#include "table.h"
#include "object.h"
#include "nativeBuiltin.h"

//the depth of call frames
#define FRAMES_MAX 1024
//customed vm stack begin size
#define STACK_INITIAL_SIZE 4096

typedef struct {
	ObjClosure* closure;
	uint8_t* ip;
	Value* slots; //first avilable slot
} CallFrame;

typedef struct {
	//a cache
	Value* stack;
	Value* stackTop;
	//the edge of stack
	Value* stackBoundary;

	// deduplicated global constant table
	ValueArray constants;
	// In order to repurpose the voids caused by GC
	// create a constant void table to record and reuse
	ValueHoles constantHoles;

	//pool
	StringTable strings;
	//pool
	NumberTable numbers;

	//upvalues
	ObjUpvalue* openUpvalues;

	//global hash table
	ObjInstance globals;
	ObjInstance builtins[BUILTIN_MODULE_COUNT];

	//the root for dynamic objects
	Obj* objects;
	//the root for static objects
	Obj* objects_no_gc;

	//gc gray objects
	uint64_t grayCount;
	uint64_t grayCapacity;
	Obj** grayStack;

	//Excludes space used by stacks/constants/compilations
	uint64_t bytesAllocated_no_gc;
	uint64_t bytesAllocated;

	//Flip tagging, is more suitable for concurrent tagging
	uint8_t gcMark;
	uint64_t beginGC;
	uint64_t nextGC;

	//ip for debug error
	uint8_t** ip_error;

	//literal object
	ObjClass emptyClass;

	ObjString* initString;
	ObjString* typeStrings[TYPE_STRING_COUNT];

	//id for compiled functions
	uint32_t functionID;

	//frames
	uint32_t frameCount;
	CallFrame frames[FRAMES_MAX];
} VM;

typedef enum {
	INTERPRET_OK,
	INTERPRET_COMPILE_ERROR,
	INTERPRET_RUNTIME_ERROR
} InterpretResult;

//the global shared vm extern to other file
extern VM vm;

void vm_init();
void vm_free();

void stack_push(Value value);
Value stack_pop();
void stack_replace(Value val);

//get the size of constants (including the holes)
uint32_t getConstantSize();
//add a constant and get the index of it,i will handle it later
uint32_t addConstant(Value value);

InterpretResult interpret(C_STR source);
InterpretResult interpret_repl(C_STR source);

//for builtin
void defineNative_math(C_STR name, NativeFn function);
void defineNative_array(C_STR name, NativeFn function);
void defineNative_object(C_STR name, NativeFn function);
void defineNative_string(C_STR name, NativeFn function);
void defineNative_time(C_STR name, NativeFn function);
void defineNative_ctor(C_STR name, NativeFn function);
void defineNative_system(C_STR name, NativeFn function);
//for global
void defineNative_global(C_STR name, NativeFn function);