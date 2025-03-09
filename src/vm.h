/*
 * MIT License
 * Copyright (c) 2025 IM&SD (https://github.com/IMSDcrueoft)
 * See LICENSE file in the root directory for full license text.
*/
#pragma once
#include "chunk.h"
#include "compiler.h"
#include "table.h"

//customed vm stack begin size
#define STACK_MAX 256u

typedef struct {
	Chunk* chunk;

	//code pointer
	uint8_t* ip;

	//a cache
	Value* stack;
	Value* stackTop;
	//the edge of stack
	Value* stackBoundary;
	
	//pool
	Table strings;
	//pool
	NumberTable numbers;

	//global hash table
	Table globals;

	//the root
	Obj* objects;
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

InterpretResult interpret(C_STR source);
InterpretResult require(C_STR source, Chunk* chunk);
InterpretResult eval(C_STR source, Chunk* chunk);