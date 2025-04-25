/*
 * MIT License
 * Copyright (c) 2025 IMSDcrueoft (https://github.com/IMSDcrueoft)
 * See LICENSE file in the root directory for full license text.
*/
#pragma once
#include "vm.h"
#include "scanner.h"
#include "object.h"

#define LOCAL_INIT 64
//local var
#define LOCAL_MAX 1024
//array literal
#define ARRAY_MAX 1024
//object literal
#define OBJECT_MAX_NESTING 12
//function nesting
#define FUNCTION_MAX_NESTING 8

typedef struct {
	Token current;
	Token previous;

	bool hadError;
	bool panicMode;
} Parser;

//must be ordered
typedef enum {
	PREC_NONE,
	PREC_ASSIGNMENT,  // =
	PREC_OR,          // or
	PREC_AND,         // and
	PREC_BITWISE,	  // |, ^, &, <<, >>, >>>
	PREC_EQUALITY,    // == !=
	PREC_INSTANCEOF,  // instanceOf
	PREC_COMPARISON,  // <, >, <=, >=
	PREC_TERM,        // +, -
	PREC_FACTOR,      // *, /, %
	PREC_UNARY,       // !, -, ~, typeof
	PREC_CALL,        // ., (), []
	PREC_OPERATE,
	PREC_PRIMARY
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
	TYPE_LAMBDA,
	TYPE_METHOD, //class method
	TYPE_INITIALIZER, //class init
	TYPE_SCRIPT
} FunctionType;

typedef struct {
	uint16_t index;
	bool isLocal;
} Upvalue;

typedef struct Compiler {
	struct Compiler* enclosing;

	ObjFunction* function;
	FunctionType type;
	uint16_t nestingDepth;
	uint16_t objectNestingDepth;

	uint16_t localCount;
	uint16_t scopeDepth;
	uint32_t localCapacity;
	Local* locals;

	LoopContext* currentLoop;
	Upvalue upvalues[UINT8_COUNT];
} Compiler;

typedef struct ClassCompiler {
	struct ClassCompiler* enclosing;
} ClassCompiler;

ObjFunction* compile(C_STR source);
void markCompilerRoots();