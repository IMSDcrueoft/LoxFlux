/*
 * MIT License
 * Copyright (c) 2025-2026 IMSDcrueoft (https://github.com/IMSDcrueoft)
 * See LICENSE file in the root directory for full license text.
*/
#pragma once
#include "chunk.h"
#include "compiler.h"
#include "table.h"
#include "object.h"
#include "nativeBuiltin.h"
#include "arena_bump.h"

//the depth of call frames
#define FRAMES_MAX 1024
//customed vm stack begin size
//#define STACK_INITIAL_SIZE 4096 //unused since the arena stack: initial footprint is one 64KB commit chunk (= the old 4096 Values)
#define STACK_MAX_SIZE (FRAMES_MAX * UINT8_COUNT) //the max stack size, if exceed this, we can be sure it's a stack overflow
//vm stack arena: VA reserved once and NEVER moved, so growing the stack never
//invalidates frame->slots or upvalue->location pointers (no re-fix pass needed);
//physical memory is committed lazily in ARENA_BUMP_COMMIT_CHUNK steps
#define STACK_ARENA_SIZE (STACK_MAX_SIZE * sizeof(Value))

//vm stack arena: same fixed-VA model as the stack arena; 512MiB reservation,
//committed lazily in 16KB steps (a single oversized string commits more at once)
#if LOXFLUX_BUILD_64BIT
#define STRING_ARENA_SIZE ((size_t)512 << 20)
#else
#define STRING_ARENA_SIZE ((size_t)128 << 20)
#endif

//gc gray arena: backing storage for the gc gray worklist, same fixed-VA bump
//model as the stack arena; the worklist grows lazily in 64KB commit steps and
//the reservation never moves (safe for future concurrent tagging). 1GiB on
//64-bit, 256MiB on 32-bit (UINTPTR_MAX works across MSVC/GCC/Clang)
#if LOXFLUX_BUILD_64BIT
#define GRAY_ARENA_SIZE ((size_t)1 << 30)
#else
#define GRAY_ARENA_SIZE ((size_t)256 << 20)
#endif

typedef struct {
	ObjClosure* closure;
	uint8_t* ip;
	Value* slots; //first avilable slot
	//ptrdiff_t slotsOffset;// offset of the first slot in the vm stack, not a ptr, so that we can realloc the stack when needed
	//kept as a comment for future use: the stack now lives at a fixed arena base
	//(ArenaBumpAllocator) that never moves, so frame pointers never go stale and
	//no offset bookkeeping is needed; restore this field if the stack becomes movable again
	InlineCacheSlot* caches;// IC slots of frame->closure->function, resolved once per call to avoid per-op deref chains
} CallFrame;

//frames live in a static array indexed by the dispatch loop; a silent size/layout
//shift here changes cache-line packing of hot frames and can move dispatch performance
//was 40 (with slotsOffset): the arena stack removed it, 32 packs tighter, but
//re-verify dispatch benchmarks before trusting hot-loop numbers

typedef struct {
	//a cache
	Value* stack;
	Value* stackTop;
	//the only push boundary: the committed edge of the stack arena; crossing it
	//means commit more pages or (at the VA end = stackArena.capacity) overflow
	Value* stackBoundary;

	// deduplicated global constant table
	// erased slots become holes, silently reused by the next valueArray_write
	ValueArray constants;

	//scripts
	StringTable scripts;
	//pool
	StringTable strings;

	//upvalues
	ObjUpvalue* openUpvalues;

	//global hash table
	ObjInstance globals;
	ObjInstance builtins[BUILTIN_MODULE_COUNT];

	//the root for dynamic objects
	Obj* objects;
	//the root for static objects
	Obj* objects_no_gc;

	//Excludes space used by stacks/constants/compilations
	//size_t (not uint64_t): matches the platform word size, so the 32-bit
	//build does all gc-threshold arithmetic in native-width registers
	size_t bytesAllocated_no_gc;
	size_t bytesAllocated;

	//gc gray worklist: plain count over the gray arena - same model as the vm
	//stack: the hot push path is one bounds compare against the committed edge,
	//not an arena call; capacity is grayArena.capacity / sizeof(Obj*), fixed
	Obj** grayStack;   //arena base (never moves)
	size_t grayCount;

	//Flip tagging, is more suitable for concurrent tagging
	uint8_t gcMark;
	//mark if the gc is running
	uint8_t gcWorking;
	//pad
	uint8_t padding[6];

	size_t beginGC;
	size_t nextGC;

	//literal object and builtin object
	ObjClass defaultClass;

	ObjString* initString;
	ObjString* typeStrings[TYPE_STRING_COUNT];

	//inline cache gc registry: functions whose invoke cache slots hold ObjClass*/ObjClosure*
	ObjFunction** icFuncs;
	uint32_t icFuncCount;
	uint32_t icFuncCapacity;

	//id for compiled functions
	uint32_t functionID;
	//frames
	uint32_t frameCount;
	CallFrame frames[FRAMES_MAX];

	//fixed-base backing storage for the stack (VA never moves, lazy commit)
	ArenaBumpAllocator stackArena;

	//fixed-base backing storage for interned strings (append-only bump): all
	//ObjString allocations come from here and the whole arena is released at
	//vm exit; strings are NOT linked into the object lists anymore
	ArenaBumpAllocator stringArena;

	//fixed-base backing storage for the gc gray worklist (VA never moves,
	//lazy 64KB commits; only touched when the worklist crosses the committed edge)
	ArenaBumpAllocator grayArena;
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
void vm_register_ic_function(ObjFunction* function);

void stack_push(Value value);
Value stack_pop();
void stack_replace(Value val);

//get the size of constants (including the holes)
uint32_t getConstantSize();
//add a constant and get the index of it,i will handle it later
uint32_t addConstant(Value value);

//the vm stack, interned strings and the gray worklist live in fixed-VA arenas
//outside the allocator: their committed/used sizes are read straight off the
//arena structs (vm.stackArena / vm.stringArena / vm.grayArena), the old
//single-line wrappers (stringBytes/stringCommittedBytes/stackCommittedBytes)
//are gone; pinnedBytes folds arenaBump_used(&vm.stringArena) into the no_gc book

InterpretResult interpret(C_STR source);
InterpretResult interpret_repl(C_STR source);

//for builtin
void defineNative_math_number(C_STR name, double value);
void defineNative_math(C_STR name, NativeFn function);
void defineNative_array(C_STR name, NativeFn function);
void defineNative_object(C_STR name, NativeFn function);
void defineNative_string(C_STR name, NativeFn function);
void defineNative_time(C_STR name, NativeFn function);
void defineNative_ctor(C_STR name, NativeFn function);
void defineNative_system(C_STR name, NativeFn function);
//for global
void defineNative_global(C_STR name, NativeFn function);