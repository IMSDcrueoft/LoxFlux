/*
 * MIT License
 * Copyright (c) 2025 IMSDcrueoft (https://github.com/IMSDcrueoft)
 * See LICENSE file in the root directory for full license text.
*/
#pragma once
#include "common.h"
#include "value.h"
#include "memory.h"
#include "lineArray.h"

typedef enum {
	OP_CONSTANT,   // 1 + 3 byte

	//load local
	OP_GET_LOCAL,
	OP_SET_LOCAL,

	OP_ADD,				// +
	OP_SUBTRACT,		// -
	OP_MULTIPLY,		// *
	OP_DIVIDE,			// /
	OP_MODULUS,			// %
	OP_NOT,				// !
	OP_NEGATE,			// -v

	OP_NIL,
	OP_TRUE,
	OP_FALSE,
	OP_EQUAL,			//==
	OP_GREATER,			//>
	OP_LESS,			//<
	OP_NOT_EQUAL,		//!=
	OP_LESS_EQUAL,		//<=
	OP_GREATER_EQUAL,	//>=

	OP_JUMP,			// no condition jump
	OP_LOOP,			// loop
	OP_JUMP_IF_FALSE,   // condition jump if false
	OP_JUMP_IF_FALSE_POP,
	OP_JUMP_IF_TRUE,    // condition jump if true
	OP_JUMP_IF_TRUE_POP,
	OP_POP,				// pop stack
	OP_POP_N,			// pop multiple stack
	OP_BITWISE,			//& | ~ ^ << >> >>>
	OP_CALL,			// callFn
	OP_INVOKE,			// call with xxx.()
	OP_RETURN,          // ret

	OP_SET_SUBSCRIPT,	// set subscript
	OP_GET_SUBSCRIPT,	// get subscript
	OP_GET_PROPERTY,	// modify property
	OP_SET_PROPERTY,
	OP_GET_GLOBAL,
	OP_SET_GLOBAL,
	OP_DEFINE_GLOBAL,	//define global

	OP_CLOSURE,			// getFn
	OP_GET_UPVALUE,		//up value
	OP_SET_UPVALUE,
	OP_CLOSE_UPVALUE,   // close upvalue
	OP_NEW_ARRAY,		// array literal
	OP_NEW_OBJECT,		// object literal
	OP_DUP,				// copy the stackTop

	//slow func
	OP_INSTANCE_OF,		// instance
	OP_TYPE_OF,			// typeof
	OP_CLASS,			// create class
	OP_METHOD,			// make class func
	OP_PRINT,			// print string or value
	OP_THROW,			// throw

	OP_MODULE_BUILTIN,	//load builtin module
	OP_MODULE_GLOBAL,	//allow user to get the global object
} OpCode;

typedef enum {
	BIT_OP_NOT,			//~
	BIT_OP_AND,			//&
	BIT_OP_OR,			//|
	BIT_OP_XOR,			//^
	BIT_OP_SHL,			//<<
	BIT_OP_SHR,			//>>>
	BIT_OP_SAR,			//>>
} BitOpCode;

typedef struct {
	uint32_t count;    //limit to 4G
	uint32_t capacity; //limit to 4G

	uint8_t* code;
	LineArray lines; //codes are nearby,so it's based on offset
} Chunk;

void chuck_init(Chunk* chunk);
void chunk_write(Chunk* chunk, uint8_t byte, uint32_t line);
void chunk_free(Chunk* chunk);

//free the error complied code
void chunk_free_errorCode(Chunk* chunk, uint32_t beginError);