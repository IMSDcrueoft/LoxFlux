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
#include "xoshiro256.h"

//the depth of call frames
#define LOXFLUX_FRAMES_INIT 64
#define LOXFLUX_FRAMES_MAX 1024
// stack in fiber uses va, avoid syscall too frequently
#define LOXFLUX_FIBER_CACHE_MAX 4

_Static_assert(LOXFLUX_FRAMES_INIT > 0 && LOXFLUX_FRAMES_INIT <= LOXFLUX_FRAMES_MAX, "Invalid frame limits.");
_Static_assert(((LOXFLUX_FRAMES_MAX / LOXFLUX_FRAMES_INIT) & ((LOXFLUX_FRAMES_MAX / LOXFLUX_FRAMES_INIT) - 1)) == 0, "The frameMax/frameInit ratio must be power of 2.");

//customed vm stack begin size
//#define LOXFLUX_STACK_INITIAL_SIZE 4096 //unused since the arena stack: initial footprint is one 64KB commit chunk (= the old 4096 Values)
#define LOXFLUX_STACK_MAX_SIZE (LOXFLUX_FRAMES_MAX * UINT8_COUNT) //the max stack size, if exceed this, we can be sure it's a stack overflow
//vm stack arena: VA reserved once and NEVER moved, so growing the stack never
//invalidates frame->slots or upvalue->location pointers (no re-fix pass needed);
//physical memory is committed lazily in ARENA_BUMP_COMMIT_CHUNK steps
#define LOXFLUX_STACK_ARENA_SIZE (LOXFLUX_STACK_MAX_SIZE * sizeof(LfxValue))

//vm stack arena: same fixed-VA model as the stack arena; 512MiB reservation,
//committed lazily in 16KB steps (a single oversized string commits more at once)
#if LOXFLUX_BUILD_64BIT
#define LOXFLUX_STRING_ARENA_SIZE ((size_t)512 << 20)
#else
#define LOXFLUX_STRING_ARENA_SIZE ((size_t)128 << 20)
#endif

//gc gray arena: backing storage for the gc gray worklist, same fixed-VA bump
//model as the stack arena; the worklist grows lazily in 64KB commit steps and
//the reservation never moves (safe for future concurrent tagging). 1GiB on
//64-bit, 256MiB on 32-bit (UINTPTR_MAX works across MSVC/GCC/Clang)
#if LOXFLUX_BUILD_64BIT
#define LOXFLUX_GRAY_ARENA_SIZE ((size_t)1 << 30)
#else
#define LOXFLUX_GRAY_ARENA_SIZE ((size_t)256 << 20)
#endif

typedef struct {
	//runtime --print-bytecode flag: vm-level setting (set once by main,
	//survives /reset), read by endCompiler to dump the final bytecode
	bool printBytecode;
	//runtime --no-optimize flag: when false, emitOpStack/instructionOptimize,
	//foldBuiltinMember and emitBranchJump all pass through unoptimized
	bool optimizationEnabled;
} LfxOptions;

typedef struct LfxVM {
	// runtime weak ref cache
	FiberCore cache;
	//currently executing fiber; owns the real frames/open-upvalue state
	ObjFiber* currentFiber;

	// deduplicated global constant table
	// erased slots become holes, silently reused by the next valueArray_write
	ValueArray constants;

	// bit-keyed dedup pool for native constants binary64 -> index into constants
	BinaryTable nativeConstants;

	//scripts
	StringTable scripts;
	//pool
	StringTable strings;

	//the root for dynamic objects
	Obj* objects;
	//the root for static objects
	Obj* objects_no_gc;

	//inline cache gc registry: functions whose invoke cache slots hold ObjClass*/ObjClosure*
	ObjFunction** icFuncs;
	uint32_t icFuncCount;
	uint32_t icFuncCapacity;

	//free ObjFiber cache; linked through the object header and not gc-scanned
	ObjFiber* fiberFreeList;
	size_t fiberFreeCount;

	//Excludes space used by stacks/constants/compilations
	//size_t (not uint64_t): matches the platform word size, so the 32-bit
	//build does all gc-threshold arithmetic in native-width registers
	size_t bytesAllocated_no_gc;
	size_t bytesAllocated;

	size_t beginGC;
	size_t nextGC;

	//gc gray worklist: plain count over the gray arena - same model as the vm
	//stack: the hot push path is one bounds compare against the committed edge,
	//not an arena call; capacity is grayArena.capacity / sizeof(Obj*), fixed
	Obj** grayStack;   //arena base (never moves)
	size_t grayCount;

	//Flip tagging, is more suitable for concurrent tagging
	uint8_t gcMark;
	//mark if the gc is running
	uint8_t gcWorking;

	// 1 = execution control has been ceded to the host
	uint8_t ceding;
	//pad
	uint8_t padding[1];
	//id for compiled functions
	uint32_t functionID;

	ObjString* initString;
	ObjString* typeStrings[TYPE_STRING_COUNT];

	//literal object and builtin object
	ObjClass defaultClass;

	//global hash table
	ObjInstance globals;
	ObjInstance builtins[MODULE_COUNT];

	//fixed-base backing storage for interned strings (append-only bump): all
	//ObjString allocations come from here and the whole arena is released at
	//vm exit; strings are NOT linked into the object lists anymore
	ArenaBumpAllocator stringArena;

	//fixed-base backing storage for the gc gray worklist (VA never moves,
	//lazy 64KB commits; only touched when the worklist crosses the committed edge)
	ArenaBumpAllocator grayArena;

	// rng
	Xoshiro256StarStar rng;

	// per vm options
	LfxOptions options;
} LfxVM;

typedef enum {
	INTERPRET_OK,
	INTERPRET_CEDE, // execution control returned to the host
	INTERPRET_COMPILE_ERROR,
	INTERPRET_RUNTIME_ERROR
} InterpretResult;

//the global shared vm extern to other file
extern LfxVM vm;

void vm_init(LfxVM* vm);
void vm_free(LfxVM* vm);
bool vm_isCeding(LfxVM* vm);
// Must be called when switching fibers or performing GC
void vm_storeCacheToFiber(LfxVM* vm);
void vm_registerIcFunction(LfxVM* vm, ObjFunction* function);

void vm_setPrintCodeEnabled(LfxVM* vm, bool isEnabled);
void vm_setOptimizationEnabled(LfxVM* vm, bool isEnabled);

void stack_push(LfxVM* vm, LfxValue value);
LfxValue stack_pop(LfxVM* vm);
void stack_replace(LfxVM* vm, LfxValue val);

//get the size of constants (including the holes)
uint32_t getConstantSize(LfxVM* vm);
//add a constant and get the index of it,i will handle it later
uint32_t addConstant(LfxValue value);

//the vm stack cache, interned strings and the gray worklist live in fixed-VA
//arenas outside the allocator. The current fiber owns its stack arena; its
//base/committed-edge are loaded into vm.cache.stack / vm.cache.stackBoundary
//while current.

//compile + prepare the top-level frame; on success the VM is ceding and the
//caller may release the source buffer before calling interpret_execute
bool interpret_compile(LfxVM* vm, C_STR source);
//run the prepared frame, or continue after INTERPRET_CEDE
InterpretResult interpret_execute(LfxVM* vm);
InterpretResult interpret_repl(LfxVM* vm, C_STR source);

//for builtin
void defineNative_math_number(LfxVM* vm, C_STR name, double value);
void defineNative_math(LfxVM* vm, C_STR name, LfxNativeFn function);
void defineNative_array(LfxVM* vm, C_STR name, LfxNativeFn function);
void defineNative_object(LfxVM* vm, C_STR name, LfxNativeFn function);
void defineNative_string(LfxVM* vm, C_STR name, LfxNativeFn function);
void defineNative_time(LfxVM* vm, C_STR name, LfxNativeFn function);
void defineNative_ctor(LfxVM* vm, C_STR name, LfxNativeFn function);
void defineNative_system(LfxVM* vm, C_STR name, LfxNativeFn function);
//for global
void defineNative_global(LfxVM* vm, C_STR name, LfxNativeFn function);