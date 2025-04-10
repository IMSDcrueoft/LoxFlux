/*
 * MIT License
 * Copyright (c) 2025 IM&SD (https://github.com/IMSDcrueoft)
 * See LICENSE file in the root directory for full license text.
*/
#pragma once
#include "chunk.h"
#include "compiler.h"
#include "table.h"
#include "object.h"

//the depth of call frames
#define FRAMES_MAX 1024
//customed vm stack begin size,the real limit is localLimit(1024) * frameLimit(1024)
#define STACK_INITIAL_SIZE (16 * UINT10_COUNT)

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

	//pool
	Table strings;
	//pool
	NumberTable numbers;

	//global hash table
	Table globals;

	//the root for dynamic objects
	Obj* objects;
	//the root for static objects
	Obj* objects_no_gc;

	//upvalues
	ObjUpvalue* openUpvalues;

	//gc gray objects
	uint64_t grayCount;
	uint64_t grayCapacity;
	Obj** grayStack;

	//Excludes space used by stacks/constants/compilations
	uint64_t bytesAllocated;
	uint64_t nextGC;

	//frames
	uint64_t frameCount;
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

//get the size of constants (including the holes)
uint32_t getConstantSize();
//add a constant and get the index of it,i will handle it later
uint32_t addConstant(Value value);

InterpretResult interpret(C_STR source);
InterpretResult interpret_repl(C_STR source);

//for vm
void defineNative(C_STR name, NativeFn function);