/*
 * MIT License
 * Copyright (c) 2025-2026 IMSDcrueoft (https://github.com/IMSDcrueoft)
 * See LICENSE file in the root directory for full license text.
*/
#include "compiler.h"
#include "vm.h"
#include "gc.h"
#include "nativeBuiltin.h"
#include "debug.h"
#include <assert.h>
#if LOG_COMPILE_TIMING
#include "timer.h"
#endif

static void declaration(CompilerContext* cc);
static void expression(CompilerContext* cc);
static void statement(CompilerContext* cc);
static void branchCaseStatement(CompilerContext* cc);
static ParseRule* getRule(TokenType type);
static void namedVariable(CompilerContext* cc, Token name, bool canAssign);
static void variable(CompilerContext* cc, bool canAssign);
static Token syntheticToken(C_STR text);

#if LOXFLUX_COMPILATION_TIME_OPTIMIZATION
static void instructionOptimize(CompilerContext* cc);
#endif

static Chunk* currentChunk(CompilerContext* cc) {
	Compiler* current = cc->currentCompiler;
	return &current->function.chunk;
}

static OPStack* currentOpStack(CompilerContext* cc) {
	Compiler* current = cc->currentCompiler;
	return &current->stack;
}

//for which complex logical/control flows, merge is not used
static void clearOpStack(CompilerContext* cc) {
	opStack_clear(currentOpStack(cc));
}

//emit and check opStack
static void emitOpStack(CompilerContext* cc, uint8_t byte, bool doCheck) {
	opStack_push(currentOpStack(cc), byte);

#if LOXFLUX_COMPILATION_TIME_OPTIMIZATION
	if (!doCheck || !cc->optimizationEnabled) return;

#if DEBUG_PRINT_CODE
	//print before optimize
	disassembleOpStack(currentOpStack(cc));
#endif

	instructionOptimize(cc);
#endif
}

static void errorAt(CompilerContext* cc, Token* token, C_STR message) {
	//check the panicMode
	if (cc->parser.panicMode) return;
	//set the panicMode
	cc->parser.panicMode = true;

	fprintf(stderr, "[line %d] Error", token->line);

	if (token->type == TOKEN_EOF) {
		fprintf(stderr, " at end");
	}
	else if (token->type == TOKEN_ERROR) {
		// Nothing.
	}
	else {
		fprintf(stderr, " at '%.*s'", token->length, token->start);
	}

	fprintf(stderr, ": %s\n", message);
	cc->parser.hadError = true;
}

static void error(CompilerContext* cc, C_STR message) {
	errorAt(cc, &cc->parser.previous, message);
}

static void errorAtCurrent(CompilerContext* cc, C_STR message) {
	errorAt(cc, &cc->parser.current, message);
}

static void advance(CompilerContext* cc) {
	cc->parser.previous = cc->parser.current;

	while (true) {
		cc->parser.current = scanToken(&cc->scanner);
		if (cc->parser.current.type != TOKEN_ERROR) break;

		errorAtCurrent(cc, cc->parser.current.start);
	}
}

static void consume(CompilerContext* cc, TokenType type, C_STR message) {
	if (cc->parser.current.type == type) {
		advance(cc);
		return;
	}

	errorAtCurrent(cc, message);
}

static bool check(CompilerContext* cc, TokenType type) {
	return cc->parser.current.type == type;
}

static bool match(CompilerContext* cc, TokenType type) {
	if (!check(cc, type)) return false;
	advance(cc);
	return true;
}

static void emitByte(CompilerContext* cc, uint8_t byte) {
	chunk_write(currentChunk(cc), byte, cc->parser.previous.line);
}

//dynamic args
static void emitBytes(CompilerContext* cc, uint32_t count, int byte, ...) {
	//write the first
	emitByte(cc, byte);

	// arguments
	va_list arguments;
	va_start(arguments, byte);

	for (uint32_t i = 1; i < count; ++i) {
		byte = va_arg(arguments, int);
		emitByte(cc, byte);
	}

	// clear arguments
	va_end(arguments);
}

static void emitConstantCommond(CompilerContext* cc, OpCode target, uint32_t index) {
	if (index <= UINT24_MAX) {
		emitBytes(cc, 4, target, (uint8_t)index, (uint8_t)(index >> 8), (uint8_t)(index >> 16));
	}
	else {
		error(cc, "Too many constants in chunk.");
	}
}

//16bits index into function's own constants
static void emitShortCommand(CompilerContext* cc, OpCode target, uint32_t index) {
	if (index <= UINT16_MAX) {
		emitBytes(cc, 3, target, (uint8_t)index, (uint8_t)(index >> 8));
	}
	else {
		error(cc, "Too many constants in function.");
	}
}

//allocate one inline cache slot (raw InlineCacheSlot, zero-filled = guaranteed miss before first backfill)
//operand encoding: 0 = sentinel (uncached site, array[0] is the shared sentinel slot), real slot k emitted as k+1
//returns the operand to embed in the instruction
static uint8_t emitCacheSlot(CompilerContext* cc) {
	Compiler* current = cc->currentCompiler;
	if (current->cacheCount + 1 > UINT8_MAX) {//operand values 1..255 are real slots
#if DEBUG_PRINT_CODE
		if (!current->icWarned) {
			current->icWarned = true;
			fprintf(stderr, "[ic] function '%s' exhausted inline-cache slots, remaining sites will not be cached\n",
				current->function.name != NULL ? current->function.name->chars : "<script>");
		}
#endif
		if (current->cacheCapacity == 0) {//sentinel sites still need array[0] to exist
			current->function.caches = ALLOCATE_NO_GC(InlineCacheSlot, 1);
			current->cacheCapacity = 1;
			current->function.caches[0].capacity = 0;
			current->function.caches[0].index = 0;
			current->function.caches[0].extraA = NULL;
			current->function.caches[0].extraB = NULL;
		}
		return 0;//sentinel
	}

	if (current->cacheCapacity < (uint32_t)current->cacheCount + 2) {//real slot k lives at array index k+1
		uint32_t capacity = GROW_CAPACITY(current->cacheCapacity);
		bool firstAlloc = (current->function.caches == NULL);
		current->function.caches = GROW_ARRAY_NO_GC(InlineCacheSlot, current->function.caches, current->cacheCapacity, capacity);
		if (firstAlloc) {//malloc memory is not zeroed: init the shared sentinel slot at index 0
			current->function.caches[0].capacity = 0;
			current->function.caches[0].index = 0;
			current->function.caches[0].extraA = NULL;
			current->function.caches[0].extraB = NULL;
		}
		current->cacheCapacity = capacity;
	}

	uint8_t slot = (uint8_t)(current->cacheCount + 1);//operand = real index + 1
	current->cacheCount++;
	current->function.caches[slot].capacity = 0;
	current->function.caches[slot].index = 0;
	current->function.caches[slot].extraA = NULL;
	current->function.caches[slot].extraB = NULL;
	return slot;
}

static int32_t emitJump(CompilerContext* cc, uint8_t instruction) {
	emitBytes(cc, 3, instruction, 0xff, 0xff);
	clearOpStack(cc);
	return currentChunk(cc)->count - 2;
}

//branch-style conditional jump (if/branch/while/do-while only)
//whole condition is exactly one compile-time constant load:
//  falsy (OP_FALSE/OP_NIL/nil constant)              -> always taken : rollback cond, emit OP_JUMP (no runtime compare)
//  truthy (OP_TRUE/number constant/non-nil constant) -> never taken  : rollback cond, no jump, return -1 (no backpatch)
//truthiness mirrors vm's isTruthy: only nil and false are falsy
//returns patch offset like emitJump, or -1 when nothing to patch
static int32_t emitBranchJump(CompilerContext* cc, uint8_t instruction, int32_t condStart) {
#if LOXFLUX_COMPILATION_TIME_OPTIMIZATION
	if (!cc->optimizationEnabled) return emitJump(cc, instruction);

	if (instruction == OP_JUMP_IF_FALSE_POP) {
		Chunk* chunk = currentChunk(cc);
		uint32_t regionSize = chunk->count - (uint32_t)condStart;
		uint8_t cond = chunk->code[condStart];

		//1 byte literal / 3 bytes number constant / 4 bytes non-number constant
		if ((regionSize == 1 && (cond == OP_TRUE || cond == OP_FALSE || cond == OP_NIL))
			|| (regionSize == 3 && cond == OP_CONST_NUMBER)
			|| (regionSize == 4 && cond == OP_CONSTANT)) {
			bool truthy;

			switch (cond) {
			case OP_FALSE:
			case OP_NIL: {
				truthy = false;
				break;
			}
			case OP_TRUE:
			case OP_CONST_NUMBER:
			case OP_CONSTANT: {
				truthy = true;
				break;
			}
			}

			chunk_fallback(chunk, regionSize);//rollback the condition bytes
			clearOpStack(cc);

			if (!truthy) {
				return emitJump(cc, OP_JUMP);//always taken
			}
			return -1;//never taken
		}

		//try to fuse [operand load][fused compare] + jump into one OP_JIF_*_LL/LC_NUMBER:
		//pattern A: GET_LOCAL a + <CMP>_LOCAL b   (6 bytes; both slots can swap, so > / >= reuse the LESS forms)
		//pattern B: GET_LOCAL a + <CMP>_CONST c   (6 bytes, number constant on the right)
		//pattern C: CONST_NUMBER c + <CMP>_LOCAL a (6 bytes, number constant on the left, flip the direction)
		//anything else falls through to the plain jump
		if (regionSize == 6 && (cond == OP_GET_LOCAL || cond == OP_CONST_NUMBER)) {
			uint32_t indexA = ((uint32_t)chunk->code[condStart + 1]) | ((uint32_t)chunk->code[condStart + 2] << 8);
			uint8_t cmpOp = chunk->code[condStart + 3];
			uint32_t indexB = ((uint32_t)chunk->code[condStart + 4]) | ((uint32_t)chunk->code[condStart + 5] << 8);

			uint8_t fused = INVALID_OP;
			bool swap = false;
			bool isLL = false;

			if (cond == OP_GET_LOCAL) {
				//local on the left
				switch (cmpOp) {
				case OP_LESS_LOCAL:				fused = OP_JIF_LESS_LL; isLL = true; break;
				case OP_LESS_EQUAL_LOCAL:		fused = OP_JIF_LESS_EQUAL_LL; isLL = true; break;
				case OP_GREATER_LOCAL:			fused = OP_JIF_LESS_LL; swap = true; isLL = true; break;
				case OP_GREATER_EQUAL_LOCAL:	fused = OP_JIF_LESS_EQUAL_LL; swap = true; isLL = true; break;
				case OP_EQUAL_LOCAL:			fused = OP_JIF_EQUAL_LL; isLL = true; break;
				case OP_NOT_EQUAL_LOCAL:		fused = OP_JIF_NOT_EQUAL_LL; isLL = true; break;

				case OP_LESS_CONST:				fused = OP_JIF_LESS_LC_NUMBER; break;
				case OP_LESS_EQUAL_CONST:		fused = OP_JIF_LESS_EQUAL_LC_NUMBER; break;
				case OP_GREATER_CONST:			fused = OP_JIF_GREATER_LC_NUMBER; break;
				case OP_GREATER_EQUAL_CONST:	fused = OP_JIF_GREATER_EQUAL_LC_NUMBER; break;
				case OP_EQUAL_CONST_NUMBER:		fused = OP_JIF_EQUAL_LC_NUMBER; break;
				case OP_NOT_EQUAL_CONST_NUMBER:	fused = OP_JIF_NOT_EQUAL_LC_NUMBER; break;
				}
			}
			else {
				//number constant on the left: flip the direction (== / != need no flip)
				switch (cmpOp) {
				case OP_LESS_LOCAL:				fused = OP_JIF_GREATER_LC_NUMBER; break;
				case OP_LESS_EQUAL_LOCAL:		fused = OP_JIF_GREATER_EQUAL_LC_NUMBER; break;
				case OP_GREATER_LOCAL:			fused = OP_JIF_LESS_LC_NUMBER; break;
				case OP_GREATER_EQUAL_LOCAL:	fused = OP_JIF_LESS_EQUAL_LC_NUMBER; break;
				case OP_EQUAL_LOCAL:			fused = OP_JIF_EQUAL_LC_NUMBER; break;
				case OP_NOT_EQUAL_LOCAL:		fused = OP_JIF_NOT_EQUAL_LC_NUMBER; break;
				}
			}

			if (fused != INVALID_OP) {
				chunk_fallback(chunk, regionSize);//rollback the condition bytes

				if (isLL) {
					uint32_t slotA = (swap ? indexB : indexA);
					uint32_t slotB = (swap ? indexA : indexB);
					//[op:8][localA:16][localB:16][offset:16 placeholder]
					emitBytes(cc, 7, fused,
						(uint8_t)slotA, (uint8_t)(slotA >> 8),
						(uint8_t)slotB, (uint8_t)(slotB >> 8),
						0xff, 0xff);
				}
				else {
					//local left: local=indexA,const=indexB; const left: local=indexB,const=indexA
					uint32_t localIndex = (cond == OP_GET_LOCAL) ? indexA : indexB;
					uint32_t constIndex = (cond == OP_GET_LOCAL) ? indexB : indexA;
					//[op:8][local:16][const:16][offset:16 placeholder]
					emitBytes(cc, 7, fused,
						(uint8_t)localIndex, (uint8_t)(localIndex >> 8),
						(uint8_t)constIndex, (uint8_t)(constIndex >> 8),
						0xff, 0xff);
				}
				clearOpStack(cc);
				return currentChunk(cc)->count - 2;
			}
		}

		//fused equality with non-number constants (24bit index into vm.constants):
		//pattern D: GET_LOCAL a + OP_EQUAL_CONST/OP_NOT_EQUAL_CONST c  (7 bytes)
		//pattern E: OP_CONSTANT c + OP_EQUAL_LOCAL/OP_NOT_EQUAL_LOCAL a (7 bytes)
		//note: ordering comparisons against non-number constants have no super
		//instruction, so a 7-byte region starting with OP_CONSTANT is only matched
		//for the equality _LOCAL ops (a 4-byte non-number constant is never treated
		//as a 16bit one)
		if (regionSize == 7 && (cond == OP_GET_LOCAL || cond == OP_CONSTANT)) {
			uint32_t localIndex = 0;
			uint32_t constIndex = 0;
			uint8_t cmpOp = 0;
			uint8_t fused = INVALID_OP;

			if (cond == OP_GET_LOCAL) {
				localIndex = ((uint32_t)chunk->code[condStart + 1]) | ((uint32_t)chunk->code[condStart + 2] << 8);
				cmpOp = chunk->code[condStart + 3];
				constIndex = ((uint32_t)chunk->code[condStart + 4]) | ((uint32_t)chunk->code[condStart + 5] << 8) | ((uint32_t)chunk->code[condStart + 6] << 16);

				if (cmpOp == OP_EQUAL_CONST) fused = OP_JIF_EQUAL_LC;
				else if (cmpOp == OP_NOT_EQUAL_CONST) fused = OP_JIF_NOT_EQUAL_LC;
			}
			else {
				constIndex = ((uint32_t)chunk->code[condStart + 1]) | ((uint32_t)chunk->code[condStart + 2] << 8) | ((uint32_t)chunk->code[condStart + 3] << 16);
				cmpOp = chunk->code[condStart + 4];
				localIndex = ((uint32_t)chunk->code[condStart + 5]) | ((uint32_t)chunk->code[condStart + 6] << 8);

				//const was on the left: == / != commute, no flip
				if (cmpOp == OP_EQUAL_LOCAL) fused = OP_JIF_EQUAL_LC;
				else if (cmpOp == OP_NOT_EQUAL_LOCAL) fused = OP_JIF_NOT_EQUAL_LC;
			}

			if (fused != INVALID_OP) {
				chunk_fallback(chunk, regionSize);//rollback the condition bytes

				//[op:8][local:16][const:24][offset:16 placeholder]
				emitBytes(cc, 8, fused,
					(uint8_t)localIndex, (uint8_t)(localIndex >> 8),
					(uint8_t)constIndex, (uint8_t)(constIndex >> 8), (uint8_t)(constIndex >> 16),
					0xff, 0xff);
				clearOpStack(cc);
				return currentChunk(cc)->count - 2;
			}
		}
	}
#else
	(void)condStart;
#endif
	return emitJump(cc, instruction);
}

// generate loop
static void emitLoop(CompilerContext* cc, int32_t loopStart) {
	emitByte(cc, OP_LOOP);

	int32_t offset = currentChunk(cc)->count - loopStart + 2;
	if (offset > UINT16_MAX) error(cc, "Loop body too large.");

	emitBytes(cc, 2, offset & 0xff, (offset >> 8) & 0xff);
	clearOpStack(cc);
}

static void emitReturn(CompilerContext* cc) {
	Compiler* current = cc->currentCompiler;
	if (current->type == TYPE_INITIALIZER) {
		//16bits index get_local
		emitBytes(cc, 4, OP_GET_LOCAL, 0, 0, OP_RETURN);
	}
	else {
		emitBytes(cc, 2, OP_NIL, OP_RETURN);
	}
	clearOpStack(cc);
}

//string and native constants,24bits index into vm.constants (both deduplicated)
static uint32_t makeConstant(CompilerContext* cc, LfxValue value) {
	if (IS_STRING(value)) {
		//find if string is in constant,because it is in pool
		StringEntry* entry = tableGetStringEntry(&cc->vm->strings, AS_STRING(value));

		if (entry != NULL) {
			if (entry->index == UINT32_MAX) {
				uint32_t index = addConstant(value);

				//never mask the index: a wrapped value would silently reference
				//the wrong constant; report and return the sentinel instead
				if (index > UINT24_MAX) {
					error(cc, "Too many constants in chunk.");
					return UINT32_MAX;
				}

				entry->index = index;//set value and return
				return index;
			}
			return entry->index;
		}
	}
	else if (IS_NATIVE(value)) {
		//natives are immortal (gc-excluded),so pointer bits are a stable dedup key
		BinaryEntry* entry = tableGetBinaryEntry(&cc->vm->nativeConstants, &value);

		if (entry != NULL) {
			if (entry->index == UINT32_MAX) {
				uint32_t index = addConstant(value);

				//never cache a wrapped index: it would silently reference
				//the wrong constant; report and return the sentinel instead
				if (index > UINT24_MAX) {
					error(cc, "Too many constants in chunk.");
					return UINT32_MAX;
				}

				entry->index = index;//set value and return
				return index;
			}
			return entry->index;
		}
	}
	return addConstant(value);
}

//number constants only,16bits index into function's own constants
static uint32_t makeNumberConstant(CompilerContext* cc, LfxValue value) {
	Compiler* current = cc->currentCompiler;
	//deduplicate in per-function pool (compiler-owned: one probe, insert on miss)
	BinaryEntry* entry = tableGetOrInsertBinaryEntry(cc->vm, &current->numbers, &value);

	if (entry->index == UINT32_MAX) {
		//OP_CONST_NUMBER encodes a 16bits index: reject before the entry
		//gets a wrapped/unrepresentable index stored in the dedup pool
		if (current->constants.count > UINT16_MAX) {
			error(cc, "Too many constants in function.");
			return UINT32_MAX;
		}

		ValueArray* constants = &current->constants;
		entry->index = valueArray_write(constants, value);//set value and return
	}

	return entry->index;
}

static void emitConstant(CompilerContext* cc, LfxValue value) {
	emitConstantCommond(cc, OP_CONSTANT, makeConstant(cc, value));
	emitOpStack(cc, OP_CONSTANT, false);
}

static void emitNumberConstant(CompilerContext* cc, LfxValue value) {
	emitShortCommand(cc, OP_CONST_NUMBER, makeNumberConstant(cc, value));
	emitOpStack(cc, OP_CONST_NUMBER, false);
}

//fold `OP_MODULE_BUILTIN m` + `OP_GET_PROPERTY name slot` into a constant load
//valid because builtin module fields are frozen once vm_init populates them (they never change at runtime):
//numbers fold to the function's number pool (short index), native members to the shared constant pool;
//other member kinds keep the property-access site (only known-safe kinds are folded)
//the pre-allocated IC slot is rolled back:the folded site no longer owns one
static bool foldBuiltinMember(CompilerContext* cc) {
#if LOXFLUX_COMPILATION_TIME_OPTIMIZATION
	if (!cc->optimizationEnabled) return false;

	Chunk* chunk = currentChunk(cc);

	//check opStack first, then use it to override the old codes
#define CHUNK_PEEK(offset) chunk->code[chunk->count - offset - 1]
#define READ_24BITS_INDEX(offset)	\
	(((uint32_t)chunk->code[chunk->count - (offset) - 1] << 16) +	\
	 ((uint32_t)chunk->code[chunk->count - (offset) - 2] << 8) +	\
	 (uint32_t)(chunk->code[chunk->count - (offset) - 3]))

	if (chunk->count < 7) return false;
	//tail layout: [BUILTIN u8][module u8][GET_PROPERTY][name u24][slot u8]
	if (CHUNK_PEEK(6) != OP_MODULE_BUILTIN) return false;
	if (CHUNK_PEEK(4) != OP_GET_PROPERTY) return false;

	uint8_t module = CHUNK_PEEK(5);
	uint32_t nameIdx = READ_24BITS_INDEX(1);
	uint8_t slot = CHUNK_PEEK(0);

	LfxValue member;
	if (!tableGet(&cc->vm->builtins[module].fields, AS_STRING(cc->vm->constants.values[nameIdx]), &member)) return false;
	//only fold member kinds that are safe as compile-time constants: numbers (function pool)
	//and native functions (shared pool; static/non-GC objects). anything else keeps the
	//property-access site untouched.
	if (!IS_NUMBER(member) && !IS_NATIVE(member)) return false;

	chunk_fallback(chunk, 7);
	//drop the tracked module push entry folded away with the bytes
	OPStack* opStack = currentOpStack(cc);
	if (opStack_peek(opStack, 0) == OP_MODULE_BUILTIN) {
		opStack_fallback(opStack, 1);
	}
	else {
		//compiler bug: pattern exists but the push was never tracked
		fprintf(stderr, "[compiler bug] foldBuiltinMember: pattern matched but opStack tail is not OP_MODULE_BUILTIN\n");
	}
	// revert the allocated IC slot: the folded site no longer owns one
	if (slot != 0) {
		Compiler* current = cc->currentCompiler;
		current->cacheCount--;
	}

	//numbers into the function's own pool, native members into the shared pool
	if (IS_NUMBER(member)) {
		emitNumberConstant(cc, NUMBER_VAL(AS_NUMBER(member)));
	}
	else {// is native
		emitConstant(cc, member);
	}
	return true;

#undef CHUNK_PEEK
#undef READ_24BITS_INDEX
#else
	return false;
#endif
}

static void patchJump(CompilerContext* cc, int32_t offset) {
	// -2 to adjust for the bytecode for the jump offset itself.
	int32_t jump = currentChunk(cc)->count - offset - 2;

	if (jump > UINT16_MAX) {
		error(cc, "Too much code to jump over.");
	}

	//write low and high
	currentChunk(cc)->code[offset] = jump & 0xff;
	currentChunk(cc)->code[offset + 1] = (jump >> 8) & 0xff;
}

//patch a previously emitted jump to an arbitrary (forward) target position
//used by do-while 'continue': the jump sites are known before the condition
//is compiled, so they are backpatched to condStart once it exists
//'jump' is the chunk position of the 2 operand bytes (as returned by emitJump);
//the vm's READ_SHORT consumes those 2 bytes first, so at runtime ip already
//sits at jump+2 before doing ip += offset. offset must therefore be measured
//from jump+2: -2 skips the operand bytes themselves, -jump converts the
//absolute target position into that runtime-relative distance
static void patchJumpTo(CompilerContext* cc, int32_t jump, int32_t target) {
	int32_t offset = target - jump - 2;

	if (offset > UINT16_MAX) {
		error(cc, "Too much code to jump over.");
	}

	currentChunk(cc)->code[jump] = offset & 0xff;
	currentChunk(cc)->code[jump + 1] = (offset >> 8) & 0xff;
}

static void initCompiler(CompilerContext* cc, Compiler* compiler, FunctionType type) {
	compiler->enclosing = cc->currentCompiler;
	cc->currentCompiler = compiler;//must set now

	//init
	compiler->currentLoop = NULL;

	compiler->type = type;

	compiler->localCount = 0;
	compiler->scopeDepth = 0;
	compiler->locals = ALLOCATE_NO_GC(Local, LOCAL_INIT);
	compiler->localCapacity = LOCAL_INIT;

	//embedded compile-time function state: no heap allocation, no list linkage
	compiler->function.id = cc->vm->functionID++;
	compiler->function.arity = 0;
	compiler->function.upvalueCount = 0;
	compiler->function.name = NULL;
	chunk_init(&compiler->function.chunk);
	compiler->function.caches = NULL;
	compiler->function.cacheCount = 0;
	compiler->function.constantsCount = 0;//scratch state, constants live in the compiler until sealed
	compiler->objectNestingDepth = 0;

	//inline cache slots
	compiler->cacheCount = 0;
	compiler->icWarned = false;
	compiler->cacheCapacity = 0;

	opStack_init(&compiler->stack);
	binaryTable_init(&compiler->numbers);
	// init compile constants, after compile we copy it to FAM objFunction
	valueArray_init(&compiler->constants);

	//it's a function
	switch (type) {
	case TYPE_FUNCTION:
	case TYPE_METHOD:
	case TYPE_INITIALIZER:
		compiler->function.name = copyString(cc->vm, cc->parser.previous.start, cc->parser.previous.length, false);
		break;
	case TYPE_LAMBDA:
		compiler->function.name = copyString(cc->vm, "", 0, false);
		break;
	default:
		break;
	}

	Local* local = &compiler->locals[compiler->localCount++];
	local->depth = 0;
	local->isCaptured = false;
	local->isConst = false;
	local->isHidden = false;

	if (type != TYPE_FUNCTION && type != TYPE_LAMBDA) {
		local->name.start = "this";
		local->name.length = 4;
	}
	else {
		local->name.start = "";
		local->name.length = 0;
	}

	// the module don't work in global scope
	if (compiler->enclosing == NULL) {
		compiler->nestingDepth = 0;
	}
	else {
		compiler->nestingDepth = compiler->enclosing->nestingDepth + 1;
		if (compiler->nestingDepth == FUNCTION_MAX_NESTING) {
			error(cc, "Too many nested functions.");
		}
	}

	// run in block
	if (type == TYPE_MODULE) {
		compiler->scopeDepth = 1;
	}
}

static void freeLocals(Compiler* compiler) {
	FREE_ARRAY_NO_GC(Local, compiler->locals, compiler->localCapacity);
	compiler->locals = NULL;
	compiler->localCapacity = 0;
}

static ObjFunction* endCompiler(CompilerContext* cc) {
	Compiler* current = cc->currentCompiler;
	emitReturn(cc);
	opStack_free(currentOpStack(cc));
	binaryTable_free(cc->vm, &current->numbers);//free every compiler's number pool (nested compilers were leaking)

	//shrink inline caches to exactly cacheCount+1 entries (index 0 = shared sentinel slot)
	//small functions/lambdas keep no doubling slack; functions without ic sites keep NULL
	if (current->cacheCapacity > (uint32_t)current->cacheCount + 1) {
		current->function.caches = GROW_ARRAY_NO_GC(InlineCacheSlot, current->function.caches, current->cacheCapacity, (uint32_t)current->cacheCount + 1);
		current->cacheCapacity = (uint32_t)current->cacheCount + 1;
	}

	//materialize the final FAM function from the embedded compile-time state
	//must run BEFORE IC registration: the registry and OP_CLOSURE must hold the final object pointer
	ObjFunction* function = newFunction(cc->vm, current);

	//detach the moved buffers from the embedded state: the fresh object owns them now
	chunk_init(&current->function.chunk);
	current->function.caches = NULL;
	current->function.constantsCount = 0;

	valueArray_free(&current->constants);

	//register for gc: invoke cache slots hold ObjClass*/ObjClosure* that must stay marked
	if (current->cacheCapacity > 0) {
		vm_registerIcFunction(cc->vm, function);
	}

#if DEBUG_PRINT_CODE
	if (!cc->parser.hadError) {
		disassembleChunk(function, (function->name != NULL)
			? ((function->name->length != 0) ? function->name->chars : "<lambda>") : "<script>", function->id);
	}
#else
	//runtime --print-bytecode (release builds): dump goes to stderr so the
	//script's own stdout stays clean and pipeable
	if (cc->printBytecode && !cc->parser.hadError) {
		debug_setDisasmStream(stderr);
		disassembleChunk(function, (function->name != NULL)
			? ((function->name->length != 0) ? function->name->chars : "<lambda>") : "<script>", function->id);
		debug_setDisasmStream(NULL);
	}
#endif
	cc->currentCompiler = current->enclosing;
	return function;
}

static void beginScope(CompilerContext* cc) {
	Compiler* current = cc->currentCompiler;
	current->scopeDepth++;
}

static void emitPopCount(CompilerContext* cc, uint16_t popCount) {
	if (popCount == 0) return;
	if (popCount > 1) { // 16-bit index
		emitBytes(cc, 3, OP_POP_N, (uint8_t)popCount, (uint8_t)(popCount >> 8));
	}
	else {
		emitByte(cc, OP_POP);
		emitOpStack(cc, OP_POP, true);
	}
	clearOpStack(cc);
}

static void endScope(CompilerContext* cc) {
	Compiler* current = cc->currentCompiler;
	current->scopeDepth--;

	uint32_t popCount = 0;

	while ((current->localCount > 0) && (current->locals[current->localCount - 1].depth > current->scopeDepth)) {
		if (current->locals[current->localCount - 1].isCaptured) {
			if (popCount > 0) {
				emitPopCount(cc, popCount);
				popCount = 0;
			}

			emitByte(cc, OP_CLOSE_UPVALUE);
			clearOpStack(cc);
		}
		else {
			++popCount;
		}
		current->localCount--;
	}

	if (popCount > 0) {
		emitPopCount(cc, popCount);
	}
}

//emit the same close/pop sequence endScope would, from the innermost local
//down to `target`, without releasing the local records: the code after
//break/continue still needs them for name resolution, and the block's own
//endScope cleanup bytes become dead code after the jump (harmless)
static void closeLoopLocals(CompilerContext* cc, uint32_t target) {
	Compiler* current = cc->currentCompiler;
	uint32_t popCount = 0;
	for (int32_t i = current->localCount - 1; i >= (int32_t)target; i--) {
		if (current->locals[i].isCaptured) {
			if (popCount > 0) {
				emitPopCount(cc, popCount);
				popCount = 0;
			}
			emitByte(cc, OP_CLOSE_UPVALUE);
			clearOpStack(cc);
		}
		else {
			++popCount;
		}
	}

	if (popCount > 0) {
		emitPopCount(cc, popCount);
	}
}

static void parsePrecedenceInner(CompilerContext* cc, Precedence precedence) {
	advance(cc);

	ParseFn prefixRule = getRule(cc->parser.previous.type)->prefix;

	if (prefixRule == NULL) {
		error(cc, "Expect expression.");
		return;
	}

	bool canAssign = precedence <= PREC_ASSIGNMENT;
	prefixRule(cc, canAssign);

	while (precedence <= getRule(cc->parser.current.type)->precedence) {
		advance(cc);
		ParseFn infixRule = getRule(cc->parser.previous.type)->infix;

		if (infixRule == NULL) {
			error(cc, "Syntax error, no infix syntax at current location.");
			break;
		}

		infixRule(cc, canAssign);
	}

	if (canAssign && match(cc, TOKEN_EQUAL)) {
		error(cc, "Invalid assignment target.");
	}
}

//recursion guard:parens/unary/and-or/subscript chains recurse through here,
//nesting beyond the cap would overflow the C stack
static void parsePrecedence(CompilerContext* cc, Precedence precedence) {
	if (cc->parser.parseDepth >= MAX_PARSE_DEPTH) {
		error(cc, "Code nesting is too deep.");
		return;
	}
	cc->parser.parseDepth++;
	parsePrecedenceInner(cc, precedence);
	cc->parser.parseDepth--;
}

static uint32_t identifierConstant(CompilerContext* cc, Token* name) {
	return makeConstant(cc, OBJ_VAL(copyString(cc->vm, name->start, name->length, false)));
}

static void addLocal(CompilerContext* cc, Token name, bool isHidden) {
	Compiler* current = cc->currentCompiler;
	if (current->localCount == LOCAL_MAX) {
		error(cc, "Too many nested local variables in scope.");
		return;
	}

	if (current->localCount == current->localCapacity) {
		//grow
		uint32_t oldCapacity = current->localCapacity;
		current->localCapacity = GROW_CAPACITY(oldCapacity);
		current->locals = GROW_ARRAY_NO_GC(Local, current->locals, oldCapacity, current->localCapacity);
	}

	Local* local = &current->locals[current->localCount++];
	local->name = name;
	local->depth = -1;//guard: a local is not readable in its own initializer (var a = a;)
	local->isCaptured = false;
	local->isConst = false;
	local->isHidden = isHidden;
}

static bool identifiersEqual(Token* a, Token* b) {
	if (a->length != b->length) return false;
	return memcmp(a->start, b->start, a->length) == 0;
}

static void declareVariableAt(CompilerContext* cc, Token* name) {
	Compiler* current = cc->currentCompiler;
	//it is global
	if (current->scopeDepth == 0) return;

	for (int32_t i = current->localCount - 1; i >= 0; i--) {
		Local* local = &current->locals[i];

		if (local->depth != -1 && local->depth < current->scopeDepth) {
			break;
		}

		if (local->isHidden) continue;//hidden locals are unresolvable, never a duplicate source

		if (identifiersEqual(name, &local->name)) {
			error(cc, "Already a variable with this name in this scope.");
		}
	}

	addLocal(cc, *name, false);
}

static void declareVariable(CompilerContext* cc) {
	declareVariableAt(cc, &cc->parser.previous);
}

typedef struct {
	int32_t arg;
	bool isConst;
} LocalInfo;

static LocalInfo resolveLocal(CompilerContext* cc, Compiler* compiler, Token* name) {
	for (int32_t i = compiler->localCount - 1; i >= 0; i--) {
		Local* local = &compiler->locals[i];
		if (local->isHidden) continue;//compiler-owned slot, unreachable by any user token
		if (identifiersEqual(name, &local->name)) {
			if (local->depth == -1) {
				error(cc, "Can't read local variable in its own initializer.");
			}

			return (LocalInfo) { .arg = i, .isConst = local->isConst };
		}
	}

	//it is a global declared var
	return (LocalInfo) { .arg = -1, .isConst = false };
}

static uint32_t addUpvalue(CompilerContext* cc, Compiler* compiler, int32_t index, bool isLocal) {
	uint32_t upvalueCount = compiler->function.upvalueCount;

	//deduplicate
	for (uint32_t i = 0; i < upvalueCount; i++) {
		Upvalue* upvalue = &compiler->upvalues[i];
		if (upvalue->index == index && upvalue->isLocal == isLocal) {
			return i;
		}
	}

	if (upvalueCount == UINT8_COUNT) {
		error(cc, "Too many closure variables in function.");
		return 0;
	}

	compiler->upvalues[upvalueCount].isLocal = isLocal;
	compiler->upvalues[upvalueCount].index = index;
	return compiler->function.upvalueCount++;
}

static LocalInfo resolveUpvalue(CompilerContext* cc, Compiler* compiler, Token* name) {
	if (compiler->enclosing == NULL) return (LocalInfo) { .arg = -1, .isConst = false };

	//local
	LocalInfo localInfo = resolveLocal(cc, compiler->enclosing, name);
	if (localInfo.arg != -1) {
		compiler->enclosing->locals[localInfo.arg].isCaptured = true;//mark it as captured
		localInfo.arg = addUpvalue(cc, compiler, localInfo.arg, true);
		return localInfo;
	}

	//recursion
	LocalInfo upvalue = resolveUpvalue(cc, compiler->enclosing, name);
	if (upvalue.arg != -1) {
		upvalue.arg = addUpvalue(cc, compiler, upvalue.arg, false);
		return upvalue;
	}

	return (LocalInfo) { .arg = -1, .isConst = false };
}

static uint32_t parseVariable(CompilerContext* cc, C_STR errorMessage) {
	Compiler* current = cc->currentCompiler;
	consume(cc, TOKEN_IDENTIFIER, errorMessage);

	declareVariable(cc);
	//it is a local one
	if (current->scopeDepth > 0) return 0;

	return identifierConstant(cc, &cc->parser.previous);
}

static void markInitialized(CompilerContext* cc, bool isConst) {
	Compiler* current = cc->currentCompiler;
	if (current->scopeDepth == 0) return;
	//only defined local vars could be used
	current->locals[current->localCount - 1].depth = current->scopeDepth;
	current->locals[current->localCount - 1].isConst = isConst;
}

static void defineVariable(CompilerContext* cc, uint32_t global) {
	Compiler* current = cc->currentCompiler;
	//it is a local one
	if (current->scopeDepth > 0) {
		markInitialized(cc, false);
		return;
	}

	emitConstantCommond(cc, OP_DEFINE_GLOBAL, global);
	clearOpStack(cc);
}

static void defineConst(CompilerContext* cc, uint32_t global) {
	Compiler* current = cc->currentCompiler;
	//it is a local one
	if (current->scopeDepth > 0) {
		markInitialized(cc, true);
		return;
	}
	else {
		errorAtCurrent(cc, "Constant can only be defined in the local scope.");
	}
}

static uint8_t argumentList(CompilerContext* cc) {
	uint8_t argCount = 0;
	if (!check(cc, TOKEN_RIGHT_PAREN)) {
		do {
			expression(cc);

			if (argCount == UINT8_MAX) {
				error(cc, "Can't have more than 255 arguments.");
			}

			argCount++;
		} while (match(cc, TOKEN_COMMA));
	}
	consume(cc, TOKEN_RIGHT_PAREN, "Expect ')' after arguments.");
	return argCount;
}

static void and_(CompilerContext* cc, bool canAssign) {
	int32_t endJump = emitJump(cc, OP_JUMP_IF_FALSE);

	emitPopCount(cc, 1);
	parsePrecedence(cc, PREC_AND);

	patchJump(cc, endJump);
}

static void expression(CompilerContext* cc) {
	parsePrecedence(cc, PREC_ASSIGNMENT);
}

static void block(CompilerContext* cc) {
	while (!check(cc, TOKEN_RIGHT_BRACE) && !check(cc, TOKEN_EOF)) {
		declaration(cc);
	}

	consume(cc, TOKEN_RIGHT_BRACE, "Expect '}' after block.");
}

static void function(CompilerContext* cc, FunctionType type) {
	Compiler compiler;
	initCompiler(cc, &compiler, type);
	Compiler* current = cc->currentCompiler;
	beginScope(cc);

	//check '('
	consume(cc, TOKEN_LEFT_PAREN, "Expect '(' before function parameters.");

	if (!check(cc, TOKEN_RIGHT_PAREN)) {
		do {
			current->function.arity++;
			if (current->function.arity > UINT8_MAX) {
				errorAtCurrent(cc, "Can't have more than 255 parameters.");
			}

			uint32_t constant = parseVariable(cc, "Expect parameter name.");
			defineVariable(cc, constant);
		} while (match(cc, TOKEN_COMMA));
	}

	consume(cc, TOKEN_RIGHT_PAREN, "Expect ')' after function parameters.");

	if (!match(cc, TOKEN_RIGHT_ARROW)) {
		consume(cc, TOKEN_LEFT_BRACE, "Expect '{' before function body.");
		block(cc);
	}
	else {
		if (type != TYPE_LAMBDA) {
			//no early return here:endCompiler/freeLocals must run,otherwise
			//'current' keeps pointing at this dead stack frame (use-after-scope)
			errorAtCurrent(cc, "'=>' can only be used after lambda parameters.");
		}

		expression(cc);
		emitByte(cc, OP_RETURN);
		clearOpStack(cc);
	}

	ObjFunction* function = endCompiler(cc);

	//create a closure
	emitConstantCommond(cc, OP_CLOSURE, makeConstant(cc, OBJ_VAL(function)));

	//insert upValue index
	for (uint32_t i = 0; i < function->upvalueCount; i++) {
		emitByte(cc, compiler.upvalues[i].isLocal ? 1 : 0);
		emitBytes(cc, 2, (uint8_t)(compiler.upvalues[i].index), (uint8_t)(compiler.upvalues[i].index >> 8));
	}
	clearOpStack(cc);

	freeLocals(&compiler);
}

static void lambda(CompilerContext* cc, bool canAssign) {
	FunctionType type = TYPE_LAMBDA;
	function(cc, type);
}

static void method(CompilerContext* cc) {
	consume(cc, TOKEN_IDENTIFIER, "Expect method name.");
	uint32_t constant = identifierConstant(cc, &cc->parser.previous);

	FunctionType type = TYPE_METHOD;
	if (cc->parser.previous.length == 4 && memcmp(cc->parser.previous.start, "init", 4) == 0) {
		type = TYPE_INITIALIZER;
	}
	function(cc, type);
	emitConstantCommond(cc, OP_METHOD, constant);
	clearOpStack(cc);
}

static void classDeclaration(CompilerContext* cc) {
	consume(cc, TOKEN_IDENTIFIER, "Expect class name.");
	Token className = cc->parser.previous;

	uint32_t nameConstant = identifierConstant(cc, &cc->parser.previous);
	declareVariable(cc);

	emitConstantCommond(cc, OP_CLASS, nameConstant);
	clearOpStack(cc);
	defineVariable(cc, nameConstant);

	//link the chain
	ClassCompiler classCompiler = { .enclosing = cc->currentClass,.hasSuperclass = false };
	cc->currentClass = &classCompiler;

	//check 'class name < super {}'
	if (match(cc, TOKEN_LESS)) {
		consume(cc, TOKEN_IDENTIFIER, "Expect superclass name.");
		variable(cc, false);

		if (identifiersEqual(&className, &cc->parser.previous)) {
			error(cc, "A class can't inherit from itself.");
		}

		beginScope(cc);
		addLocal(cc, syntheticToken("super"), false);
		defineVariable(cc, 0);

		namedVariable(cc, className, false);
		emitByte(cc, OP_INHERIT);
		clearOpStack(cc);

		//set flag
		classCompiler.hasSuperclass = true;
	}

	//put the class back to stack
	namedVariable(cc, className, false);
	consume(cc, TOKEN_LEFT_BRACE, "Expect '{' before class body.");
	while (!check(cc, TOKEN_RIGHT_BRACE) && !check(cc, TOKEN_EOF)) {
		method(cc);
	}
	consume(cc, TOKEN_RIGHT_BRACE, "Expect '}' after class body.");
	//pop out the class
	emitPopCount(cc, 1);

	//end the scope of super
	if (classCompiler.hasSuperclass) {
		endScope(cc);
	}

	//resume
	cc->currentClass = cc->currentClass->enclosing;
}

static void funDeclaration(CompilerContext* cc) {
	uint32_t arg = parseVariable(cc, "Expect function name.");
	markInitialized(cc, false);
	function(cc, TYPE_FUNCTION);
	defineVariable(cc, arg);
}

static void varDeclaration(CompilerContext* cc) {
	do {
		uint32_t arg = parseVariable(cc, "Expect variable name.");

		if (match(cc, TOKEN_EQUAL)) {
			expression(cc);
		}
		else {
			emitByte(cc, OP_NIL);
			clearOpStack(cc);
		}

		defineVariable(cc, arg);

		if (cc->parser.panicMode) return;
	} while (match(cc, TOKEN_COMMA));

	consume(cc, TOKEN_SEMICOLON, "Expect ';' after variable declaration.");
}

static void constDeclaration(CompilerContext* cc) {
	do {
		uint32_t arg = parseVariable(cc, "Expect constant name.");

		if (!match(cc, TOKEN_EQUAL)) {
			errorAtCurrent(cc, "Constant must be initialized.");
		}

		expression(cc);

		defineConst(cc, arg);

		if (cc->parser.panicMode) return;
	} while (match(cc, TOKEN_COMMA));

	consume(cc, TOKEN_SEMICOLON, "Expect ';' after constant declaration.");
}

static void expressionStatement(CompilerContext* cc) {
	expression(cc);
	consume(cc, TOKEN_SEMICOLON, "Expect ';' after expression.");
	emitPopCount(cc, 1);
}

static void forStatement(CompilerContext* cc) {
	Compiler* current = cc->currentCompiler;
	//for is a block
	beginScope(cc);

	consume(cc, TOKEN_LEFT_PAREN, "Expect '(' after 'for'.");

	if (match(cc, TOKEN_SEMICOLON)) { //like this -> for(;;)
		// No initializer.
	}
	else if (match(cc, TOKEN_VAR)) { //like this -> for(var i = 0;;)
		varDeclaration(cc);
	}
	else if (match(cc, TOKEN_CONST)) {
		constDeclaration(cc);
	}
	else { //like this -> for( here ;;)
		expressionStatement(cc);
	}

	int32_t loopStart = currentChunk(cc)->count;

	int32_t exitJump = -1;
	if (!match(cc, TOKEN_SEMICOLON)) {//for(; here ;)
		int32_t condStart = currentChunk(cc)->count;
		expression(cc);
		consume(cc, TOKEN_SEMICOLON, "Expect ';' after loop condition.");

		// Jump out of the loop if the condition is false.
		exitJump = emitBranchJump(cc, OP_JUMP_IF_FALSE_POP, condStart);
	}

	//the code is: init,condition,increase,body,loop_to_increase
	if (!match(cc, TOKEN_RIGHT_PAREN)) { // for(;;here)
		//we don't do increase first,we go to body first
		int32_t bodyJump = emitJump(cc, OP_JUMP);
		int32_t incrementStart = currentChunk(cc)->count;
		expression(cc);
		emitPopCount(cc, 1);

		consume(cc, TOKEN_RIGHT_PAREN, "Expect ')' after for clauses.");

		//we go to where the condition is
		emitLoop(cc, loopStart);
		//after the body,we go to increase
		loopStart = incrementStart;
		//patch the body,first time,we jump to body
		patchJump(cc, bodyJump);
	}

	//record the loop
	LoopContext loop = (LoopContext){
		.start = loopStart,
		.enclosing = current->currentLoop,
		.breakJumps = NULL,
		.breakJumpCount = 0 ,
		.enterParamCount = current->localCount,
		.continueJumps = NULL,
		.continueJumpCount = 0,
		.loopType = LOOP_FOR
	};
	loop.breakJumpCapacity = 8;
	loop.breakJumps = ALLOCATE_NO_GC(int32_t, loop.breakJumpCapacity);
	current->currentLoop = &loop;

	statement(cc);
	emitLoop(cc, loopStart);

	//if there is no exitJump,this is an infinite loop
	if (exitJump != -1) {
		patchJump(cc, exitJump);
	}

	while (loop.breakJumpCount > 0) {
		patchJump(cc, loop.breakJumps[--loop.breakJumpCount]);
	}

	FREE_ARRAY_NO_GC(int32_t, loop.breakJumps, loop.breakJumpCapacity);
	current->currentLoop = current->currentLoop->enclosing;
	//end block
	endScope(cc);
}

static void foreachStatement(CompilerContext* cc) {
	Compiler* current = cc->currentCompiler;
	//locals layout: [others..., k, v, hidden iterator, body locals]
	beginScope(cc);

	//var/const only controls user assignment; OP_ITERATE always overwrites k/v
	consume(cc, TOKEN_LEFT_PAREN, "Expect '(' after 'foreach'.");
	if (!match(cc, TOKEN_VAR) && !match(cc, TOKEN_CONST)) {
		errorAtCurrent(cc, "Expect 'var' or 'const' after 'foreach'.");
		return;
	}
	bool isConst = (cc->parser.previous.type == TOKEN_CONST);

	emitBytes(cc, 2, OP_NIL, OP_NIL);
	clearOpStack(cc);

	consume(cc, TOKEN_IDENTIFIER, "Expect loop key variable name.");
	Token keyName = cc->parser.previous;

	consume(cc, TOKEN_COMMA, "Expect ',' between loop variables.");

	consume(cc, TOKEN_IDENTIFIER, "Expect loop value variable name.");
	Token valueName = cc->parser.previous;

	consume(cc, TOKEN_IN, "Expect 'in' after loop variables.");

	expression(cc);
	consume(cc, TOKEN_RIGHT_PAREN, "Expect ')' after the iterable.");

	//declare after the iterable: a same-named variable in it resolves to the
	//enclosing scope, only the loop body gets the new k/v
	declareVariableAt(cc, &keyName);
	markInitialized(cc, isConst);
	declareVariableAt(cc, &valueName);
	markInitialized(cc, isConst);

	emitByte(cc, OP_NEW_ITERATOR);
	clearOpStack(cc);

	//hidden local: unresolvable by user code (isHidden skips it in every matching path)
	Token hiddenIterator = (Token){ .start = "", .length = 0, .line = cc->parser.previous.line };
	addLocal(cc, hiddenIterator, true);
	markInitialized(cc, false);

	LoopContext loop = (LoopContext){
		.start = currentChunk(cc)->count,
		.enclosing = current->currentLoop,
		.breakJumps = NULL,
		.breakJumpCount = 0,
		.enterParamCount = current->localCount,
		.continueJumps = NULL,
		.continueJumpCount = 0,
		.loopType = LOOP_FOREACH
	};
	loop.breakJumpCapacity = 8;
	loop.breakJumps = ALLOCATE_NO_GC(int32_t, loop.breakJumpCapacity);
	current->currentLoop = &loop;

	//exhaustion jump: patched past the dead endScope cleanup below
	int32_t exhaustJump = emitJump(cc, OP_ITERATE);

	statement(cc);

	emitLoop(cc, loop.start);

	//dead cleanup bytes: both loop exits already unwound k/v/iter at runtime
	endScope(cc);

	patchJump(cc, exhaustJump);

	while (loop.breakJumpCount > 0) {
		patchJump(cc, loop.breakJumps[--loop.breakJumpCount]);
	}

	FREE_ARRAY_NO_GC(int32_t, loop.breakJumps, loop.breakJumpCapacity);
	current->currentLoop = current->currentLoop->enclosing;
}

static void ifStatement(CompilerContext* cc) {
	consume(cc, TOKEN_LEFT_PAREN, "Expect '(' after 'if'.");
	int32_t condStart = currentChunk(cc)->count;
	expression(cc);
	consume(cc, TOKEN_RIGHT_PAREN, "Expect ')' after condition.");

	int32_t thenJump = emitBranchJump(cc, OP_JUMP_IF_FALSE_POP, condStart);
	statement(cc);

	int32_t elseJump = emitJump(cc, OP_JUMP);
	if (thenJump != -1) patchJump(cc, thenJump);

	if (match(cc, TOKEN_ELSE)) statement(cc);
	patchJump(cc, elseJump);
}

static void branchCaseStatementInner(CompilerContext* cc) {
	if (!match(cc, TOKEN_NONE)) {
		int32_t condStart = currentChunk(cc)->count;
		expression(cc);
		int32_t thenJump = emitBranchJump(cc, OP_JUMP_IF_FALSE_POP, condStart);
		consume(cc, TOKEN_COLON, "Expect ':' after condition.");
		statement(cc);

		int32_t elseJump = emitJump(cc, OP_JUMP);
		if (thenJump != -1) patchJump(cc, thenJump);

		//prevent endless stack overflow
		if (cc->parser.hadError) return;

		//seek '}' or next case
		if (!match(cc, TOKEN_RIGHT_BRACE)) {
			branchCaseStatement(cc);
		}
		patchJump(cc, elseJump);
	}
	else {
		consume(cc, TOKEN_COLON, "Expect ':' after 'none'.");
		statement(cc);
		//'none' must be the last case
		consume(cc, TOKEN_RIGHT_BRACE, "Expect '}' after 'none' case.");
	}
}

//recursion guard:one frame per case
static void branchCaseStatement(CompilerContext* cc) {
	if (cc->parser.parseDepth >= MAX_PARSE_DEPTH) {
		error(cc, "Code nesting is too deep.");
		return;
	}
	cc->parser.parseDepth++;
	branchCaseStatementInner(cc);
	cc->parser.parseDepth--;
}

static void branchStatement(CompilerContext* cc) {
	consume(cc, TOKEN_LEFT_BRACE, "Expect '{' after 'branch'.");
	branchCaseStatement(cc);
}

static void printStatement(CompilerContext* cc) {
	uint32_t argCount = 0;
	do {
		expression(cc);
		if (++argCount > UINT8_MAX) error(cc, "Too many arguments in print.");
	} while (match(cc, TOKEN_COMMA));
	consume(cc, TOKEN_SEMICOLON, "Expect ';' after value.");

	//values were pushed left-to-right; the VM prints and pops them in order
	emitBytes(cc, 2, OP_PRINT, (uint8_t)argCount);
	clearOpStack(cc);
}

static void throwStatement(CompilerContext* cc) {
	expression(cc);
	consume(cc, TOKEN_SEMICOLON, "Expect ';' after value.");
	emitByte(cc, OP_THROW);
	clearOpStack(cc);
}

static void returnStatement(CompilerContext* cc) {
	Compiler* current = cc->currentCompiler;
	if (current->type == TYPE_SCRIPT) {
		error(cc, "Can't return from top-level code.");
	}

	if (match(cc, TOKEN_SEMICOLON)) {
		emitReturn(cc);
	}
	else {
		if (current->type == TYPE_INITIALIZER) {
			error(cc, "Can't return a value from an initializer.");
		}

		expression(cc);
		consume(cc, TOKEN_SEMICOLON, "Expect ';' after return value.");
		emitByte(cc, OP_RETURN);
		clearOpStack(cc);
	}
}

static void cedeStatement(CompilerContext* cc) {
	consume(cc, TOKEN_SEMICOLON, "Expect ';' after 'cede'.");
	emitByte(cc, OP_CEDE);
	clearOpStack(cc);
}

//exports in module script
static void exportsStatement(CompilerContext* cc) {
	Compiler* current = cc->currentCompiler;
	if (current->type != TYPE_MODULE) {
		error(cc, "Only module can use 'export'.");
	}

	if (match(cc, TOKEN_SEMICOLON)) {
		emitReturn(cc);
	}
	else {
		expression(cc);
		consume(cc, TOKEN_SEMICOLON, "Expect ';' after export value.");
		emitByte(cc, OP_RETURN);
		clearOpStack(cc);
	}
}

static void import_(CompilerContext* cc, bool canAssign) {
	//parse the module name
	expression(cc);

	//emit the import command
	emitByte(cc, OP_IMPORT);
	clearOpStack(cc);
}

static void whileStatement(CompilerContext* cc) {
	Compiler* current = cc->currentCompiler;
	int32_t loopStart = currentChunk(cc)->count;

	consume(cc, TOKEN_LEFT_PAREN, "Expect '(' after 'while'.");
	int32_t condStart = currentChunk(cc)->count;
	expression(cc);
	consume(cc, TOKEN_RIGHT_PAREN, "Expect ')' after condition.");

	int32_t exitJump = emitBranchJump(cc, OP_JUMP_IF_FALSE_POP, condStart);

	//record the loop
	LoopContext loop = (LoopContext){
		.start = loopStart,
		.enclosing = current->currentLoop,
		.breakJumps = NULL,
		.breakJumpCount = 0 ,
		.enterParamCount = current->localCount,
		.continueJumps = NULL,
		.continueJumpCount = 0,
		.loopType = LOOP_WHILE
	};
	loop.breakJumpCapacity = 8;
	loop.breakJumps = ALLOCATE_NO_GC(int32_t, loop.breakJumpCapacity);
	current->currentLoop = &loop;

	statement(cc);

	emitLoop(cc, loopStart);

	if (exitJump != -1) {
		patchJump(cc, exitJump);
	}

	while (loop.breakJumpCount > 0) {
		patchJump(cc, loop.breakJumps[--loop.breakJumpCount]);
	}

	FREE_ARRAY_NO_GC(int32_t, loop.breakJumps, loop.breakJumpCapacity);
	current->currentLoop = current->currentLoop->enclosing;
}

static void doWhileStatement(CompilerContext* cc) {
	Compiler* current = cc->currentCompiler;
	int32_t loopStart = currentChunk(cc)->count;
	//record the loop
	LoopContext loop = (LoopContext){
		.start = loopStart,
		.enclosing = current->currentLoop,
		.breakJumps = NULL,
		.breakJumpCount = 0 ,
		.enterParamCount = current->localCount,
		.continueJumps = NULL,
		.continueJumpCount = 0,
		.loopType = LOOP_DO_WHILE
	};
	loop.breakJumpCapacity = 8;
	loop.breakJumps = ALLOCATE_NO_GC(int32_t, loop.breakJumpCapacity);
	current->currentLoop = &loop;

	statement(cc);

	consume(cc, TOKEN_WHILE, "Expect 'while' after 'do' to form a valid 'do-while'.");
	consume(cc, TOKEN_LEFT_PAREN, "Expect '(' after 'while'.");
	int32_t condStart = currentChunk(cc)->count;

	//now that the condition position is known, route every 'continue' of this
	//loop to the condition check (the body start would re-run the body and
	//never re-evaluate the condition)
	while (loop.continueJumpCount > 0) {
		patchJumpTo(cc, loop.continueJumps[--loop.continueJumpCount], condStart);
	}
	FREE_ARRAY_NO_GC(int32_t, loop.continueJumps, loop.continueJumpCapacity);

	expression(cc);
	consume(cc, TOKEN_RIGHT_PAREN, "Expect ')' after condition.");
	consume(cc, TOKEN_SEMICOLON, "Expect ';' after 'do-while' loop.");

	int32_t exitJump = emitBranchJump(cc, OP_JUMP_IF_FALSE_POP, condStart);
	emitLoop(cc, loopStart);
	if (exitJump != -1) {
		patchJump(cc, exitJump);
	}

	while (loop.breakJumpCount > 0) {
		patchJump(cc, loop.breakJumps[--loop.breakJumpCount]);
	}
	FREE_ARRAY_NO_GC(int32_t, loop.breakJumps, loop.breakJumpCapacity);
	current->currentLoop = current->currentLoop->enclosing;
}

//only the loop itself can fix the jump position
static void breakStatement(CompilerContext* cc) {
	Compiler* current = cc->currentCompiler;
	if (current->currentLoop == NULL) {
		error(cc, "Cannot use 'break' outside of a loop.");
		return;
	}

	//foreach: the iterator (last of k/v/iter) is gc-managed, unwind it with the
	//body locals down to k's index so body locals and k/v go in one pass
	if (current->currentLoop->loopType == LOOP_FOREACH) {
		closeLoopLocals(cc, current->currentLoop->enterParamCount - 3);
	}
	else {
		closeLoopLocals(cc, current->currentLoop->enterParamCount);
	}

	int32_t jump = emitJump(cc, OP_JUMP);

	if (current->currentLoop->breakJumpCount == current->currentLoop->breakJumpCapacity) {
		uint32_t oldCapacity = current->currentLoop->breakJumpCapacity;
		uint32_t newCapacity = GROW_CAPACITY(oldCapacity);

		//capacity is uint16_t: 32768 << 1 wraps to 0, must check before the store
		if (newCapacity > UINT16_MAX) {
			error(cc, "Too many break statements in one loop.");
			return;
		}

		current->currentLoop->breakJumpCapacity = (uint16_t)newCapacity;
		current->currentLoop->breakJumps = GROW_ARRAY_NO_GC(int32_t, current->currentLoop->breakJumps, oldCapacity, newCapacity);
	}

	current->currentLoop->breakJumps[current->currentLoop->breakJumpCount++] = jump;

	consume(cc, TOKEN_SEMICOLON, "Expect ';' after 'break'.");
}

static void continueStatement(CompilerContext* cc) {
	Compiler* current = cc->currentCompiler;
	if (current->currentLoop == NULL) {
		error(cc, "Cannot use 'continue' outside of a loop.");
		return;
	}

	closeLoopLocals(cc, current->currentLoop->enterParamCount);

	if (current->currentLoop->loopType == LOOP_DO_WHILE) {
		//do-while: the condition check sits after the body and its position
		//is not known yet, so jump forward and patch it to condStart later
		//(jumping to loop.start would skip the condition -> infinite loop)
		LoopContext* loop = current->currentLoop;

		if (loop->continueJumpCount == loop->continueJumpCapacity) {
			uint32_t oldCapacity = loop->continueJumpCapacity;
			uint32_t newCapacity = GROW_CAPACITY(oldCapacity);

			//capacity is uint16_t: 32768 << 1 wraps to 0, must check before the store
			if (newCapacity > UINT16_MAX) {
				error(cc, "Too many continue statements in one loop.");
				return;
			}

			loop->continueJumpCapacity = (uint16_t)newCapacity;
			loop->continueJumps = GROW_ARRAY_NO_GC(int32_t, loop->continueJumps, oldCapacity, newCapacity);
		}

		loop->continueJumps[loop->continueJumpCount++] = emitJump(cc, OP_JUMP);
	}
	else {
		emitLoop(cc, current->currentLoop->start);
	}

	consume(cc, TOKEN_SEMICOLON, "Expect ';' after 'continue'.");
}

static void synchronize(CompilerContext* cc) {
	cc->parser.panicMode = false;

	while (cc->parser.current.type != TOKEN_EOF) {
		if (cc->parser.previous.type == TOKEN_SEMICOLON) return;
		switch (cc->parser.current.type) {
		case TOKEN_CLASS:
		case TOKEN_FUN:
		case TOKEN_VAR:
		case TOKEN_CONST:
		case TOKEN_FOR:
		case TOKEN_IF:
		case TOKEN_BRANCH:
		case TOKEN_DO:
		case TOKEN_WHILE:
		case TOKEN_FOREACH:
		case TOKEN_PRINT:
		case TOKEN_RETURN:
		case TOKEN_THROW:
		case TOKEN_IMPORT:
		case TOKEN_EXPORT:
		case TOKEN_CEDE:
			return;

		default:
			break; // Do nothing.
		}

		advance(cc);
	}
}

static void statementInner(CompilerContext* cc) {
	if (match(cc, TOKEN_PRINT)) {
		printStatement(cc);
	}
	else if (match(cc, TOKEN_IF)) {
		ifStatement(cc);
	}
	else if (match(cc, TOKEN_BRANCH)) {
		branchStatement(cc);
	}
	else if (match(cc, TOKEN_RETURN)) {
		returnStatement(cc);
	}
	else if (match(cc, TOKEN_WHILE)) {
		whileStatement(cc);
	}
	else if (match(cc, TOKEN_DO)) {
		doWhileStatement(cc);
	}
	else if (match(cc, TOKEN_FOR)) {
		forStatement(cc);
	}
	else if (match(cc, TOKEN_FOREACH)) {
		foreachStatement(cc);
	}
	else if (match(cc, TOKEN_BREAK)) {
		breakStatement(cc);
	}
	else if (match(cc, TOKEN_CONTINUE)) {
		continueStatement(cc);
	}
	else if (match(cc, TOKEN_LEFT_BRACE)) {
		beginScope(cc);
		block(cc);
		endScope(cc);
	}
	else if (match(cc, TOKEN_THROW)) {
		throwStatement(cc);
	}
	else if (match(cc, TOKEN_EXPORT)) {
		exportsStatement(cc);
	}
	else if (match(cc, TOKEN_CEDE)) {
		cedeStatement(cc);
	}
	else {
		expressionStatement(cc);
	}
}

//recursion guard:if/else chains,branch and nested blocks recurse through here
static void statement(CompilerContext* cc) {
	if (cc->parser.parseDepth >= MAX_PARSE_DEPTH) {
		error(cc, "Code nesting is too deep.");
		return;
	}
	cc->parser.parseDepth++;
	statementInner(cc);
	cc->parser.parseDepth--;
}

static void declaration(CompilerContext* cc) {
	//might went panicMode
	if (cc->parser.panicMode) synchronize(cc);

	if (match(cc, TOKEN_CLASS)) {
		classDeclaration(cc);
	}
	else if (match(cc, TOKEN_FUN)) {
		funDeclaration(cc);
	}
	else if (match(cc, TOKEN_VAR)) {
		varDeclaration(cc);
	}
	else if (match(cc, TOKEN_CONST)) {
		constDeclaration(cc);
	}
	else {
		statement(cc);
	}

	//If we hit a compile error while parsing the previous statement, we enter panic mode. When that happens, after the statement we start synchronizing.
	if (cc->parser.panicMode) synchronize(cc);
}

static void binary(CompilerContext* cc, bool canAssign) {
	TokenType operatorType = cc->parser.previous.type;
	ParseRule* rule = getRule(operatorType);
	parsePrecedence(cc, (Precedence)(rule->precedence + 1));

	switch (operatorType) {
	case TOKEN_PLUS: {
		emitByte(cc, OP_ADD);
		emitOpStack(cc, OP_ADD, true);
		break;
	}
	case TOKEN_MINUS: {
		emitByte(cc, OP_SUBTRACT);
		emitOpStack(cc, OP_SUBTRACT, true);
		break;
	}
	case TOKEN_STAR: {
		emitByte(cc, OP_MULTIPLY);
		emitOpStack(cc, OP_MULTIPLY, true);
		break;
	}
	case TOKEN_SLASH: {
		emitByte(cc, OP_DIVIDE);
		emitOpStack(cc, OP_DIVIDE, true);
		break;
	}
	case TOKEN_PERCENT: {
		emitByte(cc, OP_MODULUS);
		emitOpStack(cc, OP_MODULUS, true);
		break;
	}
	case TOKEN_BANG_EQUAL: {
		emitByte(cc, OP_NOT_EQUAL);
		emitOpStack(cc, OP_NOT_EQUAL, true);
		break;
	}
	case TOKEN_EQUAL_EQUAL: {
		emitByte(cc, OP_EQUAL);
		emitOpStack(cc, OP_EQUAL, true);
		break;
	}
	case TOKEN_GREATER: {
		emitByte(cc, OP_GREATER);
		emitOpStack(cc, OP_GREATER, true);
		break;
	}
	case TOKEN_GREATER_EQUAL: {
		emitByte(cc, OP_GREATER_EQUAL);
		emitOpStack(cc, OP_GREATER_EQUAL, true);
		break;
	}
	case TOKEN_LESS: {
		emitByte(cc, OP_LESS);
		emitOpStack(cc, OP_LESS, true);
		break;
	}
	case TOKEN_LESS_EQUAL: {
		emitByte(cc, OP_LESS_EQUAL);
		emitOpStack(cc, OP_LESS_EQUAL, true);
		break;
	}
	case TOKEN_INSTANCE_OF: {
		emitByte(cc, OP_INSTANCE_OF);
		clearOpStack(cc);
		break;
	}
	case TOKEN_IN: {
		emitByte(cc, OP_IN);
		clearOpStack(cc);
		break;
	}
	case TOKEN_BIT_AND: {
		emitBytes(cc, 2, OP_BITWISE, BIT_OP_AND);
		emitOpStack(cc, OP_BITWISE, true);
		break;
	}
	case TOKEN_BIT_OR: {
		emitBytes(cc, 2, OP_BITWISE, BIT_OP_OR);
		emitOpStack(cc, OP_BITWISE, true);
		break;
	}
	case TOKEN_BIT_XOR: {
		emitBytes(cc, 2, OP_BITWISE, BIT_OP_XOR);
		emitOpStack(cc, OP_BITWISE, true);
		break;
	}
	case TOKEN_BIT_SHL: {
		emitBytes(cc, 2, OP_BITWISE, BIT_OP_SHL);
		emitOpStack(cc, OP_BITWISE, true);
		break;
	}
	case TOKEN_BIT_SHR: {
		emitBytes(cc, 2, OP_BITWISE, BIT_OP_SHR);
		emitOpStack(cc, OP_BITWISE, true);
		break;
	}
	case TOKEN_BIT_SAR: {
		emitBytes(cc, 2, OP_BITWISE, BIT_OP_SAR);
		emitOpStack(cc, OP_BITWISE, true);
		break;
	}
	default: return; // Unreachable.
	}
}

static void call(CompilerContext* cc, bool canAssign) {
	uint8_t argCount = argumentList(cc);
	emitBytes(cc, 2, OP_CALL, argCount);
	clearOpStack(cc);
}

//after the receiver expression, check whether it compiled to a plain local load
//(e.g. 'this' at slot 0 or a local variable); if so, roll the [OP_GET_LOCAL slot:u16]
//bytes back and return the slot so the property access can fuse into one instruction
//returns -1 when the receiver is not a local load (upvalue/global/complex expression)
static int32_t tryPopReceiverLocal(CompilerContext* cc) {
	int32_t receiverLocal = -1;
#if LOXFLUX_COMPILATION_TIME_OPTIMIZATION
	if (!cc->optimizationEnabled) return receiverLocal;

	OPStack* opStack = currentOpStack(cc);
	if (opStack_peek(opStack, 0) == OP_GET_LOCAL) {
		Chunk* chunk = currentChunk(cc);
		uint32_t slot = (uint32_t)chunk->code[chunk->count - 2] | ((uint32_t)chunk->code[chunk->count - 1] << 8);
		chunk_fallback(chunk, 3);		//[OP_GET_LOCAL lo hi]
		opStack_fallback(opStack, 1);	//the opcode entry
		receiverLocal = (int32_t)slot;
	}
#endif
	return receiverLocal;
}

static void dot(CompilerContext* cc, bool canAssign) {
	consume(cc, TOKEN_IDENTIFIER, "Expect property name after '.'.");
	uint32_t name = identifierConstant(cc, &cc->parser.previous);

	if (canAssign && match(cc, TOKEN_EQUAL)) {
		int32_t receiverLocal = tryPopReceiverLocal(cc);

		expression(cc);
		uint8_t cacheSlot = emitCacheSlot(cc);

		if (receiverLocal >= 0) {
			//[op][local u16][name u24][cache u8], the assigned value remains on the stack
			emitBytes(cc, 7, OP_SET_PROP_LOCAL, (uint8_t)receiverLocal, (uint8_t)((uint32_t)receiverLocal >> 8),
				(uint8_t)name, (uint8_t)(name >> 8), (uint8_t)(name >> 16), cacheSlot);
		}
		else {
			emitConstantCommond(cc, OP_SET_PROPERTY, name);
			emitByte(cc, cacheSlot);
		}
		//clear expression ops,keep SET_PROPERTY for POP merge
		clearOpStack(cc);
		emitOpStack(cc, receiverLocal >= 0 ? OP_SET_PROP_LOCAL : OP_SET_PROPERTY, false);
	}
	else if (match(cc, TOKEN_LEFT_PAREN)) {
#if LOXFLUX_COMPILATION_TIME_OPTIMIZATION
		//fold `@module.member(args)` -> `OP_CONSTANT <native> + args + OP_CALL`:
		//builtin module fields are frozen, so the member is a compile-time constant; this drops
		//the module push and the invoke site (and its inline-cache slot)
		//detect a bare builtin-module receiver by its tracked opcode (like tryPopReceiverLocal)
		if (cc->optimizationEnabled) {
			OPStack* opStack = currentOpStack(cc);
			Chunk* chunk = currentChunk(cc);
			if (opStack_peek(opStack, 0) == OP_MODULE_BUILTIN) {
				//module id is the operand byte right behind the opcode
				uint8_t builtinModule = chunk->code[chunk->count - 1];

				LfxValue member;
				if (tableGet(&cc->vm->builtins[builtinModule].fields, AS_STRING(cc->vm->constants.values[name]), &member)) {
					if (!IS_NATIVE(member)) {
						//frozen builtin member that is not a method (e.g. @math.PI): calling it is a
						//compile-time-detectable error
						errorAtCurrent(cc, "Cannot call a non-method member of a builtin module.");
						return;
					}
					//drop the OP_MODULE_BUILTIN push (opcode + module u8)
					chunk_fallback(chunk, 2);
					opStack_fallback(opStack, 1);//the opcode entry
					emitConstant(cc, member);
					uint8_t argCount = argumentList(cc);
					emitBytes(cc, 2, OP_CALL, argCount);
					clearOpStack(cc);
					return;
				}
			}
		}
#endif

		uint8_t argCount = argumentList(cc);
		emitConstantCommond(cc, OP_INVOKE, name);
		emitByte(cc, argCount);
		emitByte(cc, emitCacheSlot(cc));
		clearOpStack(cc);
	}
	else {
		int32_t receiverLocal = tryPopReceiverLocal(cc);

		if (receiverLocal >= 0) {
			//[op][local u16][name u24][cache u8], no module-builtin fold applies to local receivers
			uint8_t cacheSlot = emitCacheSlot(cc);
			emitBytes(cc, 7, OP_GET_PROP_LOCAL, (uint8_t)receiverLocal, (uint8_t)((uint32_t)receiverLocal >> 8),
				(uint8_t)name, (uint8_t)(name >> 8), (uint8_t)(name >> 16), cacheSlot);
			clearOpStack(cc);
		}
		else {
			emitConstantCommond(cc, OP_GET_PROPERTY, name);
			emitByte(cc, emitCacheSlot(cc));
			if (!foldBuiltinMember(cc)) {
				clearOpStack(cc);
			}
		}
	}
}

static void arrayLiteral(CompilerContext* cc, bool canAssign) {
	uint32_t elementCount = 0;
	if (!check(cc, TOKEN_RIGHT_SQUARE_BRACKET) && !check(cc, TOKEN_EOF)) {
		do {
			expression(cc); //parse values
			elementCount++;

			if (cc->parser.panicMode) return;
		} while (match(cc, TOKEN_COMMA));
	}
	consume(cc, TOKEN_RIGHT_SQUARE_BRACKET, "Expect ']' to close the array.");

	if (elementCount > ARRAY_MAX) {
		error(cc, "Array literal is too long.");
		return;
	}

	emitBytes(cc, 3, OP_NEW_ARRAY, (uint8_t)elementCount, (uint8_t)(elementCount >> 8));  //make array
	clearOpStack(cc);
}

static void objectLiteral(CompilerContext* cc, bool canAssign) {
	Compiler* current = cc->currentCompiler;
	if (current->objectNestingDepth == OBJECT_MAX_NESTING) {
		error(cc, "Too many nested objects.");
		return;
	}

	++current->objectNestingDepth;
	emitByte(cc, OP_NEW_OBJECT);
	clearOpStack(cc);

	if (!check(cc, TOKEN_RIGHT_BRACE) && !check(cc, TOKEN_EOF)) {
		do {
			uint32_t constant = UINT32_MAX;//limit is 2^24-1

			if (match(cc, TOKEN_IDENTIFIER)) {
				constant = identifierConstant(cc, &cc->parser.previous);
			}
			else if (match(cc, TOKEN_STRING)) {
				constant = makeConstant(cc, OBJ_VAL(copyString(cc->vm, cc->parser.previous.start + 1, cc->parser.previous.length - 2, false)));
			}
			else if (match(cc, TOKEN_STRING_ESCAPE)) {
				constant = makeConstant(cc, OBJ_VAL(copyString(cc->vm, cc->parser.previous.start + 1, cc->parser.previous.length - 2, true)));
			}
			else {
				errorAtCurrent(cc, "Expect property name.");
			}

			consume(cc, TOKEN_COLON, "Expect ':' after property name.");
			expression(cc); //get value
			emitConstantCommond(cc, OP_NEW_PROPERTY, constant);
			clearOpStack(cc);

			if (cc->parser.panicMode) {
				--current->objectNestingDepth;
				return;
			}
		} while (match(cc, TOKEN_COMMA));
	}

	consume(cc, TOKEN_RIGHT_BRACE, "Expect '}' to close the object.");
	--current->objectNestingDepth;
}

//when it is constant index,use this
static void mergeSubscript(CompilerContext* cc, uint8_t code, bool isAssignment) {
	Chunk* chunk = currentChunk(cc);
#define READ_CONSTANT(index) (cc->vm->constants.values[(index)])
#define READ_24BITS_INDEX()	\
	(((uint32_t)chunk->code[chunk->count - 1] << 16) +	\
	 ((uint32_t)chunk->code[chunk->count - 2] << 8) +	\
	 (uint32_t)(chunk->code[chunk->count - 3]))
#define READ_SHORT_INDEX()	\
	(((uint32_t)chunk->code[chunk->count - 2]) +	\
	 ((uint32_t)chunk->code[chunk->count - 1] << 8))

	//number constant index : OP_CONST_NUMBER 1 + 2 byte
	if (code == OP_CONST_NUMBER) {
		uint32_t index = READ_SHORT_INDEX();

		//must fallback
		chunk_fallback(chunk, 3);
		clearOpStack(cc);

		if (isAssignment) expression(cc);

		emitShortCommand(cc, isAssignment ? OP_SET_INDEX : OP_GET_INDEX, index);
		clearOpStack(cc);
	}
	else {
		//non-number constant index : OP_CONSTANT 1 + 3 byte
		uint32_t index = READ_24BITS_INDEX();
		LfxValue val = READ_CONSTANT(index);

		//must fallback
		chunk_fallback(chunk, 4);
		clearOpStack(cc);

		if (isAssignment) expression(cc);

		if (IS_STRING(val)) {
			emitConstantCommond(cc, isAssignment ? OP_SET_PROPERTY : OP_GET_PROPERTY, index);
			emitByte(cc, emitCacheSlot(cc));
			if (isAssignment) {
				//clear expression ops,keep SET_PROPERTY for POP merge
				clearOpStack(cc);
				emitOpStack(cc, OP_SET_PROPERTY, false);
			}
			else if (!foldBuiltinMember(cc)) {
				clearOpStack(cc);
			}
		}
		else {
			error(cc, "Can only subscript with string or number.\n");
		}
	}
#undef READ_CONSTANT
#undef READ_24BITS_INDEX
#undef READ_SHORT_INDEX
}

static void subscript(CompilerContext* cc, bool canAssign) {
	expression(cc);
	consume(cc, TOKEN_RIGHT_SQUARE_BRACKET, "Expect ']' after subscript.");

	// Because of the order of parsing, the index must be processed here
	uint8_t code = opStack_peek(currentOpStack(cc), 0);
	bool isAssignment = canAssign && match(cc, TOKEN_EQUAL);

	if (code == OP_CONSTANT || code == OP_CONST_NUMBER) {// constant index
		mergeSubscript(cc, code, isAssignment);
	}
	else {
		if (isAssignment) {
			expression(cc);
			emitByte(cc, OP_SET_SUBSCRIPT);
			clearOpStack(cc);
		}
		else {
			emitByte(cc, OP_GET_SUBSCRIPT);
			clearOpStack(cc);
		}
	}
}

//check builtin
static void builtinLiteral(CompilerContext* cc, bool canAssign) {
	uint8_t module = 0;
	switch (cc->parser.previous.type)
	{
	case TOKEN_MODULE_MATH: {
		module = MODULE_MATH;
		break;
	}
	case TOKEN_MODULE_ARRAY: {
		module = MODULE_ARRAY;
		break;
	}
	case TOKEN_MODULE_OBJECT: {
		module = MODULE_OBJECT;
		break;
	}
	case TOKEN_MODULE_STRING: {
		module = MODULE_STRING;
		break;
	}
	case TOKEN_MODULE_TIME: {
		module = MODULE_TIME;
		break;
	}
	case TOKEN_MODULE_CTOR: {
		module = MODULE_CTOR;
		break;
	}
	case TOKEN_MODULE_SYSTEM: {
		module = MODULE_SYSTEM;
		break;
	}
	default: {
		emitByte(cc, OP_NIL);
		clearOpStack(cc);
		return;
	}
	}

	emitBytes(cc, 2, OP_MODULE_BUILTIN, module);
	//reset tracking (merges never cross a module boundary) and track the push for dot()'s fold
	clearOpStack(cc);
	emitOpStack(cc, OP_MODULE_BUILTIN, false);
}

static void literal(CompilerContext* cc, bool canAssign) {
	switch (cc->parser.previous.type) {
	case TOKEN_FALSE: {
		emitByte(cc, OP_FALSE);
		emitOpStack(cc, OP_FALSE, false);
		break;
	}
	case TOKEN_NIL: {
		emitByte(cc, OP_NIL);
		emitOpStack(cc, OP_NIL, false);
		break;
	}
	case TOKEN_TRUE: {
		emitByte(cc, OP_TRUE);
		emitOpStack(cc, OP_TRUE, false);
		break;
	}
	default: return; // Unreachable.
	}
}

static void grouping(CompilerContext* cc, bool canAssign) {
	expression(cc);
	consume(cc, TOKEN_RIGHT_PAREN, "Expect ')' after expression.");
}

static void emitNumber(CompilerContext* cc, double value) {
	emitNumberConstant(cc, NUMBER_VAL(value));
}

static void number(CompilerContext* cc, bool canAssign) {
	emitNumber(cc, strtod(cc->parser.previous.start, NULL));
}

static void number_bin(CompilerContext* cc, bool canAssign) {
	emitNumber(cc, (double)strtoll(cc->parser.previous.start + 2, NULL, 2));
}

static void number_hex(CompilerContext* cc, bool canAssign) {
	emitNumber(cc, (double)strtoll(cc->parser.previous.start + 2, NULL, 16));
}

static void or_(CompilerContext* cc, bool canAssign) {
	//if true
	int32_t ifJump = emitJump(cc, OP_JUMP_IF_TRUE);
	emitPopCount(cc, 1);
	parsePrecedence(cc, PREC_OR);
	patchJump(cc, ifJump);
}

static void string(CompilerContext* cc, bool canAssign) {
	emitConstant(cc, OBJ_VAL(copyString(cc->vm, cc->parser.previous.start + 1, cc->parser.previous.length - 2, false)));
}

static void namedVariable(CompilerContext* cc, Token name, bool canAssign) {
	LocalInfo args = resolveLocal(cc, cc->currentCompiler, &name);
	int32_t arg = args.arg;

	if (arg != -1) {//it's a local var
		if (canAssign && match(cc, TOKEN_EQUAL)) {
			expression(cc);

			if (args.isConst) {
				errorAtCurrent(cc, "Assignment to constant variable.");
				return;
			}
			// 16-bit index
			emitBytes(cc, 3, OP_SET_LOCAL, (uint8_t)arg, (uint8_t)(arg >> 8));
			emitOpStack(cc, OP_SET_LOCAL, false);
		}
		else { // 16-bit index
			emitBytes(cc, 3, OP_GET_LOCAL, (uint8_t)arg, (uint8_t)(arg >> 8));
			emitOpStack(cc, OP_GET_LOCAL, false);
		}
	}
	else {
		args = resolveUpvalue(cc, cc->currentCompiler, &name);
		arg = args.arg;

		if (arg != -1) {//it's an upvalue
			if (canAssign && match(cc, TOKEN_EQUAL)) {
				expression(cc);

				if (args.isConst) {
					errorAtCurrent(cc, "Assignment to constant variable.");
					return;
				}
				// 16-bit index
				emitBytes(cc, 2, OP_SET_UPVALUE, (uint8_t)arg);
			}
			else { // 16-bit index
				emitBytes(cc, 2, OP_GET_UPVALUE, (uint8_t)arg);
			}
			clearOpStack(cc);
		}
		else {//it's a global var
			arg = identifierConstant(cc, &name);

			if (canAssign && match(cc, TOKEN_EQUAL)) {
				expression(cc);
				emitConstantCommond(cc, OP_SET_GLOBAL, arg);
			}
			else {
				emitConstantCommond(cc, OP_GET_GLOBAL, arg);
			}
			clearOpStack(cc);
		}
	}
}

static void variable(CompilerContext* cc, bool canAssign) {
	namedVariable(cc, cc->parser.previous, canAssign);
}

static Token syntheticToken(C_STR text) {
	return (Token) {
		.start = text, .length = (uint32_t)strlen(text)
	};
}

static void this_(CompilerContext* cc, bool canAssign) {
	if (cc->currentClass == NULL) {
		error(cc, "Can't use 'this' outside of a class.");
		return;
	}

	variable(cc, false);
}

static void super_(CompilerContext* cc, bool canAssign) {
	if (cc->currentClass == NULL) {
		error(cc, "Can't use 'super' outside of a class.");
	}
	else if (!cc->currentClass->hasSuperclass) {
		error(cc, "Can't use 'super' in a class with no superclass.");
	}

	consume(cc, TOKEN_DOT, "Expect '.' after 'super'.");
	consume(cc, TOKEN_IDENTIFIER, "Expect superclass method name.");
	uint32_t name = identifierConstant(cc, &cc->parser.previous);

	namedVariable(cc, syntheticToken("this"), false);//load instance

	if (match(cc, TOKEN_LEFT_PAREN)) {
		uint8_t argCount = argumentList(cc);
		namedVariable(cc, syntheticToken("super"), false);
		emitConstantCommond(cc, OP_SUPER_INVOKE, name);//super call
		emitByte(cc, argCount);
	}
	else {
		namedVariable(cc, syntheticToken("super"), false);//load class
		emitConstantCommond(cc, OP_GET_SUPER, name);//get method
	}
	clearOpStack(cc);
}

static void string_escape(CompilerContext* cc, bool canAssign) {
	emitConstant(cc, OBJ_VAL(copyString(cc->vm, cc->parser.previous.start + 1, cc->parser.previous.length - 2, true)));
}

static void unary(CompilerContext* cc, bool canAssign) {
	TokenType operatorType = cc->parser.previous.type;

	// Compile the operand.
	parsePrecedence(cc, PREC_UNARY);

	// Emit the operator instruction.
	switch (operatorType) {
	case TOKEN_BANG: {
		emitByte(cc, OP_NOT);
		emitOpStack(cc, OP_NOT, true);
		break;
	}
	case TOKEN_MINUS: {
		emitByte(cc, OP_NEGATE);
		emitOpStack(cc, OP_NEGATE, true);
		break;
	}
	case TOKEN_BIT_NOT: {
		emitBytes(cc, 2, OP_BITWISE, BIT_OP_NOT);
		clearOpStack(cc);
		break;
	}
	case TOKEN_TYPE_OF: {
		emitByte(cc, OP_TYPE_OF);
		clearOpStack(cc);
		break;
	}
	default: return; // Unreachable.
	}
}

ParseRule rules[] = {
	[TOKEN_LEFT_PAREN] = { grouping, call,   PREC_CALL },
	[TOKEN_RIGHT_PAREN] = { NULL,     NULL,   PREC_NONE },
	[TOKEN_LEFT_BRACE] = { objectLiteral,     NULL,   PREC_CALL },
	[TOKEN_RIGHT_BRACE] = { NULL,     NULL,   PREC_NONE },
	[TOKEN_LEFT_SQUARE_BRACKET] = { arrayLiteral,	subscript,	PREC_CALL },
	[TOKEN_RIGHT_SQUARE_BRACKET] = { NULL,     NULL,   PREC_NONE },
	[TOKEN_COMMA] = { NULL,     NULL,   PREC_NONE },
	[TOKEN_DOT] = { NULL,     dot,   PREC_CALL },
	[TOKEN_MINUS] = { unary   ,    binary, PREC_TERM },
	[TOKEN_PLUS] = { NULL,     binary, PREC_TERM },
	[TOKEN_SEMICOLON] = { NULL,     NULL,   PREC_NONE },
	[TOKEN_COLON] = { NULL,     NULL,   PREC_NONE },
	[TOKEN_SLASH] = { NULL,     binary, PREC_FACTOR },
	[TOKEN_STAR] = { NULL,     binary, PREC_FACTOR },
	[TOKEN_PERCENT] = { NULL,     binary, PREC_FACTOR },
	[TOKEN_BANG] = { unary,     NULL,   PREC_NONE },
	[TOKEN_BANG_EQUAL] = { NULL,     binary,   PREC_EQUALITY },
	[TOKEN_EQUAL] = { NULL,     NULL,   PREC_NONE },
	[TOKEN_EQUAL_EQUAL] = { NULL,     binary, PREC_EQUALITY },
	[TOKEN_GREATER] = { NULL,     binary, PREC_COMPARISON },
	[TOKEN_GREATER_EQUAL] = { NULL,     binary, PREC_COMPARISON },
	[TOKEN_LESS] = { NULL,     binary, PREC_COMPARISON },
	[TOKEN_LESS_EQUAL] = { NULL,     binary, PREC_COMPARISON },
	[TOKEN_INSTANCE_OF] = { NULL,     binary,   PREC_RELATIONAL },
	[TOKEN_TYPE_OF] = { unary,	NULL, PREC_UNARY },
	[TOKEN_BIT_AND] = { NULL,	binary, PREC_BITWISE },
	[TOKEN_BIT_OR] = { NULL,		binary, PREC_BITWISE },
	[TOKEN_BIT_XOR] = { NULL,	binary, PREC_BITWISE },
	[TOKEN_BIT_NOT] = { unary,	NULL, PREC_UNARY },
	[TOKEN_BIT_SHL] = { NULL,	binary, PREC_BITWISE },
	[TOKEN_BIT_SHR] = { NULL,	binary, PREC_BITWISE },
	[TOKEN_BIT_SAR] = { NULL,	binary, PREC_BITWISE },
	[TOKEN_IDENTIFIER] = { variable,     NULL,   PREC_NONE },
	[TOKEN_STRING] = { string,     NULL,   PREC_NONE },
	[TOKEN_STRING_ESCAPE] = { string_escape,     NULL,   PREC_NONE },
	[TOKEN_NUMBER] = { number  ,   NULL,   PREC_NONE },
	[TOKEN_NUMBER_BIN] = { number_bin  ,   NULL,   PREC_NONE },
	[TOKEN_NUMBER_HEX] = { number_hex  ,   NULL,   PREC_NONE },
	[TOKEN_MODULE_MATH] = { builtinLiteral,     NULL,   PREC_NONE },
	[TOKEN_MODULE_ARRAY] = { builtinLiteral,     NULL,   PREC_NONE },
	[TOKEN_MODULE_OBJECT] = { builtinLiteral,     NULL,   PREC_NONE },
	[TOKEN_MODULE_STRING] = { builtinLiteral,     NULL,   PREC_NONE },
	[TOKEN_MODULE_TIME] = { builtinLiteral,     NULL,   PREC_NONE },
	[TOKEN_MODULE_CTOR] = { builtinLiteral,     NULL,   PREC_NONE },
	[TOKEN_MODULE_SYSTEM] = { builtinLiteral,     NULL,   PREC_NONE },
	[TOKEN_AND] = { NULL,     and_,   PREC_AND },
	[TOKEN_CLASS] = { NULL,     NULL,   PREC_NONE },
	[TOKEN_ELSE] = { NULL,     NULL,   PREC_NONE },
	[TOKEN_FALSE] = { literal,     NULL,   PREC_NONE },
	[TOKEN_FOR] = { NULL,     NULL,   PREC_NONE },
	[TOKEN_FOREACH] = { NULL,     NULL,   PREC_NONE },
	[TOKEN_IN] = { NULL,     binary,   PREC_RELATIONAL },
	[TOKEN_FUN] = { NULL,     NULL,   PREC_NONE },
	[TOKEN_LAMBDA] = { lambda,	NULL,	PREC_NONE },
	[TOKEN_RIGHT_ARROW] = { NULL,	NULL,	PREC_NONE },
	[TOKEN_IF] = { NULL,     NULL,   PREC_NONE },
	[TOKEN_BRANCH] = { NULL,     NULL,   PREC_NONE },
	[TOKEN_NONE] = { NULL,     NULL,   PREC_NONE },
	[TOKEN_NIL] = { literal,     NULL,   PREC_NONE },
	[TOKEN_OR] = { NULL,     or_,   PREC_OR },
	[TOKEN_PRINT] = { NULL,     NULL,   PREC_NONE },
	[TOKEN_THROW] = { NULL,     NULL,   PREC_NONE },
	[TOKEN_RETURN] = { NULL,     NULL,   PREC_NONE },
	[TOKEN_SUPER] = { super_,     NULL,   PREC_NONE },
	[TOKEN_THIS] = { this_,     NULL,   PREC_NONE },
	[TOKEN_TRUE] = { literal,     NULL,   PREC_NONE },
	[TOKEN_VAR] = { NULL,     NULL,   PREC_NONE },
	[TOKEN_CONST] = { NULL,     NULL,   PREC_NONE },
	[TOKEN_DO] = { NULL,     NULL,   PREC_NONE },
	[TOKEN_WHILE] = { NULL,     NULL,   PREC_NONE },
	[TOKEN_ERROR] = { NULL,     NULL,   PREC_NONE },
	[TOKEN_EOF] = { NULL,     NULL,   PREC_NONE },
	[TOKEN_BREAK] = { NULL,     NULL,   PREC_NONE },
	[TOKEN_CONTINUE] = { NULL,     NULL,   PREC_NONE },
	[TOKEN_IMPORT] = { import_,     NULL,   PREC_NONE },
	[TOKEN_EXPORT] = { NULL,     NULL,   PREC_NONE },
	[TOKEN_CEDE] = { NULL,     NULL,   PREC_NONE },
};

static ParseRule* getRule(TokenType type) {
	return &rules[type];
}

ObjFunction* compile(LfxVM* vm, C_STR source, FunctionType compileType) {
	CompilerContext cc = {
		.vm = vm,
		.currentClass = NULL,
		.currentCompiler = NULL,
		.printBytecode = vm->options.printBytecode,
		.optimizationEnabled = vm->options.optimizationEnabled,
		.scanner = { 0 },
		//init flags
		.parser = {
			.current = { 0 },
			.previous = { 0 },
			.hadError = false,
			.panicMode = false,
			.parseDepth = 0,
		}
	};

	Compiler compiler = { 0 };

	scanner_init(&cc.scanner, source);
	initCompiler(&cc, &compiler, compileType);

	advance(&cc);

	while (!match(&cc, TOKEN_EOF)) {
		declaration(&cc);
	}

	ObjFunction* function = endCompiler(&cc);
	freeLocals(&compiler);

	return cc.parser.hadError ? NULL : function;
}

// removed
//void markCompilerRoots()
//{
//	Compiler* compiler = current;
//	while (compiler != NULL) {
//		markObject((Obj*)compiler->function);
//		compiler = compiler->enclosing;
//	}
//}

/*
* This part uses hard-coded instruction lengths, which needs to be noted
*/
#if LOXFLUX_COMPILATION_TIME_OPTIMIZATION
//COLD_FUNCTION
static void instructionOptimize(CompilerContext* cc) {
	Compiler* current = cc->currentCompiler;
	Chunk* chunk = currentChunk(cc);
	OPStack* opStack = currentOpStack(cc);

	//opStack_peek will handle overflow
	uint8_t code = opStack_peek(opStack, 0);
	uint8_t prevRight = opStack_peek(opStack, 1);
	uint8_t prevLeft = opStack_peek(opStack, 2);

	//Constant folding
	bool isLeftConstNumber = (prevLeft == OP_CONST_NUMBER);
	bool isRightConstNumber = (prevRight == OP_CONST_NUMBER);
	bool isLeftConstant = (prevLeft == OP_CONSTANT) || isLeftConstNumber;
	bool isRightConstant = (prevRight == OP_CONSTANT) || isRightConstNumber;
	bool isBothConstant = (isLeftConstant && isRightConstant);
	//literal opcodes (true/false/nil) are compile-time constants too
	bool isLeftLiteral = (prevLeft == OP_TRUE) || (prevLeft == OP_FALSE) || (prevLeft == OP_NIL);
	bool isRightLiteral = (prevRight == OP_TRUE) || (prevRight == OP_FALSE) || (prevRight == OP_NIL);
	bool isLeftLocal = (prevLeft == OP_GET_LOCAL);
	bool isRightLocal = (prevRight == OP_GET_LOCAL);

#define CHUNK_PEEK(offset) chunk->code[chunk->count - (offset) - 1]
	//constants in vm.constants (non-number),24bits index
#define READ_GLOBAL_CONSTANT(index) (cc->vm->constants.values[(index)])
//constants in the compiler's own table (number),16bits index (merged into the function FAM at endCompiler)
//assert vanishes in release builds (NDEBUG)
#define READ_LOCAL_CONSTANT(index)	(assert((uint32_t)(index) < current->constants.count), current->constants.values[(index)])

#define READ_SHORT_INDEX(offset)	\
	(((uint32_t)chunk->code[chunk->count - (offset) - 1] << 8) +	\
	 ((uint32_t)chunk->code[chunk->count - (offset) - 2]))

#define READ_24BITS_INDEX(offset)	\
	(((uint32_t)chunk->code[chunk->count - (offset) - 1] << 16) +	\
	 ((uint32_t)chunk->code[chunk->count - (offset) - 2] << 8) +	\
	 (uint32_t)(chunk->code[chunk->count - (offset) - 3]))

//instruction size of a constant load
#define CONST_SIZE(isConstNumber) ((isConstNumber) ? 3 : 4)

//read the index by the constant kind,offset counts from the last emitted byte
#define READ_INDEX(isConstNumber, offset)	((isConstNumber) ? READ_SHORT_INDEX(offset) : READ_24BITS_INDEX(offset))

	//read the constant value by the constant kind
#define READ_CONST_VALUE(isConstNumber, offset)	\
	((isConstNumber) ? READ_LOCAL_CONSTANT(READ_INDEX(isConstNumber, offset))	\
					 : READ_GLOBAL_CONSTANT(READ_INDEX(isConstNumber, offset)))

//total byte size of an operand instruction (OP_CONST_NUMBER 3, OP_CONSTANT 4, literal ops 1)
#define OPERAND_SIZE(op)	\
	(((op) == OP_CONST_NUMBER) ? 3 : (((op) == OP_CONSTANT) ? 4 : 1))

	//resolve a compile-time constant operand (OP_CONSTANT/OP_CONST_NUMBER/OP_TRUE/OP_FALSE/OP_NIL) to its value
#define READ_OPERAND(op, offset)	\
	(((op) == OP_CONST_NUMBER)	? READ_LOCAL_CONSTANT(READ_SHORT_INDEX(offset)) :	\
	 ((op) == OP_CONSTANT)		? READ_GLOBAL_CONSTANT(READ_24BITS_INDEX(offset)) :	\
	 ((op) == OP_TRUE)			? TRUE_VAL :	\
	 ((op) == OP_FALSE)			? FALSE_VAL : NIL_VAL)

#define BINARY_CALC(left,right,op,sizeLeft,sizeRight)			\
	do {														\
		if (IS_NUMBER(left) && IS_NUMBER(right)) {				\
			double val = AS_NUMBER(left) op AS_NUMBER(right);	\
			chunk_fallback(chunk, 1 + sizeLeft + sizeRight);	\
			opStack_fallback(opStack,3);						\
			emitNumberConstant(cc, NUMBER_VAL(val));				\
		} else {												\
			error(cc, "Operands must be numbers.");					\
		}														\
	} while (false)

#define BINARY_CMP(left,right,op,sizeLeft,sizeRight)			\
	do {														\
		if (IS_NUMBER(left) && IS_NUMBER(right)) {				\
			bool val = AS_NUMBER(left) op AS_NUMBER(right);		\
			chunk_fallback(chunk, 1 + sizeLeft + sizeRight);	\
			opStack_fallback(opStack,3);						\
			emitByte(cc, val ? OP_TRUE : OP_FALSE);					\
			emitOpStack(cc, val ? OP_TRUE : OP_FALSE, false);		\
		}														\
		else if (IS_STRING(left) && IS_STRING(right)) {			\
			bool val = compareStringLike(left, right) op 0;		\
			chunk_fallback(chunk, 1 + sizeLeft + sizeRight);	\
			opStack_fallback(opStack,3);						\
			emitByte(cc, val ? OP_TRUE : OP_FALSE);					\
			emitOpStack(cc, val ? OP_TRUE : OP_FALSE, false);		\
		}														\
		else {													\
			error(cc, "Operands must be two numbers or two strings.");\
		}														\
	} while (false)

	switch (code) {
	case OP_ADD: {
		if (isBothConstant) {
			LfxValue left = READ_CONST_VALUE(isLeftConstNumber, 1 + CONST_SIZE(isRightConstNumber));
			LfxValue right = READ_CONST_VALUE(isRightConstNumber, 1);

			if (IS_NUMBER(left) && IS_NUMBER(right)) {
				double val = AS_NUMBER(left) + AS_NUMBER(right);
				chunk_fallback(chunk, 1 + CONST_SIZE(isLeftConstNumber) + CONST_SIZE(isRightConstNumber));//add + const + const
				opStack_fallback(opStack, 3);
				emitNumberConstant(cc, NUMBER_VAL(val));
			}
			else if (IS_STRING(left) && IS_STRING(right)) {
				ObjString* val = connectString(cc->vm, AS_STRING(left), AS_STRING(right));
				chunk_fallback(chunk, 1 + CONST_SIZE(isLeftConstNumber) + CONST_SIZE(isRightConstNumber));//add + const + const
				opStack_fallback(opStack, 3);
				emitConstant(cc, OBJ_VAL(val));
			}
			else {
				error(cc, "Operands must be two numbers or two strings.");
			}
		}
		else if (isRightConstNumber) {
			//number constant -> super instruction,16bits index
			chunk_fallback(chunk, 1);//op
			clearOpStack(cc);
			CHUNK_PEEK(2) = OP_ADD_CONST; //convert command
		}
		//non-number constant : no super instruction for arithmetic
		else if (isRightLocal) {
			chunk_fallback(chunk, 1);//op
			clearOpStack(cc);
			CHUNK_PEEK(2) = OP_ADD_LOCAL; //convert command
		}
		break;
	}
	case OP_SUBTRACT: {
		if (isBothConstant) {
			LfxValue left = READ_CONST_VALUE(isLeftConstNumber, 1 + CONST_SIZE(isRightConstNumber));
			LfxValue right = READ_CONST_VALUE(isRightConstNumber, 1);
			BINARY_CALC(left, right, -, CONST_SIZE(isLeftConstNumber), CONST_SIZE(isRightConstNumber));
		}
		else if (isRightConstNumber) {
			//number constant -> super instruction,16bits index
			chunk_fallback(chunk, 1);//op
			clearOpStack(cc);
			CHUNK_PEEK(2) = OP_SUBTRACT_CONST; //convert command
		}
		//non-number constant : no super instruction for arithmetic
		else if (isRightLocal) {
			chunk_fallback(chunk, 1);//op
			clearOpStack(cc);
			CHUNK_PEEK(2) = OP_SUBTRACT_LOCAL; //convert command
		}
		break;
	}
	case OP_MULTIPLY: {
		if (isBothConstant) {
			LfxValue left = READ_CONST_VALUE(isLeftConstNumber, 1 + CONST_SIZE(isRightConstNumber));
			LfxValue right = READ_CONST_VALUE(isRightConstNumber, 1);
			BINARY_CALC(left, right, *, CONST_SIZE(isLeftConstNumber), CONST_SIZE(isRightConstNumber));
		}
		else if (isRightConstNumber) {
			//number constant -> super instruction,16bits index
			chunk_fallback(chunk, 1);//op
			clearOpStack(cc);
			CHUNK_PEEK(2) = OP_MULTIPLY_CONST; //convert command
		}
		//non-number constant : no super instruction for arithmetic
		else if (isRightLocal) {
			chunk_fallback(chunk, 1);//op
			clearOpStack(cc);
			CHUNK_PEEK(2) = OP_MULTIPLY_LOCAL; //convert command
		}
		break;
	}
	case OP_DIVIDE: {
		if (isBothConstant) {
			LfxValue left = READ_CONST_VALUE(isLeftConstNumber, 1 + CONST_SIZE(isRightConstNumber));
			LfxValue right = READ_CONST_VALUE(isRightConstNumber, 1);
			BINARY_CALC(left, right, / , CONST_SIZE(isLeftConstNumber), CONST_SIZE(isRightConstNumber));
		}
		else if (isRightConstNumber) {
			//number constant -> super instruction,16bits index
			chunk_fallback(chunk, 1);//op
			clearOpStack(cc);
			CHUNK_PEEK(2) = OP_DIVIDE_CONST; //convert command
		}
		//non-number constant : no super instruction for arithmetic
		else if (isRightLocal) {
			chunk_fallback(chunk, 1);//op
			clearOpStack(cc);
			CHUNK_PEEK(2) = OP_DIVIDE_LOCAL; //convert command
		}
		break;
	}
	case OP_MODULUS: {
		if (isBothConstant) {
			LfxValue left = READ_CONST_VALUE(isLeftConstNumber, 1 + CONST_SIZE(isRightConstNumber));
			LfxValue right = READ_CONST_VALUE(isRightConstNumber, 1);

			if (IS_NUMBER(left) && IS_NUMBER(right)) {
				double val = fmod(AS_NUMBER(left), AS_NUMBER(right));
				chunk_fallback(chunk, 1 + CONST_SIZE(isLeftConstNumber) + CONST_SIZE(isRightConstNumber));//op + const + const
				opStack_fallback(opStack, 3);
				emitNumberConstant(cc, NUMBER_VAL(val));
			}
			else {
				error(cc, "Operands must be numbers.");
			}
		}
		else if (isRightConstNumber) {
			//number constant -> super instruction,16bits index
			chunk_fallback(chunk, 1);//op
			clearOpStack(cc);
			CHUNK_PEEK(2) = OP_MODULUS_CONST; //convert command
		}
		//non-number constant : no super instruction for arithmetic
		else if (isRightLocal) {
			chunk_fallback(chunk, 1);//op
			clearOpStack(cc);
			CHUNK_PEEK(2) = OP_MODULUS_LOCAL; //convert command
		}
		break;
	}
	case OP_NOT: {
		if (isRightConstNumber) {
			chunk_fallback(chunk, 1 + 3);//op + const
			opStack_fallback(opStack, 2);
			emitByte(cc, OP_FALSE);
			emitOpStack(cc, OP_FALSE, false);
		}
		else if (prevRight == OP_CONSTANT) {
			chunk_fallback(chunk, 1 + 4);//op + const
			opStack_fallback(opStack, 2);
			emitByte(cc, OP_FALSE);
			emitOpStack(cc, OP_FALSE, false);
		}
		else if (prevRight == OP_FALSE || prevRight == OP_NIL) {
			chunk_fallback(chunk, 1 + 1);//op + op
			opStack_fallback(opStack, 2);
			emitByte(cc, OP_TRUE);
			emitOpStack(cc, OP_TRUE, false);
		}
		else if (prevRight == OP_TRUE) {
			chunk_fallback(chunk, 1 + 1);//op + op
			opStack_fallback(opStack, 2);
			emitByte(cc, OP_FALSE);
			emitOpStack(cc, OP_FALSE, false);
		}
		else if (isRightLocal) {
			chunk_fallback(chunk, 1);//op
			clearOpStack(cc);
			CHUNK_PEEK(2) = OP_NOT_LOCAL; //convert command
		}
		break;
	}
	case OP_NEGATE: {
		if (isRightConstNumber) {
			//number constants only,always foldable
			LfxValue right = READ_LOCAL_CONSTANT(READ_SHORT_INDEX(1));

			double val = -AS_NUMBER(right);
			chunk_fallback(chunk, 1 + 3);//op + const
			opStack_fallback(opStack, 2);
			emitNumberConstant(cc, NUMBER_VAL(val));
		}
		else if (prevRight == OP_CONSTANT) {
			//non-number constant
			error(cc, "Operand must be a number.");
		}
		else if (prevRight == OP_FALSE || prevRight == OP_TRUE || prevRight == OP_NIL) {
			error(cc, "Operand must be a number.");
		}
		else if (isRightLocal) {
			chunk_fallback(chunk, 1);//op
			clearOpStack(cc);
			CHUNK_PEEK(2) = OP_NEGATE_LOCAL; //convert command
		}
		break;
	}
	case OP_EQUAL: {
		if ((isLeftConstant || isLeftLiteral) && (isRightConstant || isRightLiteral)) {
			LfxValue left = READ_OPERAND(prevLeft, 1 + OPERAND_SIZE(prevRight));
			LfxValue right = READ_OPERAND(prevRight, 1);

			bool val = valuesEqual(left, right);
			chunk_fallback(chunk, 1 + OPERAND_SIZE(prevLeft) + OPERAND_SIZE(prevRight));//op + const + const
			opStack_fallback(opStack, 3);
			emitByte(cc, val ? OP_TRUE : OP_FALSE);
			emitOpStack(cc, val ? OP_TRUE : OP_FALSE, false);
		}
		else if (isRightConstNumber) {
			//number constant -> super instruction,16bits index
			chunk_fallback(chunk, 1);//op
			clearOpStack(cc);
			CHUNK_PEEK(2) = OP_EQUAL_CONST_NUMBER; //convert command
		}
		else if (prevRight == OP_CONSTANT) {
			//non-number constant -> super instruction,24bits index
			chunk_fallback(chunk, 1);//op
			clearOpStack(cc);
			CHUNK_PEEK(3) = OP_EQUAL_CONST; //convert command
		}
		else if (isRightLocal) {
			chunk_fallback(chunk, 1);//op
			clearOpStack(cc);
			CHUNK_PEEK(2) = OP_EQUAL_LOCAL; //convert command
		}
		break;
	}
	case OP_NOT_EQUAL: {
		if ((isLeftConstant || isLeftLiteral) && (isRightConstant || isRightLiteral)) {
			LfxValue left = READ_OPERAND(prevLeft, 1 + OPERAND_SIZE(prevRight));
			LfxValue right = READ_OPERAND(prevRight, 1);

			bool val = !valuesEqual(left, right);
			chunk_fallback(chunk, 1 + OPERAND_SIZE(prevLeft) + OPERAND_SIZE(prevRight));//op + const + const
			opStack_fallback(opStack, 3);
			emitByte(cc, val ? OP_TRUE : OP_FALSE);
			emitOpStack(cc, val ? OP_TRUE : OP_FALSE, false);
		}
		else if (isRightConstNumber) {
			//number constant -> super instruction,16bits index
			chunk_fallback(chunk, 1);//op
			clearOpStack(cc);
			CHUNK_PEEK(2) = OP_NOT_EQUAL_CONST_NUMBER; //convert command
		}
		else if (prevRight == OP_CONSTANT) {
			//non-number constant -> super instruction,24bits index
			chunk_fallback(chunk, 1);//op
			clearOpStack(cc);
			CHUNK_PEEK(3) = OP_NOT_EQUAL_CONST; //convert command
		}
		else if (isRightLocal) {
			chunk_fallback(chunk, 1);//op
			clearOpStack(cc);
			CHUNK_PEEK(2) = OP_NOT_EQUAL_LOCAL; //convert command
		}
		break;
	}
	case OP_GREATER: {
		if (isBothConstant) {
			LfxValue left = READ_CONST_VALUE(isLeftConstNumber, 1 + CONST_SIZE(isRightConstNumber));
			LfxValue right = READ_CONST_VALUE(isRightConstNumber, 1);
			BINARY_CMP(left, right, > , CONST_SIZE(isLeftConstNumber), CONST_SIZE(isRightConstNumber));
		}
		else if (isRightConstNumber) {
			//number constant -> super instruction,16bits index
			chunk_fallback(chunk, 1);//op
			clearOpStack(cc);
			CHUNK_PEEK(2) = OP_GREATER_CONST; //convert command
		}
		//non-number constant : no super instruction for ordering comparisons
		else if (isRightLocal) {
			chunk_fallback(chunk, 1);//op
			clearOpStack(cc);
			CHUNK_PEEK(2) = OP_GREATER_LOCAL; //convert command
		}
		break;
	}
	case OP_LESS: {
		if (isBothConstant) {
			LfxValue left = READ_CONST_VALUE(isLeftConstNumber, 1 + CONST_SIZE(isRightConstNumber));
			LfxValue right = READ_CONST_VALUE(isRightConstNumber, 1);
			BINARY_CMP(left, right, < , CONST_SIZE(isLeftConstNumber), CONST_SIZE(isRightConstNumber));
		}
		else if (isRightConstNumber) {
			//number constant -> super instruction,16bits index
			chunk_fallback(chunk, 1);//op
			clearOpStack(cc);
			CHUNK_PEEK(2) = OP_LESS_CONST; //convert command
		}
		//non-number constant : no super instruction for ordering comparisons
		else if (isRightLocal) {
			chunk_fallback(chunk, 1);//op
			clearOpStack(cc);
			CHUNK_PEEK(2) = OP_LESS_LOCAL; //convert command
		}
		break;
	}
	case OP_LESS_EQUAL: {
		if (isBothConstant) {
			LfxValue left = READ_CONST_VALUE(isLeftConstNumber, 1 + CONST_SIZE(isRightConstNumber));
			LfxValue right = READ_CONST_VALUE(isRightConstNumber, 1);
			BINARY_CMP(left, right, <= , CONST_SIZE(isLeftConstNumber), CONST_SIZE(isRightConstNumber));
		}
		else if (isRightConstNumber) {
			//number constant -> super instruction,16bits index
			chunk_fallback(chunk, 1);//op
			clearOpStack(cc);
			CHUNK_PEEK(2) = OP_LESS_EQUAL_CONST; //convert command
		}
		//non-number constant : no super instruction for ordering comparisons
		else if (isRightLocal) {
			chunk_fallback(chunk, 1);//op
			clearOpStack(cc);
			CHUNK_PEEK(2) = OP_LESS_EQUAL_LOCAL; //convert command
		}
		break;
	}
	case OP_GREATER_EQUAL: {
		if (isBothConstant) {
			LfxValue left = READ_CONST_VALUE(isLeftConstNumber, 1 + CONST_SIZE(isRightConstNumber));
			LfxValue right = READ_CONST_VALUE(isRightConstNumber, 1);
			BINARY_CMP(left, right, >= , CONST_SIZE(isLeftConstNumber), CONST_SIZE(isRightConstNumber));
		}
		else if (isRightConstNumber) {
			//number constant -> super instruction,16bits index
			chunk_fallback(chunk, 1);//op
			clearOpStack(cc);
			CHUNK_PEEK(2) = OP_GREATER_EQUAL_CONST; //convert command
		}
		//non-number constant : no super instruction for ordering comparisons
		else if (isRightLocal) {
			chunk_fallback(chunk, 1);//op
			clearOpStack(cc);
			CHUNK_PEEK(2) = OP_GREATER_EQUAL_LOCAL; //convert command
		}
		break;
	}
	case OP_BITWISE: {
		if (isRightConstNumber) {
			uint8_t bitOp = CHUNK_PEEK(0);
			// no optimization for NOT, because it has only one operand
			if (bitOp == BIT_OP_NOT) {
				break;
			}

			uint32_t idx_right = READ_SHORT_INDEX(2); //op
			LfxValue right = READ_LOCAL_CONSTANT(idx_right);

			if (!IS_NUMBER(right)) {
				error(cc, "Operand of bitwise must be a number.");
				break;
			}

			double rightNum = AS_NUMBER(right);

			switch (bitOp)
			{
			case BIT_OP_AND: {
				uint32_t rightIMM = double_to_u32(rightNum);
				chunk_fallback(chunk, 2 + 3);//op + const
				emitBytes(cc, 6, OP_BITWISE, BIT_OP_ANDI, (uint8_t)rightIMM, (uint8_t)(rightIMM >> 8), (uint8_t)(rightIMM >> 16), (uint8_t)(rightIMM >> 24));
				clearOpStack(cc);
				break;
			}
			case BIT_OP_OR: {
				uint32_t rightIMM = double_to_u32(rightNum);
				chunk_fallback(chunk, 2 + 3);//op + const
				emitBytes(cc, 6, OP_BITWISE, BIT_OP_ORI, (uint8_t)rightIMM, (uint8_t)(rightIMM >> 8), (uint8_t)(rightIMM >> 16), (uint8_t)(rightIMM >> 24));
				clearOpStack(cc);
				break;
			}
			case BIT_OP_XOR: {
				uint32_t rightIMM = double_to_u32(rightNum);
				chunk_fallback(chunk, 2 + 3);//op + const
				emitBytes(cc, 6, OP_BITWISE, BIT_OP_XORI, (uint8_t)rightIMM, (uint8_t)(rightIMM >> 8), (uint8_t)(rightIMM >> 16), (uint8_t)(rightIMM >> 24));
				clearOpStack(cc);
				break;
			}
			case BIT_OP_SHL: {
				uint32_t shiftIMM = double_to_shift32(rightNum);
				chunk_fallback(chunk, 2 + 3);//op + const
				emitBytes(cc, 3, OP_BITWISE, BIT_OP_SHLI, (uint8_t)shiftIMM);
				clearOpStack(cc);
				break;
			}
			case BIT_OP_SHR: {
				if (rightNum < 0) {
					error(cc, "Operand of bitwise must be a positive.");
					break;
				}
				uint32_t shiftIMM = double_to_shift32(rightNum);
				chunk_fallback(chunk, 2 + 3);//op + const
				emitBytes(cc, 3, OP_BITWISE, BIT_OP_SHRI, (uint8_t)shiftIMM);
				clearOpStack(cc);
				break;
			}
			case BIT_OP_SAR: {
				if (rightNum < 0) {
					error(cc, "Operand of bitwise must be a positive.");
					break;
				}
				uint32_t shiftIMM = double_to_shift32(rightNum);
				chunk_fallback(chunk, 2 + 3);//op + const
				emitBytes(cc, 3, OP_BITWISE, BIT_OP_SARI, (uint8_t)shiftIMM);
				clearOpStack(cc);
				break;
			}
			default:
				fprintf(stderr, "Unexpected Bitwise(%u)\n", bitOp);
				break;
			}
		}
		else if (prevRight == OP_CONSTANT) {
			//non-number constant
			error(cc, "Operand of bitwise must be a number.");
		}
		break;
	}
	case OP_POP: {
		if (prevRight == OP_SET_LOCAL) {
			if (prevLeft == OP_GET_LOCAL) {
				uint32_t rightIndex = READ_SHORT_INDEX(1); // op pop
				chunk_fallback(chunk, 3 + 1); // set + pop
				CHUNK_PEEK(2) = OP_MOVE_LOCAL; // get
				emitBytes(cc, 2, (uint8_t)rightIndex, (uint8_t)(rightIndex >> 8));
				clearOpStack(cc);
			}
			else {
				chunk_fallback(chunk, 1);//pop
				CHUNK_PEEK(2) = OP_SET_LOCAL_POP; //convert command
				clearOpStack(cc);
			}
		}
		else if (prevRight == OP_SET_PROPERTY) {
			//set property + pop -> set property and pop
			//set property is now [op][name u24][slot u8] = 5 bytes, op sits at count-5
			chunk_fallback(chunk, 1);//pop
			CHUNK_PEEK(4) = OP_SET_PROPERTY_POP; //convert command
			clearOpStack(cc);
		}
		else if (prevRight == OP_SET_PROP_LOCAL) {
			//fused local set property + pop -> pop the assigned value only
			//instruction is [op][local u16][name u24][slot u8] = 7 bytes, op sits at count-7
			chunk_fallback(chunk, 1);//pop
			CHUNK_PEEK(6) = OP_SET_PROP_LOCAL_POP; //convert command
			clearOpStack(cc);
		}
		else if (prevRight == OP_GET_LOCAL) {
			//get local + pop -> dead code
			chunk_fallback(chunk, 3 + 1); //get_local(3) + pop
			opStack_fallback(opStack, 2);
		}
		else if (prevRight == OP_CONST_NUMBER) {
			//constant(number) + pop -> dead code
			chunk_fallback(chunk, 3 + 1); //const_number(3) + pop
			opStack_fallback(opStack, 2);
		}
		else if (prevRight == OP_CONSTANT) {
			//constant(non-number) + pop -> dead code
			chunk_fallback(chunk, 4 + 1); //constant(4) + pop
			opStack_fallback(opStack, 2);
		}
		else if (prevRight == OP_TRUE || prevRight == OP_FALSE || prevRight == OP_NIL) {
			//literal + pop -> dead code
			chunk_fallback(chunk, 1 + 1); //literal(1) + pop
			opStack_fallback(opStack, 2);
		}
		break;
	}
	case OP_SET_LOCAL: {
		break;
	}
	default: {
		fprintf(stderr, "Unexpected(%u)\n", code);
		break;
	}
	}

#undef CHUNK_PEEK
#undef READ_GLOBAL_CONSTANT
#undef READ_LOCAL_CONSTANT
#undef READ_SHORT_INDEX
#undef READ_24BITS_INDEX
#undef CONST_SIZE
#undef READ_INDEX
#undef READ_CONST_VALUE
#undef OPERAND_SIZE
#undef READ_OPERAND
#undef BINARY_CALC
#undef BINARY_CMP
}
#endif