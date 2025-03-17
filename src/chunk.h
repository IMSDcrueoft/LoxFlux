/*
 * MIT License
 * Copyright (c) 2025 IM&SD (https://github.com/IMSDcrueoft)
 * See LICENSE file in the root directory for full license text.
*/
#pragma once
#include "common.h"
#include "value.h"
#include "memory.h"
#include "lineArray.h"

typedef enum {
	OP_CONSTANT,        // 1 + 2 byte
	OP_CONSTANT_LONG,   // 1 + 3 byte

	OP_NIL,
	OP_TRUE,
	OP_FALSE,

	OP_DEFINE_GLOBAL,		//define global
	OP_DEFINE_GLOBAL_LONG,	//define global
	OP_GET_GLOBAL,
	OP_GET_GLOBAL_LONG,
	OP_SET_GLOBAL,
	OP_SET_GLOBAL_LONG,

	OP_EQUAL,			//==
	OP_GREATER,			//>
	OP_LESS,			//<
	OP_NOT_EQUAL,		//!=
	OP_LESS_EQUAL,		//<=
	OP_GREATER_EQUAL,	//>=
	OP_ADD,				// +
	OP_SUBTRACT,		// -
	OP_MULTIPLY,		// *
	OP_DIVIDE,			// /
	OP_MODULUS,			// %
	OP_NOT,				// !
	OP_NEGATE,			// -v
	OP_PRINT,			// print string or value
	OP_POP,				// pop stack
	OP_POP_N,			// pop multiple stack
	OP_JUMP,			// no condition jump
	OP_JUMP_IF_FALSE,   // condition jump if false
	OP_JUMP_IF_FALSE_POP,
	OP_JUMP_IF_TRUE,    // condition jump if true
	OP_LOOP,			// loop
	OP_CALL,			// callFn

	OP_CLOSURE,			// getFn
	OP_CLOSURE_LONG,	// getFn

	OP_RETURN,          // ret
	OP_THROW,			// throw

	//load local
	OP_GET_LOCAL,
	OP_SET_LOCAL,

	//load builtin module
	OP_MODULE_BUILTIN,
	//allow user to get the global object
	OP_MODULE_GLOBAL,

	//for debugger
	OP_DEBUGGER,
} OpCode;

typedef struct {
	uint32_t count;    //limit to 4GB
	uint32_t capacity; //limit to 4GB

	uint8_t* code;
	LineArray lines; //codes are nearby,so it's based on offset
} Chunk;

void chuck_init(Chunk* chunk);
void chunk_write(Chunk* chunk, uint8_t byte, uint32_t line);
void chunk_free(Chunk* chunk);

//free the error complied code
void chunk_free_errorCode(Chunk* chunk, uint32_t beginError);