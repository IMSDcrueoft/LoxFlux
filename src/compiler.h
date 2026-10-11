/*
 * MIT License
 * Copyright (c) 2025-2026 IMSDcrueoft (https://github.com/IMSDcrueoft)
 * See LICENSE file in the root directory for full license text.
*/
#pragma once
#include "vm.h"
#include "scanner.h"
#include "object.h"

//do optimize
#define COMPILATION_TIME_OPTIMIZATION 1

#define LOCAL_INIT 64
//local var
#define LOCAL_MAX 1024
//array literal
#define ARRAY_MAX 1024
//object literal
#define OBJECT_MAX_NESTING 12
//function nesting
#define FUNCTION_MAX_NESTING 8
//max parser recursion depth (expressions/statements),guard against C stack overflow
#define MAX_PARSE_DEPTH 512

typedef struct {
	Token current;
	Token previous;

	bool hadError;
	bool panicMode;

	uint32_t parseDepth;
} Parser;

//must be ordered
typedef enum {
	PREC_NONE,
	PREC_ASSIGNMENT,  // =
	PREC_OR,          // or
	PREC_AND,         // and
	PREC_BITWISE,	  // |, ^, &, <<, >>, >>>
	PREC_EQUALITY,    // == !=
	PREC_INSTANCEOF,  // instanceof
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

typedef struct LoopContext {
	int32_t start;
	uint32_t enterParamCount;
	uint16_t breakJumpCount;
	uint16_t breakJumpCapacity;
	//do-while: 'continue' must target the condition check, which sits AFTER
	//the body, so the forward jumps are collected here and patched once the
	//condition position is known; while/for loops keep it NULL and jump back
	//to loop.start directly
	uint16_t continueJumpCount;
	uint16_t continueJumpCapacity;
	bool isDoWhile;
	int32_t* breakJumps;
	int32_t* continueJumps;
	struct LoopContext* enclosing;
} LoopContext;

typedef enum {
	TYPE_FUNCTION,
	TYPE_METHOD, //class method
	TYPE_INITIALIZER, //class init
	TYPE_LAMBDA,
	TYPE_MODULE, // module script
	TYPE_SCRIPT
} FunctionType;

typedef struct {
	uint16_t index;
	bool isLocal;
} Upvalue;

typedef struct Compiler {
	struct Compiler* enclosing;

	ObjFunction* function;
	OPStack stack; //the command stack for optimizer
	NumberTable numbers; //per-function number dedup pool, indexes into function's own constants
	ValueArray constants;//pre-function constants, exits when function is compiled, then merged into objFunction FAM

	FunctionType type;
	uint16_t nestingDepth;
	uint16_t objectNestingDepth;

	uint16_t localCount;
	uint16_t scopeDepth;
	uint32_t localCapacity;

	//inline cache slots (capped at UINT8_MAX sites, slot operand is u8)
	uint16_t cacheCount;
	bool icWarned; //one-shot exhaustion warning flag (debug builds only)
	uint32_t cacheCapacity; //allocated InlineCacheSlot capacity of function->caches
	Local* locals;

	LoopContext* currentLoop;
	Upvalue upvalues[UINT8_COUNT];
} Compiler;

typedef struct ClassCompiler {
	struct ClassCompiler* enclosing;
	bool hasSuperclass;
} ClassCompiler;

ObjFunction* compile(C_STR source, FunctionType compileType);
void markCompilerRoots();

//runtime --print-bytecode flag: process-level setting (set once by main,
//survives /reset), read by endCompiler to dump the final bytecode to stderr
extern bool vm_printBytecode;

//runtime --no-optimize flag: disables every compile-time optimization pass
//(peephole fusion, constant folding, builtin member folding, branch-jump
//fusion); the emitted bytecode stays fully valid, just unoptimized
extern bool vm_optimizationEnabled;