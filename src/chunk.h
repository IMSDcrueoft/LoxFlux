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
	OP_POP,				// pop stack
	OP_POP_N,			// pop multiple stack
	OP_BITWISE,			//& | ~ ^ << >> >>>
	OP_CALL,			// callFn
	OP_INVOKE,			// call with xxx.()
	OP_SUPER_INVOKE,	// call with super.()
	OP_RETURN,          // ret

	OP_GET_PROPERTY,	// modify property
	OP_SET_PROPERTY,
	OP_SET_INDEX,		// for array
	OP_GET_INDEX,
	OP_GET_SUPER,		//get super
	OP_GET_GLOBAL,
	OP_SET_GLOBAL,
	OP_DEFINE_GLOBAL,	//define global
	OP_SET_SUBSCRIPT,	// set subscript
	OP_GET_SUBSCRIPT,	// get subscript

	OP_CLOSURE,			// getFn
	OP_GET_UPVALUE,		//up value
	OP_SET_UPVALUE,
	OP_CLOSE_UPVALUE,   // close upvalue
	OP_NEW_ARRAY,		// array literal
	OP_NEW_OBJECT,		// object literal
	OP_NEW_PROPERTY,	// set property but no pop

	//slow func
	OP_INSTANCE_OF,		// instance
	OP_TYPE_OF,			// typeof
	OP_CLASS,			// create class
	OP_INHERIT,			// super class
	OP_METHOD,			// make class func

	OP_MODULE_BUILTIN,	// load builtin module

	OP_PRINT,			// print string or value
	OP_THROW,			// throw
	OP_IMPORT,			// import module
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

//recommand ops when compiling
typedef struct {
	uint32_t count;    //limit to 4G
	uint32_t capacity; //limit to 4G

	uint8_t* code;
} OPStack;

void chuck_init(Chunk* chunk);
void chunk_write(Chunk* chunk, uint8_t byte, uint32_t line);
void chunk_fallback(Chunk* chunk, uint32_t byteCount);
void chunk_free(Chunk* chunk);

//chech opStack first,than use this to override old codes
#define CHUNK_PEEK(chunk, offset) chunk->code[chunk->count - offset - 1]

//free the error complied code (not used)
void chunk_free_errorCode(Chunk* chunk, uint32_t beginError);

void opStack_init(OPStack* stack);
void opStack_push(OPStack* stack, uint8_t byte);
//we don't know the offset of the code,so we use this to get the opType
uint8_t opStack_peek(OPStack* stack, uint8_t offset);
void opStack_fallback(OPStack* stack, uint32_t byteCount);
void opStack_clear(OPStack* stack);
void opStack_free(OPStack* stack);