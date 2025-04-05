/*
 * MIT License
 * Copyright (c) 2025 IM&SD (https://github.com/IMSDcrueoft)
 * See LICENSE file in the root directory for full license text.
*/
#pragma once
#include "vm.h"
#include "scanner.h"
#include "object.h"

typedef struct {
	Token current;
	Token previous;

	bool hadError;
	bool panicMode;
} Parser;

//must be ordered
typedef enum {
	PREC_NONE,
	PREC_ASSIGNMENT,  // =  []=
	PREC_OR,          // or
	PREC_AND,         // and
	PREC_EQUALITY,    // == !=
	PREC_COMPARISON,  // < > <= >=
	PREC_TERM,        // + -
	PREC_FACTOR,      // * / %
	PREC_UNARY,       // ! -
	PREC_CALL,        // . () []
	PREC_OPERATE,
	PREC_PRIMARY	  // @
} Precedence;

typedef void (*ParseFn)(bool canAssign);

typedef struct {
	ParseFn prefix;
	ParseFn infix;
	Precedence precedence;
} ParseRule;

typedef struct {
	Token name;
	int32_t depth;
	bool isCaptured; //captured by upvalue
	bool isConst; //is a const
} Local;

typedef struct LoopContext{
	int32_t start;
	uint32_t enterParamCount;
	uint16_t breakJumpCount;
	uint16_t breakJumpCapacity;
	int32_t* breakJumps;
	struct LoopContext* enclosing;
} LoopContext;

typedef enum {
	TYPE_FUNCTION,
	TYPE_SCRIPT
} FunctionType;

typedef struct {
	bool isLocal;
	uint16_t index;
} Upvalue;

typedef struct Compiler {
	struct Compiler* enclosing;

	ObjFunction* function;
	FunctionType type;
	uint32_t nestingDepth;

	uint16_t localCount;
	uint16_t scopeDepth;
	uint32_t capacity;
	Local* locals;

	Upvalue upvalues[UINT8_COUNT];

	LoopContext* currentLoop;
} Compiler;

ObjFunction* compile(C_STR source);
void markCompilerRoots();