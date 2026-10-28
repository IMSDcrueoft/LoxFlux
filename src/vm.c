/*
 * MIT License
 * Copyright (c) 2025-2026 IMSDcrueoft (https://github.com/IMSDcrueoft)
 * See LICENSE file in the root directory for full license text.
*/
#include "vm.h"
#include "object.h"
#include "gc.h"
#include "file.h"
#include "allocator.h"
#include <assert.h>

#if DEBUG_TRACE_EXECUTION
#include "debug.h"
#endif

#if LOG_MODE
#include "timer.h"
#endif

//dual dispatch: the case-body goto labels are consumed by computed-goto builds;
//under MSVC switch dispatch they are intentionally unreferenced
#ifdef _MSC_VER
#pragma warning(disable : 4102)//unreferenced local label
#endif

//the global shared vm
LfxVM vm;

static void closeUpvalues(LfxVM* vm, LfxValue* last);

COLD_FUNCTION
static void stack_reset(LfxVM* vm)
{
	//close open upvalues before clearing the stack: discarded/ceded executions
	//must not leave escaped closures pointing into reset stack slots
	if (vm->cache.openUpvalues != NULL) {
		closeUpvalues(vm, vm->cache.stack);
	}

	//reset the pointer
	vm->cache.stackTop = vm->cache.stack;

	//set all to nil
	//the commit watermark only grows within one vm lifetime, so clearing up to
	//it covers every slot ever written; slots above it were never touched
	for (LfxValue* ptr = vm->cache.stack; ptr < vm->cache.stackBoundary; ++ptr) {
		*ptr = NIL_VAL;
	}

	vm->cache.frameCount = 0;
	vm->cache.openUpvalues = NULL;
	vm->cache.stackTop = vm->cache.stack;

	//the fiber's persistent state must follow the reset cache
	vm_storeCacheToFiber(vm);
}

COLD_FUNCTION
static ObjFunction* getCachedScript(LfxVM* vm, STR absolutePath) {
	// only to seek with absolute path
	ObjString* path = copyString(absolutePath, (uint32_t)strlen(absolutePath), false);

	StringEntry* entry = tableGetScriptEntry(&vm->scripts, path);
	ObjFunction* function = (entry != NULL) ? AS_FUNCTION(vm->constants.values[entry->index]) : NULL;

	//only add here, so no need check index
	if (function == NULL) {
		//this readFile function never return null
		STR source = readFile(absolutePath);

		function = compile(source, TYPE_MODULE);
		mem_free(source);

		if (function != NULL) {
			// add to pool
			tableSet_script(&vm->scripts, path, addConstant(OBJ_VAL(function)));
		}
	}

	return function;
}

COLD_FUNCTION
static bool throwError(LfxVM* vm, LfxValue error, C_STR format, ...) {
	fprintf(stderr, "[RuntimeError] ");

	va_list args;
	va_start(args, format);
	vfprintf(stderr, format, args);
	va_end(args);
	fputs("\n", stderr);

	for (int32_t i = vm->cache.frameCount - 1; i >= 0; i--) {
		CallFrame* frame = &vm->cache.frames[i];

		ObjFunction* function = frame->closure->function;
		size_t instruction = frame->ip - function->chunk.code - 1;

		uint32_t line = getLine(&function->chunk.lines, (uint32_t)instruction);

		fprintf(stderr, "[line %d] in ", line);
		if (function->name != NULL) {
			if (function->name->length != 0) {
				fprintf(stderr, "%s() : (%d)\n", function->name->chars, function->id);
			}
			else {
				fprintf(stderr, "<lambda>() : (%d)\n", function->id);
			}
		}
		else {
			fprintf(stderr, "<script> : (%d)\n", function->id);
		}
	}

	fprintf(stderr, "[ErrorInfo] ");
	printValue(stderr, error, false);
	fprintf(stderr, "\n");

	stack_reset(vm);
	return false;
}

COLD_FUNCTION
static void runtimeError(LfxVM* vm, C_STR format, ...) {
	fprintf(stderr, "[RuntimeError] ");

	va_list args;
	va_start(args, format);
	vfprintf(stderr, format, args);
	va_end(args);
	fputs("\n", stderr);

	for (int32_t i = vm->cache.frameCount - 1; i >= 0; i--) {
		CallFrame* frame = &vm->cache.frames[i];
		ObjFunction* function = frame->closure->function;
		size_t instruction = frame->ip - function->chunk.code - 1;

		uint32_t line = getLine(&function->chunk.lines, (uint32_t)instruction);

		fprintf(stderr, "[line %d] in ", line);
		if (function->name != NULL) {
			if (function->name->length != 0) {
				fprintf(stderr, "%s() : (%d)\n", function->name->chars, function->id);
			}
			else {
				fprintf(stderr, "<lambda>() : (%d)\n", function->id);
			}
		}
		else {
			fprintf(stderr, "<script> : (%d)\n", function->id);
		}
	}

	stack_reset(vm);
}

HOT_FUNCTION
void stack_push(LfxVM* vm, LfxValue value)
{
	*vm->cache.stackTop = value;

	if (++vm->cache.stackTop == vm->cache.stackBoundary) {
		//if (oldStack != vm->cache.stack) {
		//	// update all stack frames so it is safe now
		//	for (int32_t i = vm->cache.frameCount - 1; i >= 0; i--) {
		//		CallFrame* frame = &vm->cache.frames[i];
		//		frame->slots = vm->cache.stack + frame->slotsOffset;
		//	}
		//
		//	// update all upvalues if needed
		//	for (ObjUpvalue* upvalue = vm->cache.openUpvalues;upvalue != NULL;upvalue = upvalue->next) {
		//		if (upvalue->location_offset != CLOSED_OBJ_UPVALUE_LOCATION) {
		//			upvalue->location = vm->cache.stack + upvalue->location_offset;
		//		}
		//	}
		//}

		//VA reservation exhausted: the last slot was written, the stack is full
		size_t used = (size_t)((vm->cache.stackTop - vm->cache.stack) * sizeof(LfxValue));
		if (used >= vm->currentFiber->stackArena.capacity) {
			runtimeError(vm, "Stack overflow.");
			return;
		}

		//commit the next chunk lazily (must advance past stackTop, hence the
		//extra chunk); failure means the stack cannot grow -> overflow
		if (!arenaBump_ensureCommitted(&vm->currentFiber->stackArena, used + vm->currentFiber->stackArena.commitChunkSize)) {
			runtimeError(vm, "Stack overflow.");
			return;
		}

		//commitTop is a page-aligned byte offset; a page is a multiple of sizeof(LfxValue)
		vm->cache.stackBoundary = (LfxValue*)((char*)vm->currentFiber->stackArena.base + vm->currentFiber->stackArena.commitTop);
	}
}

HOT_FUNCTION
void stack_replace(LfxVM* vm, LfxValue val) {
	vm->cache.stackTop[-1] = val;
}

HOT_FUNCTION
LfxValue stack_pop(LfxVM* vm)
{
	vm->cache.stackTop--;
	return *vm->cache.stackTop;
}

#define STACK_PEEK(distance) (vm->cache.stackTop[-1 - distance])

COLD_FUNCTION
void defineNative_math_number(LfxVM* vm, C_STR name, double value) {
	tableSet(&vm->builtins[MODULE_MATH].fields,
		copyString(name, (uint32_t)strlen(name), false),
		NUMBER_VAL(value)
	);
}

COLD_FUNCTION
void defineNative_math(LfxVM* vm, C_STR name, LfxNativeFn function) {
	tableSet(&vm->builtins[MODULE_MATH].fields,
		copyString(name, (uint32_t)strlen(name), false),
		OBJ_VAL(newNative(function))
	);
}

COLD_FUNCTION
void defineNative_array(LfxVM* vm, C_STR name, LfxNativeFn function) {
	tableSet(&vm->builtins[MODULE_ARRAY].fields,
		copyString(name, (uint32_t)strlen(name), false),
		OBJ_VAL(newNative(function))
	);
}

COLD_FUNCTION
void defineNative_object(LfxVM* vm, C_STR name, LfxNativeFn function) {
	tableSet(&vm->builtins[MODULE_OBJECT].fields,
		copyString(name, (uint32_t)strlen(name), false),
		OBJ_VAL(newNative(function))
	);
}

COLD_FUNCTION
void defineNative_string(LfxVM* vm, C_STR name, LfxNativeFn function) {
	tableSet(&vm->builtins[MODULE_STRING].fields,
		copyString(name, (uint32_t)strlen(name), false),
		OBJ_VAL(newNative(function))
	);
}

COLD_FUNCTION
void defineNative_time(LfxVM* vm, C_STR name, LfxNativeFn function) {
	tableSet(&vm->builtins[MODULE_TIME].fields,
		copyString(name, (uint32_t)strlen(name), false),
		OBJ_VAL(newNative(function))
	);
}

COLD_FUNCTION
void defineNative_ctor(LfxVM* vm, C_STR name, LfxNativeFn function) {
	tableSet(&vm->builtins[MODULE_CTOR].fields,
		copyString(name, (uint32_t)strlen(name), false),
		OBJ_VAL(newNative(function))
	);
}

COLD_FUNCTION
void defineNative_system(LfxVM* vm, C_STR name, LfxNativeFn function) {
	tableSet(&vm->builtins[MODULE_SYSTEM].fields,
		copyString(name, (uint32_t)strlen(name), false),
		OBJ_VAL(newNative(function))
	);
}

COLD_FUNCTION
void defineNative_global(LfxVM* vm, C_STR name, LfxNativeFn function) {
	//stack_push(vm, OBJ_VAL(copyString(name, (uint32_t)strlen(name), false)));
	//stack_push(vm, OBJ_VAL(newNative(function)));
	//tableSet(&vm->globals.fields, AS_STRING(vm->cache.stack[0]), vm->cache.stack[1]);
	//stack_pop(vm);
	//stack_pop(vm);

	tableSet(&vm->globals.fields,
		copyString(name, (uint32_t)strlen(name), false),
		OBJ_VAL(newNative(function))
	);
}

COLD_FUNCTION
static void importBuiltins(LfxVM* vm) {
	for (uint32_t i = 0; i < MODULE_COUNT; ++i) {
		vm->builtins[i] = (ObjInstance){
		.obj = stateLess_obj_header(OBJ_INSTANCE),
		.klass = &vm->defaultClass,
		.fields = {.isGlobal = false,.isFrozen = true,.isEternal = true,.isPoisoned = false}//frozen from birth: tableSet is unguarded now, so filling members below works fine
		};
	}

	//init
	table_init(&vm->builtins[MODULE_MATH].fields);
	table_init(&vm->builtins[MODULE_ARRAY].fields);
	table_init(&vm->builtins[MODULE_OBJECT].fields);
	table_init(&vm->builtins[MODULE_STRING].fields);
	table_init(&vm->builtins[MODULE_TIME].fields);
	table_init(&vm->builtins[MODULE_CTOR].fields);
	table_init(&vm->builtins[MODULE_SYSTEM].fields);

	importNative_math(vm);
	importNative_array(vm);
	importNative_object(vm);
	importNative_string(vm);
	importNative_time(vm);
	importNative_ctor(vm);
	importNative_system(vm);
}

COLD_FUNCTION
static void removeBuiltins(LfxVM* vm) {
	for (uint32_t i = 0; i < MODULE_COUNT; ++i) {
		table_free(&vm->builtins[i].fields);
	}
}

COLD_FUNCTION
static void initTypeStrings(LfxVM* vm) {
	for (uint32_t i = 0; i < TYPE_STRING_COUNT; ++i) {
		vm->typeStrings[i] = NULL;
	}

	vm->typeStrings[TYPE_STRING_BOOL] = copyString("boolean", (uint32_t)strlen("boolean"), false);
	vm->typeStrings[TYPE_STRING_NIL] = copyString("nil", (uint32_t)strlen("nil"), false);
	vm->typeStrings[TYPE_STRING_NUMBER] = copyString("number", (uint32_t)strlen("number"), false);
	vm->typeStrings[TYPE_STRING_STRING] = copyString("string", (uint32_t)strlen("string"), false);
	vm->typeStrings[TYPE_STRING_STRING_BUILDER] = copyString("stringBuilder", (uint32_t)strlen("stringBuilder"), false);
	vm->typeStrings[TYPE_STRING_FUNCTION] = copyString("function", (uint32_t)strlen("function"), false);
	vm->typeStrings[TYPE_STRING_NATIVE] = copyString("native", (uint32_t)strlen("native"), false);
	vm->typeStrings[TYPE_STRING_CLASS] = copyString("class", (uint32_t)strlen("class"), false);
	vm->typeStrings[TYPE_STRING_OBJECT] = copyString("object", (uint32_t)strlen("object"), false);
	vm->typeStrings[TYPE_STRING_ITERATOR] = copyString("iterator", (uint32_t)strlen("iterator"), false);
	vm->typeStrings[TYPE_STRING_FIBER] = copyString("fiber", (uint32_t)strlen("fiber"), false);
	vm->typeStrings[TYPE_STRING_ARRAY] = copyString("array", (uint32_t)strlen("array"), false);
	vm->typeStrings[TYPE_STRING_ARRAY_F64] = copyString("array-f64", (uint32_t)strlen("array-f64"), false);
	vm->typeStrings[TYPE_STRING_ARRAY_F32] = copyString("array-f32", (uint32_t)strlen("array-f32"), false);
	vm->typeStrings[TYPE_STRING_ARRAY_U32] = copyString("array-u32", (uint32_t)strlen("array-u32"), false);
	vm->typeStrings[TYPE_STRING_ARRAY_I32] = copyString("array-i32", (uint32_t)strlen("array-i32"), false);
	vm->typeStrings[TYPE_STRING_ARRAY_U16] = copyString("array-u16", (uint32_t)strlen("array-u16"), false);
	vm->typeStrings[TYPE_STRING_ARRAY_I16] = copyString("array-i16", (uint32_t)strlen("array-i16"), false);
	vm->typeStrings[TYPE_STRING_ARRAY_U8] = copyString("array-u8", (uint32_t)strlen("array-u8"), false);
	vm->typeStrings[TYPE_STRING_ARRAY_I8] = copyString("array-i8", (uint32_t)strlen("array-i8"), false);
}

HOT_FUNCTION
static void vm_loadFiberToCache(LfxVM* vm) {
	if (vm->currentFiber != NULL) {
		vm->cache = vm->currentFiber->core;
	}
}

HOT_FUNCTION
void vm_storeCacheToFiber(LfxVM* vm) {
	//the VM stack top is an execution cache; persist it before tracing so
	//the current fiber's live stack prefix is complete
	if (vm->currentFiber != NULL) {
		vm->currentFiber->core = vm->cache;
	}
}

COLD_FUNCTION
void vm_init(LfxVM* vm)
{
	//zero the accounting books BEFORE any allocation: everything vm_init
	//allocates from here on must be counted, so the matching vm_free frees net
	//the counters back to exactly zero
	//(each fiber stack and the interned strings live in fixed OS VA arenas
	//outside the allocator, like the grayStack)
	vm->bytesAllocated = 0;
	vm->bytesAllocated_no_gc = 0;

	vm->currentFiber = NULL;
	vm->fiberFreeList = NULL;
	vm->fiberFreeCount = 0;

	vm->cache = (FiberCore){0};

	//init global
	valueArray_init(&vm->constants);
	binaryTable_init(&vm->nativeConstants);

	//string arena: reserve 512MiB of VA (committed lazily in 16KB steps); no
	//eager commit needed - the first string allocation triggers it; strings
	//are NOT counted in the accounting books anymore (same class as fiber
	//stack arenas and the grayStack)
	assert(vm->stringArena.base == NULL);
	if (!arenaBump_init(&vm->stringArena, LOXFLUX_STRING_ARENA_SIZE, false, 8, 16 << 10)) {
		fprintf(stderr, "Failed to reserve the vm string arena.\n");
		exit(1);
	}

	//gray arena: reserve VA once, no eager commit - the first gray push
	//crosses the committed edge (commitTop == 0) and commits the first 64KB
	//chunk lazily, same lazy-growth model as the vm stack
	assert(vm->grayArena.base == NULL);
	if (!arenaBump_init(&vm->grayArena, LOXFLUX_GRAY_ARENA_SIZE, false, 0, 0)) {
		fprintf(stderr, "Failed to reserve the vm gray arena.\n");
		exit(1);
	}

	//init gray stack (fixed base over the gray arena)
	vm->grayStack = (Obj**)vm->grayArena.base;
	vm->grayCount = 0;

	// init global 
	vm->globals = (ObjInstance){
		.obj = stateLess_obj_header(OBJ_INSTANCE),
		.klass = NULL,
		.fields = {.isGlobal = true,.isFrozen = false,.isEternal = false,.isPoisoned = false}
	};

	table_init(&vm->globals.fields);

	stringTable_init(&vm->scripts);
	stringTable_init(&vm->strings);

	vm->objects = NULL;
	vm->objects_no_gc = NULL;

	vm->functionID = 0;
	vm->nextGC = LOXFLUX_GC_HEAP_BEGIN;
	vm->beginGC = LOXFLUX_GC_HEAP_BEGIN;
	vm->gcMark = true;
	vm->gcWorking = false;

	//the root fiber owns the main VM stack and the live frame/open-upvalue state
	vm->currentFiber = newFiber();

	//commit the first stack chunk eagerly; later growth is lazy
	if (!arenaBump_ensureCommitted(&vm->currentFiber->stackArena, vm->currentFiber->stackArena.commitChunkSize)) {
		fprintf(stderr, "Failed to commit the vm stack arena.\n");
		exit(1);
	}

	//the fiber's persistent stack cache must be initialized before the hot cache
	//loads it; stack_reset(vm) persists the reset state back into the fiber
	vm->currentFiber->core.stackBoundary = (LfxValue*)((char*)vm->currentFiber->stackArena.base + vm->currentFiber->stackArena.commitTop);
	vm_loadFiberToCache(vm);

	stack_reset(vm);

	//the default class must exist before any builtin structure: every
	//instance that has no real class (builtin modules, the global object)
	//points at it, so the klass field is never NULL anywhere in the VM
	vm->defaultClass = (ObjClass){
		.obj = stateLess_obj_header(OBJ_CLASS),
		.name = copyString("<object>", (uint32_t)strlen("<object>"), false),
		.initializer = NULL,
		.superclass = NULL
	};
	table_init(&vm->defaultClass.methods);

	//the global object joins the default class as well
	vm->globals.klass = &vm->defaultClass;

	//import the builtins
	importBuiltins(vm);

	//import native funcs
	importNative_global(vm);

	vm->initString = NULL;
	vm->initString = copyString("init", (uint32_t)strlen("init"), false);
	initTypeStrings(vm);

	vm->ceding = 0;
}

COLD_FUNCTION
bool vm_isCeding(LfxVM* vm) {
	if (!vm->ceding) return false;
	if (vm->currentFiber == NULL) return false;
	if (vm->grayStack == NULL) return false;
	return vm->cache.frameCount > 0;
}

COLD_FUNCTION
void vm_registerIcFunction(ObjFunction* function) {
	if (vm.icFuncCapacity < vm.icFuncCount + 1) {
		uint32_t capacity = GROW_CAPACITY(vm.icFuncCapacity);
		vm.icFuncs = GROW_ARRAY_NO_GC(ObjFunction*, vm.icFuncs, vm.icFuncCapacity, capacity);
		vm.icFuncCapacity = capacity;
	}
	vm.icFuncs[vm.icFuncCount++] = function;
}

COLD_FUNCTION
void vm_free(LfxVM* vm)
{
	//cache arrays are owned by their ObjFunction and released by freeObject

	FREE_ARRAY_NO_GC(ObjFunction*, vm->icFuncs, vm->icFuncCapacity);
	vm->icFuncs = NULL;
	vm->icFuncCount = 0;
	vm->icFuncCapacity = 0;

	valueArray_free(&vm->constants);
	binaryTable_free(&vm->nativeConstants);

	table_free(&vm->globals.fields);
	stringTable_free(&vm->scripts);
	stringTable_free(&vm->strings);

	vm->initString = NULL;
	for (uint32_t i = 0; i < TYPE_STRING_COUNT; ++i) {
		vm->typeStrings[i] = NULL;
	}

	//root fiber is in vm->objects and is released/recycled by freeObjects();
	//persist the live execution cache into the fiber first: vm->cache.frames may
	//have been reallocated (grown) since the last store, so freeing the fiber
	//with a stale frames pointer would double-free the old block
	vm_storeCacheToFiber(vm);
	vm->currentFiber = NULL;
	vm->cache = (FiberCore){0};

	freeObjects();
	freeFiberFreeList();

	//release the string + gray arenas (VA reservations + committed pages)
	arenaBump_destroy(&vm->stringArena);
	arenaBump_destroy(&vm->grayArena);
	//any surviving bump reservation is a leak (e.g. an init without a matching destroy)
	assert(arenaBump_liveSegments() == 0);
	vm->grayStack = NULL;
	vm->grayCount = 0;

	removeBuiltins(vm);

	table_free(&vm->defaultClass.methods);

	vm->ceding = 0;
#if DEBUG_MODE
	//leak accounting: every allocate/free pair runs through reallocate/reallocate_no_gc,
	//so after the FULL teardown (objects, builtin tables, defaultClass) both
	//counters must net to exactly zero
	//(direct mem_alloc/mem_free users: readFile buffers, REPL line buffer,
	//fiber stack arenas, are uncounted on BOTH sides and therefore never
	//show up here)
	if (vm->bytesAllocated != 0 || vm->bytesAllocated_no_gc != 0) {
		fprintf(stderr, "[mem] LEAK: bytesAllocated=%zu bytesAllocated_no_gc=%zu (both must be 0)\n",
			vm->bytesAllocated, vm->bytesAllocated_no_gc);
	}
#endif
}

uint32_t getConstantSize(LfxVM* vm)
{
	return vm->constants.count;
}

//return the constant index
uint32_t addConstant(LfxValue value)
{
	stack_push(&vm, value);//prevent GC errors
	uint32_t index = valueArray_write(&vm.constants, value);
	stack_pop(&vm);
	return index;
}

static void getTypeof(LfxVM* vm) {
	LfxValue val = vm->cache.stackTop[-1];

	if (IS_NUMBER(val)) {
		stack_replace(vm, OBJ_VAL(vm->typeStrings[TYPE_STRING_NUMBER]));
		return;
	}
	else if (IS_BOOL(val)) {
		stack_replace(vm, OBJ_VAL(vm->typeStrings[TYPE_STRING_BOOL]));
		return;
	}
	else if (IS_NIL(val)) {
		stack_replace(vm, OBJ_VAL(vm->typeStrings[TYPE_STRING_NIL]));
		return;
	}
	else {
		switch (OBJ_TYPE(val)) {
		case OBJ_STRING:
			stack_replace(vm, OBJ_VAL(vm->typeStrings[TYPE_STRING_STRING]));
			return;
		case OBJ_STRING_BUILDER:
			stack_replace(vm, OBJ_VAL(vm->typeStrings[TYPE_STRING_STRING_BUILDER]));
			return;
		case OBJ_CLOSURE:
		case OBJ_BOUND_METHOD:
			stack_replace(vm, OBJ_VAL(vm->typeStrings[TYPE_STRING_FUNCTION]));
			return;
		case OBJ_NATIVE:
			stack_replace(vm, OBJ_VAL(vm->typeStrings[TYPE_STRING_NATIVE]));
			return;
		case OBJ_CLASS:
			stack_replace(vm, OBJ_VAL(vm->typeStrings[TYPE_STRING_CLASS]));
			return;
		case OBJ_INSTANCE:
			stack_replace(vm, OBJ_VAL(vm->typeStrings[TYPE_STRING_OBJECT]));
			return;
		case OBJ_ITERATOR:
			stack_replace(vm, OBJ_VAL(vm->typeStrings[TYPE_STRING_ITERATOR]));
			return;
		case OBJ_FIBER:
			stack_replace(vm, OBJ_VAL(vm->typeStrings[TYPE_STRING_FIBER]));
			return;
		case OBJ_ARRAY:
			stack_replace(vm, OBJ_VAL(vm->typeStrings[TYPE_STRING_ARRAY]));
			return;
		case OBJ_ARRAY_F64:
			stack_replace(vm, OBJ_VAL(vm->typeStrings[TYPE_STRING_ARRAY_F64]));
			return;
		case OBJ_ARRAY_F32:
			stack_replace(vm, OBJ_VAL(vm->typeStrings[TYPE_STRING_ARRAY_F32]));
			return;
		case OBJ_ARRAY_U32:
			stack_replace(vm, OBJ_VAL(vm->typeStrings[TYPE_STRING_ARRAY_U32]));
			return;
		case OBJ_ARRAY_I32:
			stack_replace(vm, OBJ_VAL(vm->typeStrings[TYPE_STRING_ARRAY_I32]));
			return;
		case OBJ_ARRAY_U16:
			stack_replace(vm, OBJ_VAL(vm->typeStrings[TYPE_STRING_ARRAY_U16]));
			return;
		case OBJ_ARRAY_I16:
			stack_replace(vm, OBJ_VAL(vm->typeStrings[TYPE_STRING_ARRAY_I16]));
			return;
		case OBJ_ARRAY_U8:
			stack_replace(vm, OBJ_VAL(vm->typeStrings[TYPE_STRING_ARRAY_U8]));
			return;
		case OBJ_ARRAY_I8:
			stack_replace(vm, OBJ_VAL(vm->typeStrings[TYPE_STRING_ARRAY_I8]));
			return;
		}
	}

	stack_replace(vm, NIL_VAL);
}

HOT_FUNCTION
static bool call(LfxVM* vm, ObjClosure* closure, int argCount) {
	//if (argCount > closure->function->arity) {
	//	runtimeError(vm, "Expected %d arguments but got %d.",
	//		closure->function->arity, argCount);
	//	return false;
	//}

	// check stack
	if (vm->cache.frameCount == vm->cache.frameCapacity) {
		if (vm->cache.frameCapacity >= LOXFLUX_FRAMES_MAX) {
			runtimeError(vm, "Stack overflow.");
			return false;
		}

		//LOXFLUX_FRAMES_INIT / LOXFLUX_FRAMES_MAX are powers of two, so GROW_CAPACITY grows
		//64 -> 128 -> 256 -> 512 -> 1024 and stops exactly at LOXFLUX_FRAMES_MAX
		uint32_t newCapacity = vm->cache.frameCapacity * 2;
		assert((newCapacity <= LOXFLUX_FRAMES_MAX) && "Unexpected frame size overflow.");

		//no-GC accounting: frames are VM bookkeeping, not collectible values
		vm->cache.frames = GROW_ARRAY_NO_GC(CallFrame, vm->cache.frames, vm->cache.frameCapacity, newCapacity);
		vm->cache.frameCapacity = newCapacity;
	}

	// append missing args
	while (argCount < closure->function->arity) {
		stack_push(vm, NIL_VAL);
		++argCount;
	}

	CallFrame* frame = &vm->cache.frames[vm->cache.frameCount++];// add after call
	frame->closure = closure;
	frame->ip = closure->function->chunk.code;
	//-1 is for function itself
	frame->slots = vm->cache.stackTop - argCount - 1;
	//frame->slotsOffset = (ptrdiff_t)(frame->slots - vm->cache.stack); //unused since the arena stack (fixed base)
	frame->caches = closure->function->caches;

	// clear extra args
	if (argCount > closure->function->arity) {
		for (int32_t i = 0, len = argCount - closure->function->arity; i < len; ++i) {
			STACK_PEEK(i) = NIL_VAL;
		}
	}

	return true;
}

HOT_FUNCTION
static bool call_native(LfxVM* vm, LfxNativeFn native, int argCount) {
	LfxValue* stackTop = vm->cache.stackTop - argCount;//store top, we don't know if native push stack (avoiding gc)
	LfxValue result = native(vm, argCount, stackTop);
	vm->cache.stackTop = stackTop;//restore the top
	stack_replace(vm, result);
	return true;
}

static inline void defineMethod(LfxVM* vm, ObjString* name) {
	LfxValue method = vm->cache.stackTop[-1];
	ObjClass* klass = AS_CLASS(vm->cache.stackTop[-2]);

	tableSet(&klass->methods, name, method);
	if (name == vm->initString) {//inline cache
		assert(IS_CLOSURE(method) && "The initializer must be a closure.");

		klass->initializer = AS_CLOSURE(method);
	}
	vm->cache.stackTop--;
}

HOT_FUNCTION
static bool callValue(LfxVM* vm, LfxValue callee, int argCount) {
	if (IS_OBJ(callee)) {
		switch (OBJ_TYPE(callee)) {
		case OBJ_CLOSURE: return call(vm, AS_CLOSURE(callee), argCount);
		case OBJ_NATIVE: return call_native(vm, AS_NATIVE(callee), argCount);
		case OBJ_BOUND_METHOD: {
			ObjBoundMethod* bound = AS_BOUND_METHOD(callee);
			STACK_PEEK(argCount) = bound->receiver;//bind 'this' to logic slot[0]
			return call(vm, bound->method, argCount);
		}
		case OBJ_CLASS: {
			ObjClass* klass = AS_CLASS(callee);
			STACK_PEEK(argCount) = OBJ_VAL(newInstance(klass));

			//call init with fast path
			if (klass->initializer != NULL) {
				return call(vm, klass->initializer, argCount);
			}
			else if (argCount != 0) {
				runtimeError(vm, "Expected 0 arguments for initializer but got %d.", argCount);
				return false;
			}
			return true;
		}
		}
	}

	runtimeError(vm, "Can only call functions and classes.");
	return false;
}

HOT_FUNCTION
static inline bool invokeFromClass(LfxVM* vm, ObjClass* klass, ObjString* name, int argCount) {
	LfxValue method;
	if (!tableGet(&klass->methods, name, &method)) {
		runtimeError(vm, "Undefined property '%s'.", name->chars);
		return false;
	}
	return call(vm, AS_CLOSURE(method), argCount);
}

//HOT_FUNCTION
//static inline bool invoke(ObjString* name, int argCount) {
//	LfxValue receiver = STACK_PEEK(argCount);
//	if (!IS_INSTANCE(receiver)) {
//		runtimeError(vm, "Only instances have methods.");
//		return false;
//	}
//	ObjInstance* instance = AS_INSTANCE(receiver);
//
//	LfxValue value;
//	if (tableGet(&instance->fields, name, &value)) {
//		STACK_PEEK(argCount) = value;
//		return callValue(vm, value, argCount);
//	}
//
//	return invokeFromClass(vm, instance->klass, name, argCount);
//}

HOT_FUNCTION
static void bindMethod(LfxVM* vm, ObjClass* klass, ObjString* name) {
	LfxValue method;
	if (tableGet(&klass->methods, name, &method)) {
		ObjBoundMethod* bound = newBoundMethod(vm->cache.stackTop[-1], AS_CLOSURE(method));
		stack_replace(vm, OBJ_VAL(bound));
	}
	else {
		//don't throw
		//runtimeError(vm, "Undefined property '%s'.", name->chars);
		stack_replace(vm, NIL_VAL);
	}
}

HOT_FUNCTION
static ObjUpvalue* captureUpvalue(LfxVM* vm, LfxValue* local) {
	ObjUpvalue* prevUpvalue = NULL;
	ObjUpvalue* upvalue = vm->cache.openUpvalues;

	//compare stack ptr(search from deepest,so [next] is upper)
	while (upvalue != NULL && upvalue->location > local) {
		prevUpvalue = upvalue;
		upvalue = upvalue->next;
	}

	if (upvalue != NULL && upvalue->location == local) {
		return upvalue;
	}

	//ObjUpvalue* createdUpvalue = newUpvalue(local, (ptrdiff_t)(local - vm->cache.stack)); //offset unused since the arena stack
	ObjUpvalue* createdUpvalue = newUpvalue(local);

	//insert it
	createdUpvalue->next = upvalue;
	if (prevUpvalue == NULL) {
		vm->cache.openUpvalues = createdUpvalue;
	}
	else {
		prevUpvalue->next = createdUpvalue;
	}

	return createdUpvalue;
}

HOT_FUNCTION
static void closeUpvalues(LfxVM* vm, LfxValue* last) {
	while (vm->cache.openUpvalues != NULL && vm->cache.openUpvalues->location >= last) {
		ObjUpvalue* upvalue = vm->cache.openUpvalues;

		//if one upValue closed,it's location is it's closed's pointer
		upvalue->closed = *upvalue->location;
		upvalue->location = &upvalue->closed;
		//upvalue->location_offset = CLOSED_OBJ_UPVALUE_LOCATION; //unused since the arena stack
		vm->cache.openUpvalues = upvalue->next;
	}
}

HOT_FUNCTION
static inline bool isFalsey(LfxValue value) {
	return IS_NIL(value) || (IS_BOOL(value) && !AS_BOOL(value));
}

HOT_FUNCTION
static inline bool isTruthy(LfxValue value) {
	return !IS_NIL(value) && (!IS_BOOL(value) || AS_BOOL(value));
}

HOT_FUNCTION
//returns the advanced ip on success,NULL on failure (no operand bytes are
//consumed on failure paths, so no rollback is needed)
static uint8_t* bitInstruction(LfxVM* vm, uint8_t bitOpType, uint8_t* ip) {
#define READ_BYTE() (*(ip)++)
#define READ_WORD() (ip += 4, (uint32_t)((ip[-4]) | ((uint32_t)ip[-3] << 8) | ((uint32_t)ip[-2] << 16) | ((uint32_t)ip[-1] << 24)))

#define BINARAY_OP_BIT(op)																			\
    do {																							\
		/* both operands must be numbers */													\
		if (IS_NUMBER(vm->cache.stackTop[-2]) && IS_NUMBER(vm->cache.stackTop[-1])) {								\
			double left = AS_NUMBER(vm->cache.stackTop[-2]);												\
			double right = AS_NUMBER(vm->cache.stackTop[-1]);												\
			/* compute the result in place */									\
			vm->cache.stackTop[-2] = NUMBER_VAL(double_to_i32(left) op double_to_i32(right));				\
			vm->cache.stackTop--;																			\
			return ip;																				\
		}																							\
	} while (false)

#define BINARAY_OP_BIT_IMM(op)																\
    do {																					\
		/* both operands must be numbers */											\
		if (IS_NUMBER(vm->cache.stackTop[-1])) {													\
			int32_t constant = READ_WORD();													\
			double left = AS_NUMBER(vm->cache.stackTop[-1]);										\
			/* compute the result in place */							\
			vm->cache.stackTop[-1] = NUMBER_VAL(double_to_i32(left) op constant);					\
			return ip;																		\
		}																					\
	} while (false)

#if COMPUTE_GOTO
	static void* bit_op_labels[] = {
	 [BIT_OP_NOT] = && label_bit_not,
	 [BIT_OP_AND] = && label_bit_and,
	 [BIT_OP_OR] = && label_bit_or,
	 [BIT_OP_XOR] = && label_bit_xor,
	 [BIT_OP_SHL] = && label_bit_shl,
	 [BIT_OP_SAR] = && label_bit_sar,
	 [BIT_OP_SHR] = && label_bit_shr,

	 [BIT_OP_ANDI] = && label_bit_andi,
	 [BIT_OP_ORI] = && label_bit_ori,
	 [BIT_OP_XORI] = && label_bit_xori,
	 [BIT_OP_SHLI] = && label_bit_shli,
	 [BIT_OP_SARI] = && label_bit_sari,
	 [BIT_OP_SHRI] = && label_bit_shri,
	};

	goto* bit_op_labels[bitOpType];
#endif

	switch (bitOpType)
	{
	case BIT_OP_NOT: {
	label_bit_not:
		if (IS_NUMBER(vm->cache.stackTop[-1])) {
			double operand = AS_NUMBER(vm->cache.stackTop[-1]);
			vm->cache.stackTop[-1] = NUMBER_VAL(~double_to_i32(operand));
			return ip;
		}
		break;
	}
	case BIT_OP_AND: {
	label_bit_and:
		BINARAY_OP_BIT(&);
		break;
	}
	case BIT_OP_OR: {
	label_bit_or:
		BINARAY_OP_BIT(| );
		break;
	}
	case BIT_OP_XOR: {
	label_bit_xor:
		BINARAY_OP_BIT(^);
		break;
	}

	case BIT_OP_SHL: {
	label_bit_shl:
		if (IS_NUMBER(vm->cache.stackTop[-2]) && IS_NUMBER(vm->cache.stackTop[-1])) {
			double left = AS_NUMBER(vm->cache.stackTop[-2]);
			double shift = AS_NUMBER(vm->cache.stackTop[-1]);
			//JS semantics: ToInt32(left) << (ToUint32(shift) & 31), result read as int32
			uint32_t result = (uint32_t)double_to_i32(left) << double_to_shift32(shift);
			vm->cache.stackTop[-2] = NUMBER_VAL((int32_t)result);
			vm->cache.stackTop--;
			return ip;
		}
		break;
	}
	case BIT_OP_SAR: {
	label_bit_sar:
		if (IS_NUMBER(vm->cache.stackTop[-2]) && IS_NUMBER(vm->cache.stackTop[-1])) {
			double left = AS_NUMBER(vm->cache.stackTop[-2]);
			double shift = AS_NUMBER(vm->cache.stackTop[-1]);

			//NaN is not negative: it takes the shift-0 path, matching the constant path
			if (!(shift < 0.0)) {
				vm->cache.stackTop[-2] = NUMBER_VAL(double_to_i32(left) >> double_to_shift32(shift));
				vm->cache.stackTop--;
			}
			else {
				vm->cache.stackTop[-2] = NUMBER_VAL(0);
				vm->cache.stackTop--;
			}
			return ip;
		}
		break;
	}
	case BIT_OP_SHR: {
	label_bit_shr:
		if (IS_NUMBER(vm->cache.stackTop[-2]) && IS_NUMBER(vm->cache.stackTop[-1])) {
			double left = AS_NUMBER(vm->cache.stackTop[-2]);
			double shift = AS_NUMBER(vm->cache.stackTop[-1]);

			//NaN is not negative: it takes the shift-0 path, matching the constant path
			if (!(shift < 0.0)) {
				vm->cache.stackTop[-2] = NUMBER_VAL(double_to_u32(left) >> double_to_shift32(shift));
				vm->cache.stackTop--;
			}
			else {
				vm->cache.stackTop[-2] = NUMBER_VAL(0);
				vm->cache.stackTop--;
			}
			return ip;
		}
		break;
	}
	case BIT_OP_ANDI: {
	label_bit_andi:
		BINARAY_OP_BIT_IMM(&);
		break;
	}
	case BIT_OP_ORI: {
	label_bit_ori:
		BINARAY_OP_BIT_IMM(| );
		break;
	}
	case BIT_OP_XORI: {
	label_bit_xori:
		BINARAY_OP_BIT_IMM(^);
		break;
	}
	case BIT_OP_SHLI: {
	label_bit_shli:
		if (IS_NUMBER(vm->cache.stackTop[-1])) {
			int32_t shiftBits = READ_BYTE();
			double left = AS_NUMBER(vm->cache.stackTop[-1]);
			uint32_t result = (uint32_t)double_to_i32(left) << shiftBits;
			vm->cache.stackTop[-1] = NUMBER_VAL((int32_t)result);
			return ip;
		}
		break;
	}
	case BIT_OP_SARI: {
	label_bit_sari:
		if (IS_NUMBER(vm->cache.stackTop[-1])) {
			int32_t shiftBits = READ_BYTE();
			double left = AS_NUMBER(vm->cache.stackTop[-1]);
			vm->cache.stackTop[-1] = NUMBER_VAL(double_to_i32(left) >> shiftBits);
			return ip;
		}
		break;
	}
	case BIT_OP_SHRI: {
	label_bit_shri:
		if (IS_NUMBER(vm->cache.stackTop[-1])) {
			int32_t shiftBits = READ_BYTE();
			double left = AS_NUMBER(vm->cache.stackTop[-1]);
			vm->cache.stackTop[-1] = NUMBER_VAL(double_to_u32(left) >> shiftBits);
			return ip;
		}
		break;
	}
	}
	return NULL;
#undef BINARAY_OP_BIT
#undef BINARAY_OP_BIT_IMM
#undef READ_BYTE
#undef READ_WORD
}

//IC slow paths: extracted from the dispatch handlers to keep run(vm)'s code small
//(cold + noinline so a single call site cannot be inlined back into the hot dispatch loop)

COLD_FUNCTION
NOINLINE_FUNCTION
static void icGetPropertySlow(LfxVM* vm, ObjInstance* instance, ObjString* name, uint8_t slot, InlineCacheSlot* icCache) {
	LfxValue value;
	Entry* entry;
	if (tableGetEntry(&instance->fields, name, &value, &entry)) {
		//backfill cache
		if (slot != 0 && instance->fields.capacity <= UINT16_MAX) {
			icCache[slot].capacity = instance->fields.capacity;
			icCache[slot].index = (uint32_t)(entry - instance->fields.entries);
		}
		stack_replace(vm, value);
		return;
	}
	//method fallback:builtin modules point at the default class with an empty
	//method table,so an unknown property reads nil for every instance alike
	bindMethod(vm, instance->klass, name);
}

COLD_FUNCTION
NOINLINE_FUNCTION
static void icSetPropertySlow(ObjInstance* instance, ObjString* name, LfxValue newValue, uint8_t slot, InlineCacheSlot* icCache) {
	//frozen instances reject every write/delete; early return keeps the IC
	//un-backfilled so later writes keep taking this cheap exit
	if (instance->fields.isFrozen) return;

	if (NOT_NIL(newValue)) {
		LfxValue oldValue;
		Entry* entry;
		if (tableGetEntry(&instance->fields, name, &oldValue, &entry)) {
			//existing field: single-probe direct write + backfill
			entry->value = newValue;
			if (slot != 0 && instance->fields.capacity <= UINT16_MAX) {
				icCache[slot].capacity = instance->fields.capacity;
				icCache[slot].index = (uint32_t)(entry - instance->fields.entries);
			}
		}
		else {
			//new field: insert (cold path, may rehash)
			tableSet(&instance->fields, name, newValue);
			//shadow check: a NEW field name colliding with a method name poisons this instance
			//memoized per call site in the slot's spare pointers:the property name is a compile-time
			//constant,so for one klass the answer never changes (methods are immutable once instances
			//exist,same assumption as the invoke cache)
			//extraA = klass guard,extraB = NULL (no shadow) or the shadowing method closure
			if (slot != 0 && (ObjClass*)icCache[slot].extraA == instance->klass) {
				//memo hit:skip the methods table probe
				if (icCache[slot].extraB != NULL) {
					INSTANCE_POISON(instance) = true;
				}
			}
			else {
				LfxValue methodValue;
				if (tableGet(&instance->klass->methods, name, &methodValue)) {
					INSTANCE_POISON(instance) = true;
					if (slot != 0) {
						icCache[slot].extraA = instance->klass;
						icCache[slot].extraB = AS_CLOSURE(methodValue);
					}
				}
				else if (slot != 0) {
					icCache[slot].extraA = instance->klass;
					icCache[slot].extraB = NULL;
				}
			}
		}
	}
	else if (!instance->fields.isGlobal) {
		tableDelete(&instance->fields, name);
	}
	else {
		//the global object backs the variable store: nil-assign ASSIGNS nil and
		//never deletes (a deleted variable would break already-emitted global
		//accesses); mirrors OP_SET_GLOBAL's plain assign semantics
		LfxValue oldValue;
		Entry* entry;
		if (tableGetEntry(&instance->fields, name, &oldValue, &entry)) {
			entry->value = NIL_VAL;
			if (slot != 0 && instance->fields.capacity <= UINT16_MAX) {
				icCache[slot].capacity = instance->fields.capacity;
				icCache[slot].index = (uint32_t)(entry - instance->fields.entries);
			}
		}
		else {
			//defining a fresh global as nil; defaultClass.methods is empty for
			//the global object's lifetime, so no method-shadow poisoning applies
			tableSet(&instance->fields, name, NIL_VAL);
		}
	}
}

//returns true if a callee was entered,false if a runtime error was reported
COLD_FUNCTION
NOINLINE_FUNCTION
static bool icInvokeSlow(LfxVM* vm, ObjInstance* instance, ObjString* method, uint8_t argCount, uint8_t slot, InlineCacheSlot* icCache) {
	//slow path: field hit shadows method → poison + callValue
	LfxValue value;
	Entry* entry;
	if (tableGetEntry(&instance->fields, method, &value, &entry)) {
		INSTANCE_POISON(instance) = true;
		STACK_PEEK(argCount) = value;
		return callValue(vm, value, argCount);
	}

	//method lookup
	LfxValue methodValue;
	if (!tableGetEntry(&instance->klass->methods, method, &methodValue, &entry)) {
		runtimeError(vm, "Undefined property '%s'.", method->chars);
		return false;
	}
	//backfill cache
	if (slot != 0) {
		icCache[slot].extraA = instance->klass;
		icCache[slot].extraB = AS_CLOSURE(methodValue);
	}
	return call(vm, AS_CLOSURE(methodValue), argCount);
}

#if DEBUG_TRACE_EXECUTION //print in debug mode
//only dump the active frame's slots (locals + temporaries) to stderr;
//outer frames' slots are skipped to keep the trace readable
COLD_FUNCTION
static void traceFrameStack(LfxVM* vm, FILE* stream, CallFrame* frame) {
	fprintf(stream, "          ");
	for (LfxValue* slot = frame->slots; slot < vm->cache.stackTop; slot++) {
		fprintf(stream, "[ ");
		printValue(stream, *slot, false);
		fprintf(stream, " ]");
	}
	fprintf(stream, "\n");
}
#endif

//to run code in vm
HOT_FUNCTION
static InterpretResult run(LfxVM* vm)
{
	CallFrame* frame = &vm->cache.frames[vm->cache.frameCount - 1];
	register uint8_t* ip = frame->ip;
	//cached constants base of the current function,refreshed at every frame switch
	LfxValue* constants = frame->closure->function->constants;

#define UPDATE_FRAME()	\
	do {				\
		frame = &vm->cache.frames[vm->cache.frameCount - 1];\
		ip = frame->ip; \
		constants = frame->closure->function->constants;	\
	} while (false)

#if COMPUTE_GOTO
	static void* label_instructions[] = {
		[OP_CONSTANT] = && label_op_constant,
		[OP_CONST_NUMBER] = && label_op_const_number,

		[OP_GET_LOCAL] = && label_op_get_local,
		[OP_SET_LOCAL] = && label_op_set_local,
		[OP_SET_LOCAL_POP] = && label_op_set_local_pop,
		[OP_MOVE_LOCAL] = && label_op_move_local,

		[OP_ADD] = && label_op_add,
		[OP_SUBTRACT] = && label_op_subtract,
		[OP_MULTIPLY] = && label_op_multiply,
		[OP_DIVIDE] = && label_op_divide,
		[OP_MODULUS] = && label_op_modulus,
		[OP_NOT] = && label_op_not,
		[OP_NEGATE] = && label_op_negate,

		[OP_NIL] = && label_op_nil,
		[OP_TRUE] = && label_op_true,
		[OP_FALSE] = && label_op_false,
		[OP_EQUAL] = && label_op_equal,
		[OP_GREATER] = && label_op_greater,
		[OP_LESS] = && label_op_less,
		[OP_NOT_EQUAL] = && label_op_not_equal,
		[OP_LESS_EQUAL] = && label_op_less_equal,
		[OP_GREATER_EQUAL] = && label_op_greater_equal,

		[OP_JUMP] = && label_op_jump,
		[OP_LOOP] = && label_op_loop,
		[OP_JUMP_IF_FALSE] = && label_op_jump_if_false,
		[OP_JUMP_IF_FALSE_POP] = && label_op_jump_if_false_pop,
		[OP_JUMP_IF_TRUE] = && label_op_jump_if_true,
		[OP_POP] = && label_op_pop,
		[OP_POP_N] = && label_op_pop_n,
		[OP_BITWISE] = && label_op_bitwise,
		[OP_CALL] = && label_op_call,
		[OP_INVOKE] = && label_op_invoke,
		[OP_SUPER_INVOKE] = && label_op_super_invoke,
		[OP_RETURN] = && label_op_return,
		[OP_ITERATE] = && label_op_iterate,

		[OP_GET_PROPERTY] = && label_op_get_property,
		[OP_SET_PROPERTY] = && label_op_set_property,
		[OP_SET_PROPERTY_POP] = && label_op_set_property_pop,
		[OP_GET_PROP_LOCAL] = && label_op_get_prop_local,
		[OP_SET_PROP_LOCAL] = && label_op_set_prop_local,
		[OP_SET_PROP_LOCAL_POP] = && label_op_set_property_local_pop,
		[OP_SET_INDEX] = && label_op_set_index,
		[OP_GET_INDEX] = && label_op_get_index,
		[OP_GET_SUPER] = && label_op_get_super,
		[OP_GET_GLOBAL] = && label_op_get_global,
		[OP_SET_GLOBAL] = && label_op_set_global,
		[OP_DEFINE_GLOBAL] = && label_op_define_global,
		[OP_SET_SUBSCRIPT] = && label_op_set_subscript,
		[OP_GET_SUBSCRIPT] = && label_op_get_subscript,

		[OP_CLOSURE] = && label_op_closure,
		[OP_GET_UPVALUE] = && label_op_get_upvalue,
		[OP_SET_UPVALUE] = && label_op_set_upvalue,
		[OP_CLOSE_UPVALUE] = && label_op_close_upvalue,
		[OP_NEW_ARRAY] = && label_op_new_array,
		[OP_NEW_OBJECT] = && label_op_new_object,
		[OP_NEW_PROPERTY] = && label_op_new_property,
		[OP_NEW_ITERATOR] = && label_op_new_iterator,

		[OP_INSTANCE_OF] = && label_op_instance_of,
		[OP_TYPE_OF] = && label_op_type_of,
		[OP_IN] = && label_op_in,
		[OP_CLASS] = && label_op_class,
		[OP_INHERIT] = && label_op_inherit,
		[OP_METHOD] = && label_op_method,

		[OP_MODULE_BUILTIN] = && label_op_module_builtin,

		[OP_PRINT] = && label_op_print,
		[OP_THROW] = && label_op_throw,
		[OP_IMPORT] = && label_op_import,

		[OP_ADD_CONST] = && label_op_add_const,
		[OP_SUBTRACT_CONST] = && label_op_subtract_const,
		[OP_MULTIPLY_CONST] = && label_op_multiply_const,
		[OP_DIVIDE_CONST] = && label_op_divide_const,
		[OP_MODULUS_CONST] = && label_op_modulus_const,
		[OP_EQUAL_CONST] = && label_op_equal_const,
		[OP_GREATER_CONST] = && label_op_greater_const,
		[OP_LESS_CONST] = && label_op_less_const,
		[OP_NOT_EQUAL_CONST] = && label_op_not_equal_const,
		[OP_LESS_EQUAL_CONST] = && label_op_less_equal_const,
		[OP_GREATER_EQUAL_CONST] = && label_op_greater_equal_const,
		[OP_EQUAL_CONST_NUMBER] = && label_op_equal_const_number,
		[OP_NOT_EQUAL_CONST_NUMBER] = && label_op_not_equal_const_number,

		[OP_ADD_LOCAL] = && label_op_add_local,
		[OP_SUBTRACT_LOCAL] = && label_op_subtract_local,
		[OP_MULTIPLY_LOCAL] = && label_op_multiply_local,
		[OP_DIVIDE_LOCAL] = && label_op_divide_local,
		[OP_MODULUS_LOCAL] = && label_op_modulus_local,
		[OP_EQUAL_LOCAL] = && label_op_equal_local,
		[OP_GREATER_LOCAL] = && label_op_greater_local,
		[OP_LESS_LOCAL] = && label_op_less_local,
		[OP_NOT_EQUAL_LOCAL] = && label_op_not_equal_local,
		[OP_LESS_EQUAL_LOCAL] = && label_op_less_equal_local,
		[OP_GREATER_EQUAL_LOCAL] = && label_op_greater_equal_local,

		[OP_NOT_LOCAL] = && label_op_not_local,
		[OP_NEGATE_LOCAL] = && label_op_negate_local,

		[OP_JIF_LESS_LL] = && label_op_jif_less_ll,
		[OP_JIF_LESS_EQUAL_LL] = && label_op_jif_less_equal_ll,
		[OP_JIF_EQUAL_LL] = && label_op_jif_equal_ll,
		[OP_JIF_NOT_EQUAL_LL] = && label_op_jif_not_equal_ll,

		[OP_JIF_LESS_LC_NUMBER] = && label_op_jif_less_lc_number,
		[OP_JIF_LESS_EQUAL_LC_NUMBER] = && label_op_jif_less_equal_lc_number,
		[OP_JIF_GREATER_LC_NUMBER] = && label_op_jif_greater_lc_number,
		[OP_JIF_GREATER_EQUAL_LC_NUMBER] = && label_op_jif_greater_equal_lc_number,
		[OP_JIF_EQUAL_LC_NUMBER] = && label_op_jif_equal_lc_number,
		[OP_JIF_NOT_EQUAL_LC_NUMBER] = && label_op_jif_not_equal_lc_number,
		[OP_JIF_EQUAL_LC] = && label_op_jif_equal_lc,
		[OP_JIF_NOT_EQUAL_LC] = && label_op_jif_not_equal_lc,
		[OP_CEDE] = && label_op_cede,
	};
#endif

#define READ_BYTE() (*(ip++))
#define READ_SHORT() (ip += 2, (uint16_t)(ip[-2] | (ip[-1] << 8)))
#define READ_24bits() (ip += 3, (uint32_t)(ip[-3] | (ip[-2] << 8) | (ip[-1] << 16)))
#define READ_CONSTANT(index) (vm->constants.values[(index)])
	//constants in function's own table (number),16bits index
	//cached as a run(vm) local next to frame/ip,refreshed at every frame switch
#define READ_LOCAL_CONSTANT(index) (constants[(index)])

	// push(pop() op pop())
#define BINARY_OP(valueType,op)																		\
    do {																							\
		/* both operands must be numbers */													\
		if (IS_NUMBER(vm->cache.stackTop[-2]) && IS_NUMBER(vm->cache.stackTop[-1])) {								\
			/* compute the result in place */									\
			vm->cache.stackTop[-2] = valueType(AS_NUMBER(vm->cache.stackTop[-2]) op AS_NUMBER(vm->cache.stackTop[-1]));	\
			vm->cache.stackTop--;																			\
		} else {														                            \
			RUNTIME_ERROR("Operands must be numbers.");										\
		}																							\
	} while (false)

#define BINARY_OP_WITH_RIGHT(valueType,right,op)											\
    do {																					\
		/* both operands must be numbers */											\
		if (IS_NUMBER(vm->cache.stackTop[-1]) && IS_NUMBER(right)) {								\
			/* compute the result in place */							\
			vm->cache.stackTop[-1] = valueType(AS_NUMBER(vm->cache.stackTop[-1]) op AS_NUMBER(right));	\
		} else {																			\
			RUNTIME_ERROR("Operands must be numbers.");										\
		}																					\
	} while (false)

	//ordering comparison (< <= > >=): numbers or string/stringBuilder (memcmp order)
#define COMPARE_OP(op)																		\
	do {																					\
		if (IS_NUMBER(vm->cache.stackTop[-2]) && IS_NUMBER(vm->cache.stackTop[-1])) {						\
			vm->cache.stackTop[-2] = BOOL_VAL(AS_NUMBER(vm->cache.stackTop[-2]) op AS_NUMBER(vm->cache.stackTop[-1]));\
			vm->cache.stackTop--;																	\
		}																					\
		else if (isStringLike(vm->cache.stackTop[-2]) && isStringLike(vm->cache.stackTop[-1])) {			\
			vm->cache.stackTop[-2] = BOOL_VAL(compareStringLike(vm->cache.stackTop[-2], vm->cache.stackTop[-1]) op 0);\
			vm->cache.stackTop--;																	\
		}																					\
		else {																				\
			RUNTIME_ERROR("Operands must be two numbers or two strings.");					\
		}																					\
	} while (false)

	//ordering comparison with a non-stack right operand (local or number constant)
#define COMPARE_OP_WITH_RIGHT(right,op)													\
	do {																					\
		if (IS_NUMBER(vm->cache.stackTop[-1]) && IS_NUMBER(right)) {								\
			vm->cache.stackTop[-1] = BOOL_VAL(AS_NUMBER(vm->cache.stackTop[-1]) op AS_NUMBER(right));		\
		}																					\
		else if (isStringLike(vm->cache.stackTop[-1]) && isStringLike(right)) {					\
			vm->cache.stackTop[-1] = BOOL_VAL(compareStringLike(vm->cache.stackTop[-1], right) op 0);		\
		}																					\
		else {																				\
			RUNTIME_ERROR("Operands must be two numbers or two strings.");					\
		}																					\
	} while (false)

//fused compare + jump-if-false: no stack traffic, jump when compare is false.
//dual NEXT_INSTRUCTION on purpose: a single-exit form (`if (!cmp) ip += offset`)
//makes the compiler fold the ip adjustment into a cmov, which serializes the
//next-ip selection into every dispatch; with two exits the hot path dispatches
//through a predictable branch and its next-ip is ready before the compare resolves
#define JIF_CMP_WITH_RIGHT(left,right,offset,op)										\
	if (IS_NUMBER(left) && IS_NUMBER(right)) {											\
		if (AS_NUMBER(left) op AS_NUMBER(right)) NEXT_INSTRUCTION;						\
		ip += offset;																	\
		NEXT_INSTRUCTION;																\
	}																					\
	else if (isStringLike(left) && isStringLike(right)) {								\
		if (compareStringLike(left, right) op 0) NEXT_INSTRUCTION;						\
		ip += offset;																	\
		NEXT_INSTRUCTION;																\
	}																					\
	else {																				\
		RUNTIME_ERROR("Operands must be two numbers or two strings.");					\
	}

//fused compare + jump-if-false, number-constant form: the constant operand rides
//the function's own pool and is compile-time guaranteed number, so only the local
//side is checked (dual NEXT_INSTRUCTION exits, same reason as JIF_CMP_WITH_RIGHT)
#define JIF_CMP_NUM_WITH_RIGHT(left,right,offset,op)									\
	if (IS_NUMBER(left)) {																\
		if (AS_NUMBER(left) op AS_NUMBER(right)) NEXT_INSTRUCTION;						\
		ip += offset;																	\
		NEXT_INSTRUCTION;																\
	}																					\
	else {																				\
		RUNTIME_ERROR("Operands must be two numbers or two strings.");					\
	}

//we need continue/break when debug trace
#if !COMPUTE_GOTO || DEBUG_TRACE_EXECUTION
#define NEXT_INSTRUCTION continue
#else //direct threading code
#define NEXT_INSTRUCTION goto *label_instructions[READ_BYTE()]
#endif									

//error inside run(vm): sync the top frame's ip for the stack trace, then raise.
//error paths are cold, so this store costs nothing and keeps `ip` free of any
//stack escape (register-resident across the whole dispatch loop)
#define RUNTIME_ERROR(...)						\
	do {										\
		frame->ip = ip;							\
		runtimeError(vm, __VA_ARGS__);				\
		return INTERPRET_RUNTIME_ERROR;			\
	} while (false)

	while (true)
	{
#if DEBUG_TRACE_EXECUTION //print in debug mode
		traceFrameStack(vm, stderr, frame);

		//current ptr - begin ptr = offset value
		disassembleInstruction(frame->closure->function, (uint32_t)(ip - frame->closure->function->chunk.code));
#endif // DEBUG_TRACE_EXECUTION

		uint8_t instruction = READ_BYTE();
#if COMPUTE_GOTO
		goto* label_instructions[instruction];
		//jmp direct,no switch
#endif

		switch (instruction)
		{
		case OP_CONSTANT: {
		label_op_constant:
			LfxValue constant = READ_CONSTANT(READ_24bits());
			stack_push(vm, constant);
			NEXT_INSTRUCTION;
		}
		case OP_CONST_NUMBER: {
		label_op_const_number:
			LfxValue constant = READ_LOCAL_CONSTANT(READ_SHORT());
			stack_push(vm, constant);
			NEXT_INSTRUCTION;
		}
		case OP_CLOSURE: {
		label_op_closure:
			LfxValue constant = READ_CONSTANT(READ_24bits());
			ObjFunction* function = AS_FUNCTION(constant);
			ObjClosure* closure = newClosure(function);
			stack_push(vm, OBJ_VAL(closure));

			for (uint32_t i = 0; i < closure->upvalueCount; i++) {
				uint8_t isLocal = READ_BYTE();
				uint16_t index = READ_SHORT();
				if (isLocal) {
					closure->upvalues[i] = captureUpvalue(vm, frame->slots + index);
				}
				else {
					closure->upvalues[i] = frame->closure->upvalues[index];
				}
			}
			NEXT_INSTRUCTION;
		}
		case OP_CLASS: {
		label_op_class:
			LfxValue constant = READ_CONSTANT(READ_24bits());
			ObjString* name = AS_STRING(constant);
			stack_push(vm, OBJ_VAL(newClass(name)));
			NEXT_INSTRUCTION;
		}
		case OP_METHOD: {
		label_op_method:
			LfxValue constant = READ_CONSTANT(READ_24bits());
			ObjString* name = AS_STRING(constant);
			defineMethod(vm, name);
			NEXT_INSTRUCTION;
		}
		case OP_INHERIT: {
		label_op_inherit:
			LfxValue superclass = vm->cache.stackTop[-2];
			if (IS_CLASS(superclass)) {
				ObjClass* subclass = AS_CLASS(vm->cache.stackTop[-1]);
				tableAddAll(&AS_CLASS(superclass)->methods, &subclass->methods);
			//inherited constructor:keep the initializer fast path in sync with the copied methods table
			//(a subclass init declared after this would overwrite it via defineMethod)
			if (subclass->initializer == NULL) {
				subclass->initializer = AS_CLASS(superclass)->initializer;
			}
				//record the link for instanceof (methods above are copies, not a lookup chain)
				subclass->superclass = AS_CLASS(superclass);
				stack_pop(vm); // Subclass.
			}
			else {
				RUNTIME_ERROR("Superclass must be a class.");
			}
			NEXT_INSTRUCTION;
		}
		case OP_GET_SUPER: {
		label_op_get_super:
			LfxValue constant = READ_CONSTANT(READ_24bits());
			ObjString* name = AS_STRING(constant);
			ObjClass* superclass = AS_CLASS(stack_pop(vm));
			bindMethod(vm, superclass, name);
			NEXT_INSTRUCTION;
		}
		case OP_GET_PROPERTY: {
		label_op_get_property:
			if (!IS_INSTANCE(vm->cache.stackTop[-1])) {
				RUNTIME_ERROR("Only instances have properties.");
			}

			ObjInstance* instance = AS_INSTANCE(vm->cache.stackTop[-1]);
			LfxValue constant = READ_CONSTANT(READ_24bits());
			ObjString* name = AS_STRING(constant);
			uint8_t slot = READ_BYTE();
			//icCache derived per handler,not a run(vm)-wide local,to keep the dispatch loop's live ranges small
			InlineCacheSlot* icCache = frame->caches;

			//inline cache: validate [capacity, entry index] against the live table
			//slot 0 = sentinel slot {0,0}: real table capacity is never 0 → miss; idx bound check guards capacity==0 tables
			{
				Table* fields = &instance->fields;
				if ((uint32_t)icCache[slot].capacity == fields->capacity) {
					uint32_t idx = icCache[slot].index;
					if (idx < fields->capacity && fields->entries[idx].key == name) {
						stack_replace(vm, fields->entries[idx].value);
						NEXT_INSTRUCTION;
					}
				}
			}

			icGetPropertySlow(vm, instance, name, slot, icCache);
			NEXT_INSTRUCTION;
		}
		case OP_SET_PROPERTY: {
		label_op_set_property:
			if (!IS_INSTANCE(vm->cache.stackTop[-2])) {
				RUNTIME_ERROR("Only instances have fields.");
			}

			ObjInstance* instance = AS_INSTANCE(vm->cache.stackTop[-2]);
			LfxValue constant = READ_CONSTANT(READ_24bits());
			ObjString* name = AS_STRING(constant);
			uint8_t slot = READ_BYTE();
			InlineCacheSlot* icCache = frame->caches;

			//inline cache: direct write only for existing entries with non-nil value (nil means delete)
			//frozen fields fall through to the slow path, which rejects the write
			if (NOT_NIL(vm->cache.stackTop[-1])) {
				Table* fields = &instance->fields;
				if (!fields->isFrozen && (uint32_t)icCache[slot].capacity == fields->capacity) {
					uint32_t idx = icCache[slot].index;
					if (idx < fields->capacity && fields->entries[idx].key == name) {
						fields->entries[idx].value = vm->cache.stackTop[-1];
						LfxValue value = stack_pop(vm);
						stack_replace(vm, value);
						NEXT_INSTRUCTION;
					}
				}
			}

			icSetPropertySlow(instance, name, vm->cache.stackTop[-1], slot, icCache);
			LfxValue value = stack_pop(vm);
			stack_replace(vm, value);
			NEXT_INSTRUCTION;
		}
		case OP_SET_PROPERTY_POP: {
		label_op_set_property_pop:
			if (!IS_INSTANCE(vm->cache.stackTop[-2])) {
				RUNTIME_ERROR("Only instances have fields.");
			}

			ObjInstance* instance = AS_INSTANCE(vm->cache.stackTop[-2]);
			LfxValue constant = READ_CONSTANT(READ_24bits());
			ObjString* name = AS_STRING(constant);
			uint8_t slot = READ_BYTE();
			InlineCacheSlot* icCache = frame->caches;

			//inline cache: direct write only for existing entries with non-nil value (nil means delete)
			//frozen fields fall through to the slow path, which rejects the write
			if (NOT_NIL(vm->cache.stackTop[-1])) {
				Table* fields = &instance->fields;
				if (!fields->isFrozen && (uint32_t)icCache[slot].capacity == fields->capacity) {
					uint32_t idx = icCache[slot].index;
					if (idx < fields->capacity && fields->entries[idx].key == name) {
						fields->entries[idx].value = vm->cache.stackTop[-1];
						//pop value and instance
						vm->cache.stackTop -= 2;
						NEXT_INSTRUCTION;
					}
				}
			}

			icSetPropertySlow(instance, name, vm->cache.stackTop[-1], slot, icCache);
			//pop value and instance
			vm->cache.stackTop -= 2;
			NEXT_INSTRUCTION;
		}
		case OP_GET_PROP_LOCAL: {
		label_op_get_prop_local:
			uint32_t index = READ_SHORT();
			LfxValue receiver = frame->slots[index];
			if (!IS_INSTANCE(receiver)) {
				RUNTIME_ERROR("Only instances have properties.");
			}

			ObjInstance* instance = AS_INSTANCE(receiver);
			LfxValue constant = READ_CONSTANT(READ_24bits());
			ObjString* name = AS_STRING(constant);
			uint8_t slot = READ_BYTE();
			//icCache derived per handler,not a run(vm)-wide local,to keep the dispatch loop's live ranges small
			InlineCacheSlot* icCache = frame->caches;

			//inline cache: validate [capacity, entry index] against the live table
			//slot 0 = sentinel slot {0,0}: real table capacity is never 0 → miss; idx bound check guards capacity==0 tables
			{
				Table* fields = &instance->fields;
				if ((uint32_t)icCache[slot].capacity == fields->capacity) {
					uint32_t idx = icCache[slot].index;
					if (idx < fields->capacity && fields->entries[idx].key == name) {
						stack_push(vm, fields->entries[idx].value);
						NEXT_INSTRUCTION;
					}
				}
			}

			//slow path: present the receiver on the stack top so the shared cold
			//helper (stack_replace semantics / bindMethod consume the top slot)
			//operates on a copy of it; the result (field value / bound method /
			//nil) replaces the copy and ends on top, matching the fused push
			stack_push(vm, receiver);
			icGetPropertySlow(vm, instance, name, slot, icCache);
			NEXT_INSTRUCTION;
		}
		case OP_SET_PROP_LOCAL: {
		label_op_set_prop_local:
			uint32_t index = READ_SHORT();
			LfxValue receiver = frame->slots[index];
			if (!IS_INSTANCE(receiver)) {
				RUNTIME_ERROR("Only instances have fields.");
			}

			ObjInstance* instance = AS_INSTANCE(receiver);
			LfxValue constant = READ_CONSTANT(READ_24bits());
			ObjString* name = AS_STRING(constant);
			uint8_t slot = READ_BYTE();
			InlineCacheSlot* icCache = frame->caches;

			//inline cache: direct write only for existing entries with non-nil value (nil means delete)
			//frozen fields fall through to the slow path, which rejects the write
			if (NOT_NIL(vm->cache.stackTop[-1])) {
				Table* fields = &instance->fields;
				if (!fields->isFrozen && (uint32_t)icCache[slot].capacity == fields->capacity) {
					uint32_t idx = icCache[slot].index;
					if (idx < fields->capacity && fields->entries[idx].key == name) {
						fields->entries[idx].value = vm->cache.stackTop[-1];
						//the assigned value stays on the stack (assignment-as-expression semantics)
						NEXT_INSTRUCTION;
					}
				}
			}

			icSetPropertySlow(instance, name, vm->cache.stackTop[-1], slot, icCache);
			NEXT_INSTRUCTION;
		}
		case OP_SET_PROP_LOCAL_POP: {
		label_op_set_property_local_pop:
			uint32_t index = READ_SHORT();
			LfxValue receiver = frame->slots[index];
			if (!IS_INSTANCE(receiver)) {
				RUNTIME_ERROR("Only instances have fields.");
			}

			ObjInstance* instance = AS_INSTANCE(receiver);
			LfxValue constant = READ_CONSTANT(READ_24bits());
			ObjString* name = AS_STRING(constant);
			uint8_t slot = READ_BYTE();
			InlineCacheSlot* icCache = frame->caches;

			//inline cache: direct write only for existing entries with non-nil value (nil means delete)
			//frozen fields fall through to the slow path, which rejects the write
			if (NOT_NIL(vm->cache.stackTop[-1])) {
				Table* fields = &instance->fields;
				if (!fields->isFrozen && (uint32_t)icCache[slot].capacity == fields->capacity) {
					uint32_t idx = icCache[slot].index;
					if (idx < fields->capacity && fields->entries[idx].key == name) {
						fields->entries[idx].value = vm->cache.stackTop[-1];
						//pop the assigned value (the receiver never occupied a stack slot)
						vm->cache.stackTop -= 1;
						NEXT_INSTRUCTION;
					}
				}
			}

			icSetPropertySlow(instance, name, vm->cache.stackTop[-1], slot, icCache);
			vm->cache.stackTop -= 1;
			NEXT_INSTRUCTION;
		}
		case OP_GET_INDEX: {
		label_op_get_index:
			LfxValue target = vm->cache.stackTop[-1];
			LfxValue constant = READ_LOCAL_CONSTANT(READ_SHORT());
			double num_index = AS_NUMBER(constant);

			if (isIndexableArray(target)) {
				//get array
				ObjArray* array = AS_ARRAY(target);

				if (ARRAY_IN_RANGE(array, num_index)) {
					GET_INDEXABLE_ELEMENT(array, (uint32_t)num_index, STACK_PEEK(0));
				}
				else {
					stack_replace(vm, NIL_VAL);
				}
				NEXT_INSTRUCTION;
			}
			else if (IS_STRING(target)) {
				//get string
				ObjString* string = AS_STRING(target);

				if (ARRAY_IN_RANGE(string, num_index)) {//return ascii
					stack_replace(vm, NUMBER_VAL((uint8_t)(string->chars[(uint32_t)num_index])));
				}
				else {
					stack_replace(vm, NIL_VAL);
				}
				NEXT_INSTRUCTION;
			}

			RUNTIME_ERROR("Only arrayLike,stringBuilder and string can get number subscript.");
		}
		case OP_SET_INDEX: {
		label_op_set_index:
			LfxValue target = vm->cache.stackTop[-2];
			LfxValue value = vm->cache.stackTop[-1];

			LfxValue constant = READ_LOCAL_CONSTANT(READ_SHORT());
			double num_index = AS_NUMBER(constant);

			if (isArrayLike(target)) {
				//get array
				ObjArray* array = AS_ARRAY(target);

				if (ARRAY_IN_RANGE(array, num_index)) {
					SET_INDEXABLE_ELEMENT(array, (uint32_t)num_index, value);
					vm->cache.stackTop[-2] = value;

					vm->cache.stackTop -= 1;
					NEXT_INSTRUCTION;
				}
				else {
					RUNTIME_ERROR("Array index out of range.");
				}
			}

			RUNTIME_ERROR("Only arrayLike can set number subscript.");
		}
		case OP_GET_SUBSCRIPT: {
		label_op_get_subscript:
			LfxValue target = vm->cache.stackTop[-2];
			LfxValue index = vm->cache.stackTop[-1];

			if (isIndexableArray(target)) {
				if (IS_NUMBER(index)) {
					//get array
					ObjArray* array = AS_ARRAY(target);
					double num_index = AS_NUMBER(index);

					vm->cache.stackTop--;//it is number,so pop is allowed
					if (ARRAY_IN_RANGE(array, num_index)) {
						GET_INDEXABLE_ELEMENT(array, (uint32_t)num_index, STACK_PEEK(0));
					}
					else {
						stack_replace(vm, NIL_VAL);
					}
					NEXT_INSTRUCTION;
				}
				else {
					RUNTIME_ERROR("Array subscript must be number.");
				}
			}
			else if (IS_INSTANCE(target)) {
				if (IS_STRING(index)) {
					ObjInstance* instance = AS_INSTANCE(target);
					ObjString* name = AS_STRING(index);
					LfxValue value;

					vm->cache.stackTop--;//it is string,we don't gc string so pop is allowed
					if (tableGet(&instance->fields, name, &value)) {
						stack_replace(vm, value);
						NEXT_INSTRUCTION;
					}
					//method fallback via the (possibly default, empty) class
					bindMethod(vm, instance->klass, name);
					NEXT_INSTRUCTION;
				}
				else {
					RUNTIME_ERROR("Instance subscript must be string.");
				}
			}
			else if (IS_STRING(target)) {
				if (IS_NUMBER(index)) {
					//get string
					ObjString* string = AS_STRING(target);
					double num_index = AS_NUMBER(index);

					vm->cache.stackTop--;//it is number,so pop is allowed
					if (ARRAY_IN_RANGE(string, num_index)) {//return ascii
						stack_replace(vm, NUMBER_VAL((uint8_t)(string->chars[(uint32_t)num_index])));
					}
					else {
						stack_replace(vm, NIL_VAL);
					}
					NEXT_INSTRUCTION;
				}
				else {
					RUNTIME_ERROR("String subscript must be number.");
				}
			}

			RUNTIME_ERROR("Only instances,arrayLike,stringBuilder and string can get subscript.");
		}
		case OP_SET_SUBSCRIPT: {
		label_op_set_subscript:
			LfxValue target = vm->cache.stackTop[-3];
			LfxValue index = vm->cache.stackTop[-2];
			LfxValue value = vm->cache.stackTop[-1];

			if (isArrayLike(target)) {
				if (IS_NUMBER(index)) {
					//get array
					ObjArray* array = AS_ARRAY(target);
					double num_index = AS_NUMBER(index);

					if (ARRAY_IN_RANGE(array, num_index)) {
						SET_INDEXABLE_ELEMENT(array, (uint32_t)num_index, value);
						vm->cache.stackTop[-3] = value;

						vm->cache.stackTop -= 2;
						NEXT_INSTRUCTION;
					}
					else {
						RUNTIME_ERROR("Array index out of range.");
					}
				}
				else {
					RUNTIME_ERROR("Array subscript must be number.");
				}
			}
			else if (IS_INSTANCE(target)) {
				if (IS_STRING(index)) {
					ObjInstance* instance = AS_INSTANCE(target);
					ObjString* name = AS_STRING(index);

					//frozen instances reject writes and deletes (silent ignore, JS non-strict style)
					//the global object backs the variable store: nil-assign assigns nil
					//and never deletes (a deleted variable would break already-emitted
					//global accesses); mirrors the property-write path above
					if (!instance->fields.isFrozen) {
						// you can't delete a global variable,so we don't delete it
						if (NOT_NIL(value) || instance->fields.isGlobal) {
							tableSet(&instance->fields, name, value);
						}
						else {
							tableDelete(&instance->fields, name);
						}
					}

					vm->cache.stackTop[-3] = value;
					vm->cache.stackTop -= 2;
					NEXT_INSTRUCTION;
				}
				else {
					RUNTIME_ERROR("Instance subscript must be string.");
				}
			}

			RUNTIME_ERROR("Only instances and arrayLike can set subscript.");
		}
		case OP_DEFINE_GLOBAL: {
		label_op_define_global:
			LfxValue constant = READ_CONSTANT(READ_24bits());
			ObjString* name = AS_STRING(constant);

			//it's not a new key,no cache, global can't frozen
			tableSet(&vm->globals.fields, name, vm->cache.stackTop[-1]);
			vm->cache.stackTop--;//can not dec first,because gc will kill it
			NEXT_INSTRUCTION;
		}
		case OP_GET_GLOBAL: {
		label_op_get_global:
			LfxValue constant = READ_CONSTANT(READ_24bits());
			ObjString* name = AS_STRING(constant);

			//inline find by cache symbol
			if ((name->symbol != INVALID_OBJ_STRING_SYMBOL)) {
				Entry* entry = &vm->globals.fields.entries[name->symbol];

				if (entry->key == name) {
					stack_push(vm, entry->value);
					NEXT_INSTRUCTION;
				}
			}

			LfxValue value;
			if (!tableGet(&vm->globals.fields, name, &value)) {
				RUNTIME_ERROR("Undefined variable '%s'.", name->chars);
			}
			stack_push(vm, value);
			NEXT_INSTRUCTION;
		}
		case OP_SET_GLOBAL: {
		label_op_set_global:
			LfxValue constant = READ_CONSTANT(READ_24bits());
			ObjString* name = AS_STRING(constant);

			//inline find by cache symbol(if we found it,it's not new key)
			if ((name->symbol != INVALID_OBJ_STRING_SYMBOL)) {
				Entry* entry = &vm->globals.fields.entries[name->symbol];
				if (entry->key == name) {
					entry->value = vm->cache.stackTop[-1];
					NEXT_INSTRUCTION;
				}
			}

			// global can't frozen
			if (tableSet(&vm->globals.fields, name, vm->cache.stackTop[-1])) {
				//lox dont allow setting undefined one
				tableDelete(&vm->globals.fields, name);
				RUNTIME_ERROR("Undefined variable '%s'.", name->chars);
			}
			NEXT_INSTRUCTION;
		}
		case OP_NEW_ARRAY: {
		label_op_new_array:
			uint16_t size = READ_SHORT();
			ObjArray* array = newArray(OBJ_ARRAY);
			//push to prevent gc
			stack_push(vm, OBJ_VAL(array));

			if (size > 0) {
				reserveArray(array, size);//allocate after push stack

				//init the array
				memcpy(array->payload, vm->cache.stackTop - size - 1, sizeof(LfxValue) * size);
				array->length = size;

				//pop the values and the temp array at top
				STACK_PEEK(size) = OBJ_VAL(array);
				vm->cache.stackTop -= size;
			}
			NEXT_INSTRUCTION;
		}
		case OP_NEW_OBJECT: {
		label_op_new_object:
			stack_push(vm, OBJ_VAL(newInstance(&vm->defaultClass)));
			NEXT_INSTRUCTION;
		}
		case OP_NEW_PROPERTY: {
		label_op_new_property:
			ObjInstance* instance = AS_INSTANCE(vm->cache.stackTop[-2]);
			LfxValue constant = READ_CONSTANT(READ_24bits());
			ObjString* name = AS_STRING(constant);
			// this is used to create literal object,so we don't need to check frozen
			tableSet(&instance->fields, name, vm->cache.stackTop[-1]);
			stack_pop(vm);
			NEXT_INSTRUCTION;
		}
		case OP_NEW_ITERATOR: {
		label_op_new_iterator:
			LfxValue target = vm->cache.stackTop[-1];
			ObjIterator* iter = NULL;

			if (isArrayLike(target)) {
				iter = newIterator(AS_OBJ(target));
				stack_replace(vm, OBJ_VAL(iter));
				NEXT_INSTRUCTION;
			} else if (IS_INSTANCE(target)) {
				iter = newIterator(AS_OBJ(target));
				stack_replace(vm, OBJ_VAL(iter));
				// init iter
				if (!tableGetFirstEntryIndex(&AS_INSTANCE(target)->fields, &iter->index)) {
					iter->isDone = 1;
				}
				NEXT_INSTRUCTION;
			}

			RUNTIME_ERROR("Only arrayLike and instance are iterable.");
		}
		case OP_GET_UPVALUE: {
		label_op_get_upvalue:
			uint8_t slot = READ_BYTE();
			stack_push(vm, *frame->closure->upvalues[slot]->location);
			NEXT_INSTRUCTION;
		}
		case OP_SET_UPVALUE: {
		label_op_set_upvalue:
			uint8_t slot = READ_BYTE();
			*frame->closure->upvalues[slot]->location = vm->cache.stackTop[-1];
			NEXT_INSTRUCTION;
		}
		case OP_NIL: {
		label_op_nil:
			stack_push(vm, NIL_VAL);
			NEXT_INSTRUCTION;
		}
		case OP_TRUE: {
		label_op_true:
			stack_push(vm, BOOL_VAL(true));
			NEXT_INSTRUCTION;
		}
		case OP_FALSE: {
		label_op_false:
			stack_push(vm, BOOL_VAL(false));
			NEXT_INSTRUCTION;
		}
		case OP_EQUAL: {
		label_op_equal:
			vm->cache.stackTop[-2] = BOOL_VAL(valuesEqual(vm->cache.stackTop[-2], vm->cache.stackTop[-1]));
			vm->cache.stackTop--;
			NEXT_INSTRUCTION;
		}
		case OP_NOT_EQUAL: {
		label_op_not_equal:
			vm->cache.stackTop[-2] = BOOL_VAL(!valuesEqual(vm->cache.stackTop[-2], vm->cache.stackTop[-1]));
			vm->cache.stackTop--;
			NEXT_INSTRUCTION;
		}
		case OP_GREATER: {
		label_op_greater:
			COMPARE_OP(> );
			NEXT_INSTRUCTION;
		}
		case OP_LESS: {
		label_op_less:
			COMPARE_OP(< );
			NEXT_INSTRUCTION;
		}
		case OP_GREATER_EQUAL: {
		label_op_greater_equal:
			COMPARE_OP(>= );
			NEXT_INSTRUCTION;
		}
		case OP_LESS_EQUAL: {
		label_op_less_equal:
			COMPARE_OP(<= );
			NEXT_INSTRUCTION;
		}
		case OP_INSTANCE_OF: {
		label_op_instance_of:
			//"x instanceof y" walks x's class chain looking for y: an
			//instance's chain starts at its klass, a class's chain starts at
			//itself (so a class matches itself and every ancestor)
			bool isInstanceOf = false;
			ObjClass* k = NULL;
			if (IS_INSTANCE(vm->cache.stackTop[-2])) {
				k = AS_INSTANCE(vm->cache.stackTop[-2])->klass;
			}
			else if (IS_CLASS(vm->cache.stackTop[-2])) {
				k = AS_CLASS(vm->cache.stackTop[-2]);
			}

			if (k != NULL && IS_CLASS(vm->cache.stackTop[-1])) {
				ObjClass* target = AS_CLASS(vm->cache.stackTop[-1]);
				while (k != NULL) {
					if (k == target) {
						isInstanceOf = true;
						break;
					}
					k = k->superclass;
				}
			}
			vm->cache.stackTop[-2] = BOOL_VAL(isInstanceOf);
			vm->cache.stackTop--;
			NEXT_INSTRUCTION;
		}
		case OP_TYPE_OF: {
		label_op_type_of:
			getTypeof(vm);
			NEXT_INSTRUCTION;
		}
		case OP_IN: {
		label_op_in:
			LfxValue key = vm->cache.stackTop[-2];
			LfxValue target = vm->cache.stackTop[-1];

			if (!IS_STRING(key)) {
				RUNTIME_ERROR("'in' key must be a string.");
			}

			//closed inheritance copies ancestor methods into the class table
			//at inherit time, so fields + own class methods cover everything
			bool found = false;
			ObjString* name = AS_STRING(key);
			LfxValue slot;
			if (IS_INSTANCE(target)) {
				ObjInstance* instance = AS_INSTANCE(target);
				//default class owns no methods: skip the guaranteed-miss lookup
				found = tableGet(&instance->fields, name, &slot)
					|| (instance->klass != &vm->defaultClass && tableGet(&instance->klass->methods, name, &slot));
				vm->cache.stackTop[-2] = BOOL_VAL(found);
				vm->cache.stackTop--;
				NEXT_INSTRUCTION;
			}
			else if (IS_CLASS(target)) {
				found = tableGet(&AS_CLASS(target)->methods, name, &slot);
				vm->cache.stackTop[-2] = BOOL_VAL(found);
				vm->cache.stackTop--;
				NEXT_INSTRUCTION;
			}
			else {
				RUNTIME_ERROR("Only instances and classes support 'in'.");
			}
		}
		case OP_ADD: {
		label_op_add:
			// might cause gc,so can't decrease first
			if (IS_NUMBER(vm->cache.stackTop[-2]) && IS_NUMBER(vm->cache.stackTop[-1])) {
				vm->cache.stackTop[-2] = NUMBER_VAL(AS_NUMBER(vm->cache.stackTop[-2]) + AS_NUMBER(vm->cache.stackTop[-1]));
				vm->cache.stackTop--;
				NEXT_INSTRUCTION;
			}
			else if (IS_STRING(vm->cache.stackTop[-2]) && IS_STRING(vm->cache.stackTop[-1])) {
				ObjString* result = connectString(AS_STRING(vm->cache.stackTop[-2]), AS_STRING(vm->cache.stackTop[-1]));
				vm->cache.stackTop[-2] = OBJ_VAL(result);
				vm->cache.stackTop--;
				NEXT_INSTRUCTION;
			}

			RUNTIME_ERROR("Operands must be two numbers or two strings.");
		}
		case OP_SUBTRACT: {
		label_op_subtract:
			BINARY_OP(NUMBER_VAL, -);
			NEXT_INSTRUCTION;
		}
		case OP_MULTIPLY: {
		label_op_multiply:
			BINARY_OP(NUMBER_VAL, *);
			NEXT_INSTRUCTION;
		}
		case OP_DIVIDE: {
		label_op_divide:
			BINARY_OP(NUMBER_VAL, / );
			NEXT_INSTRUCTION;
		}
		case OP_MODULUS: {
		label_op_modulus:
			/* both operands must be numbers */
			if (IS_NUMBER(vm->cache.stackTop[-2]) && IS_NUMBER(vm->cache.stackTop[-1])) {
				/* compute the result in place */
				vm->cache.stackTop[-2] = NUMBER_VAL(fmod(AS_NUMBER(vm->cache.stackTop[-2]), AS_NUMBER(vm->cache.stackTop[-1])));
				vm->cache.stackTop--;
				NEXT_INSTRUCTION;
			}
			else {
				RUNTIME_ERROR("Operands must be numbers.");
			}
		}

		case OP_NOT: {
		label_op_not:
			vm->cache.stackTop[-1] = BOOL_VAL(isFalsey(vm->cache.stackTop[-1]));
			NEXT_INSTRUCTION;
		}

		case OP_NEGATE: {
		label_op_negate:
			if (IS_NUMBER(vm->cache.stackTop[-1])) {
				vm->cache.stackTop[-1] = NUMBER_VAL(-AS_NUMBER(vm->cache.stackTop[-1]));
				NEXT_INSTRUCTION;
			}
			else {
				RUNTIME_ERROR("Operand must be a number.");
			}
		}

		case OP_BITWISE: {
		label_op_bitwise:
			uint8_t bitOpType = READ_BYTE();
			uint8_t* nextIp = bitInstruction(vm, bitOpType, ip);
			if (nextIp != NULL) {
				ip = nextIp;
				NEXT_INSTRUCTION;
			}
			else {
				RUNTIME_ERROR("Operands must be numbers.");
			}
		}

		case OP_PRINT: {
		label_op_print:
			uint8_t argCount = READ_BYTE();
			//values were pushed left-to-right: print in the same order, single-space joined
			//each value uses the plain single-value print formatting (no expansion)
			for (uint8_t i = 0; i < argCount; i++) {
				if (i != 0) printf(" ");
				printValue(stdout, vm->cache.stackTop[-argCount + i], false);
			}
			printf("\n");
			vm->cache.stackTop -= argCount;
			NEXT_INSTRUCTION;
		}
		case OP_THROW: {
		label_op_throw:
			//sync for the stack trace before the error printer walks the frames
			frame->ip = ip;
			if (throwError(vm, stack_pop(vm), "An exception was thrown.")) {
				NEXT_INSTRUCTION;
			}
			return INTERPRET_RUNTIME_ERROR;
		}
		case OP_GET_LOCAL: {
		label_op_get_local:
			uint32_t index = READ_SHORT();
			stack_push(vm, frame->slots[index]);
			NEXT_INSTRUCTION;
		}
		case OP_SET_LOCAL: {
		label_op_set_local:
			uint32_t index = READ_SHORT();
			frame->slots[index] = vm->cache.stackTop[-1];
			NEXT_INSTRUCTION;
		}
		case OP_SET_LOCAL_POP: {
		label_op_set_local_pop:
			uint32_t index = READ_SHORT();
			frame->slots[index] = vm->cache.stackTop[-1];
			vm->cache.stackTop--;
			NEXT_INSTRUCTION;
		}
		case OP_MOVE_LOCAL: {
		label_op_move_local:
			uint32_t indexSrc = READ_SHORT();
			uint32_t indexDes = READ_SHORT();
			frame->slots[indexDes] = frame->slots[indexSrc];
			NEXT_INSTRUCTION;
		}
		case OP_CLOSE_UPVALUE: {
		label_op_close_upvalue:
			closeUpvalues(vm, vm->cache.stackTop - 1);
			stack_pop(vm);
			NEXT_INSTRUCTION;
		}
		case OP_POP: {
		label_op_pop:
			stack_pop(vm);
			NEXT_INSTRUCTION;
		}
		case OP_POP_N: {
		label_op_pop_n:
			uint32_t index = READ_SHORT();
			vm->cache.stackTop -= index;
			NEXT_INSTRUCTION;
		}
		case OP_JUMP: {
		label_op_jump:
			uint16_t offset = READ_SHORT();
			ip += offset;
			NEXT_INSTRUCTION;
		}
		case OP_LOOP: {
		label_op_loop:
			uint16_t offset = READ_SHORT();
			ip -= offset;
			NEXT_INSTRUCTION;
		}
		case OP_JUMP_IF_FALSE: {
		label_op_jump_if_false:
			uint16_t offset = READ_SHORT();
			LfxValue condition = vm->cache.stackTop[-1];
			if (isFalsey(condition)) {
				ip += offset;
				NEXT_INSTRUCTION;
			}
			NEXT_INSTRUCTION;
		}
		case OP_JUMP_IF_FALSE_POP: {
		label_op_jump_if_false_pop:
			uint16_t offset = READ_SHORT();
			LfxValue condition = vm->cache.stackTop[-1];
			vm->cache.stackTop--;
			if (isFalsey(condition)) {
				ip += offset;
				NEXT_INSTRUCTION;
			}
			NEXT_INSTRUCTION;
		}
		case OP_JUMP_IF_TRUE: {
		label_op_jump_if_true:
			uint16_t offset = READ_SHORT();
			LfxValue condition = vm->cache.stackTop[-1];
			if (isTruthy(condition)) {
				ip += offset;
				NEXT_INSTRUCTION;
			}
			NEXT_INSTRUCTION;
		}
		case OP_CALL: {
		label_op_call:
			uint8_t argCount = READ_BYTE();
			frame->ip = ip;//change before call

			if (!callValue(vm, STACK_PEEK(argCount), argCount)) {
				return INTERPRET_RUNTIME_ERROR;
			}
			//we entered the function
			UPDATE_FRAME();
			NEXT_INSTRUCTION;
		}
		case OP_INVOKE: {
		label_op_invoke:
			LfxValue constant = READ_CONSTANT(READ_24bits());
			ObjString* method = AS_STRING(constant);
			uint8_t argCount = READ_BYTE();
			uint8_t slot = READ_BYTE();
			InlineCacheSlot* icCache = frame->caches;

			LfxValue receiver = STACK_PEEK(argCount);
			if (!IS_INSTANCE(receiver)) {
				RUNTIME_ERROR("Only instances have methods.");
			}
			ObjInstance* instance = AS_INSTANCE(receiver);

			//inline cache: [klass, closure] pointer pair; klass pointer identity is the guard
			//(methods tables are immutable once instances can exist, so the cached closure is stable)
			if (!INSTANCE_POISON(instance)) {
				InlineCacheSlot* c = &icCache[slot];
				if ((ObjClass*)c->extraA == instance->klass) {
					frame->ip = ip;//change before call
					if (!call(vm, (ObjClosure*)c->extraB, argCount)) {
						return INTERPRET_RUNTIME_ERROR;
					}
					//we entered the function
					UPDATE_FRAME();
					NEXT_INSTRUCTION;
				}
			}

			frame->ip = ip;//change before call
			if (!icInvokeSlow(vm, instance, method, argCount, slot, icCache)) {
				return INTERPRET_RUNTIME_ERROR;
			}
			//we entered the function
			UPDATE_FRAME();
			NEXT_INSTRUCTION;
		}
		case OP_SUPER_INVOKE: {
		label_op_super_invoke:
			LfxValue constant = READ_CONSTANT(READ_24bits());
			ObjString* method = AS_STRING(constant);
			uint8_t argCount = READ_BYTE();

			ObjClass* superclass = AS_CLASS(stack_pop(vm));
			frame->ip = ip;//change before call
			if (!invokeFromClass(vm, superclass, method, argCount)) {
				return INTERPRET_RUNTIME_ERROR;
			}
			UPDATE_FRAME();
			NEXT_INSTRUCTION;
		}
		case OP_RETURN: {
		label_op_return:
			LfxValue result = stack_pop(vm);
			//close all remaining upValues of function
			closeUpvalues(vm, frame->slots);
			if (--vm->cache.frameCount == 0) {
				stack_pop(vm);
				return INTERPRET_OK;
			}

			//vm->cache.stackTop = frame->slots;
			//stack_push(vm, result);

			*frame->slots = result;
			vm->cache.stackTop = frame->slots + 1;

			UPDATE_FRAME();
			NEXT_INSTRUCTION;
		}
		case OP_ITERATE: {
		label_op_iterate:
			//stack: [k][v][iter]; operand = exhaustion jump
			uint16_t offset = READ_SHORT();
			ObjIterator* iter = AS_ITERATOR(vm->cache.stackTop[-1]);

			if (!iter->isDone) {
				if (iter->type >= OBJ_ARRAY) {
					ObjArray* array = (ObjArray*)iter->target;
					if (iter->index < array->length) {
						uint32_t i = iter->index++;
						vm->cache.stackTop[-3] = NUMBER_VAL((double)i);
						GET_INDEXABLE_ELEMENT(array, i, vm->cache.stackTop[-2]);
						NEXT_INSTRUCTION;
					}
				}
				else {
					assert(iter->type == OBJ_INSTANCE && "The iterator received a non-instance input.");

					//rehash/insert/delete during iteration is best-effort
					Table* fields = &((ObjInstance*)iter->target)->fields;
					Entry* entry = NULL;

					if (!tableIterateNext(fields, &iter->index, &entry)) {
						vm->cache.stackTop[-3] = OBJ_VAL(entry->key);
						vm->cache.stackTop[-2] = entry->value;
						NEXT_INSTRUCTION;
					}
					else {
						iter->isDone = 1;
					}
				}
			}

			//exhausted: close k/v upvalues, pop k/v/iter
			closeUpvalues(vm, vm->cache.stackTop - 3);
			vm->cache.stackTop -= 3;
			ip += offset;
			NEXT_INSTRUCTION;
		}
		case OP_MODULE_BUILTIN: {
		label_op_module_builtin:
			uint8_t moduleIndex = READ_BYTE();
			stack_push(vm, OBJ_VAL(&vm->builtins[moduleIndex]));
			NEXT_INSTRUCTION;
		}
		case OP_IMPORT: {
		label_op_import:
			LfxValue target = vm->cache.stackTop[-1];

			C_STR path = getBufferFromStringLike(target);
			if (path == NULL) {
				RUNTIME_ERROR("Path to import must be a string or stringBuilder.");
			}

			STR absolutePath = getAbsolutePath(path);

			if (absolutePath == NULL) {
				RUNTIME_ERROR("Failed to get absolute file path.");
			}

			ObjFunction* function = getCachedScript(vm, absolutePath);
			mem_free(absolutePath);
			if (function == NULL) return INTERPRET_COMPILE_ERROR;

			//circular import:the module is already executing somewhere on the
			//call stack,calling it again would re-run its body without end
			for (uint32_t i = 0; i < vm->cache.frameCount; i++) {
				if (vm->cache.frames[i].closure != NULL && vm->cache.frames[i].closure->function == function) {
					RUNTIME_ERROR("Circular import detected.");
				}
			}

			//same as interpret_compile: create the module closure on top of the stack
			ObjClosure* closure = newClosure(function);
			stack_replace(vm, OBJ_VAL(closure));

			//call the module
			frame->ip = ip;//change before call

			if (!call(vm, closure, 0)) {
				return INTERPRET_RUNTIME_ERROR;
			}

			//we entered the function
			UPDATE_FRAME();
			NEXT_INSTRUCTION;
		}
		case OP_ADD_CONST: {
		label_op_add_const:
			//number only,16bits index into function's own constants
			LfxValue constant = READ_LOCAL_CONSTANT(READ_SHORT());
			// might cause gc,so can't decrease first
			if (IS_NUMBER(vm->cache.stackTop[-1])) {
				vm->cache.stackTop[-1] = NUMBER_VAL(AS_NUMBER(vm->cache.stackTop[-1]) + AS_NUMBER(constant));
				NEXT_INSTRUCTION;
			}

			RUNTIME_ERROR("Operands must be two numbers or two strings.");
		}
		case OP_SUBTRACT_CONST: {
		label_op_subtract_const:
			LfxValue constant = READ_LOCAL_CONSTANT(READ_SHORT());
			BINARY_OP_WITH_RIGHT(NUMBER_VAL, constant, -);
			NEXT_INSTRUCTION;
		}
		case OP_MULTIPLY_CONST: {
		label_op_multiply_const:
			LfxValue constant = READ_LOCAL_CONSTANT(READ_SHORT());
			BINARY_OP_WITH_RIGHT(NUMBER_VAL, constant, *);
			NEXT_INSTRUCTION;
		}
		case OP_DIVIDE_CONST: {
		label_op_divide_const:
			LfxValue constant = READ_LOCAL_CONSTANT(READ_SHORT());
			BINARY_OP_WITH_RIGHT(NUMBER_VAL, constant, / );
			NEXT_INSTRUCTION;
		}
		case OP_MODULUS_CONST: {
		label_op_modulus_const:
			LfxValue constant = READ_LOCAL_CONSTANT(READ_SHORT());
			/* both operands must be numbers */
			if (IS_NUMBER(vm->cache.stackTop[-1])) {
				/* compute the result in place */
				vm->cache.stackTop[-1] = NUMBER_VAL(fmod(AS_NUMBER(vm->cache.stackTop[-1]), AS_NUMBER(constant)));
				NEXT_INSTRUCTION;
			}
			else {
				RUNTIME_ERROR("Operands must be numbers.");
			}
		}
		case OP_EQUAL_CONST: {
		label_op_equal_const:
			//non-number constant,24bits index into vm->constants
			LfxValue constant = READ_CONSTANT(READ_24bits());
			vm->cache.stackTop[-1] = BOOL_VAL(valuesEqual(vm->cache.stackTop[-1], constant));
			NEXT_INSTRUCTION;
		}
		case OP_NOT_EQUAL_CONST: {
		label_op_not_equal_const:
			//non-number constant,24bits index into vm->constants
			LfxValue constant = READ_CONSTANT(READ_24bits());
			vm->cache.stackTop[-1] = BOOL_VAL(!valuesEqual(vm->cache.stackTop[-1], constant));
			NEXT_INSTRUCTION;
		}
		case OP_GREATER_CONST: {
		label_op_greater_const:
			LfxValue constant = READ_LOCAL_CONSTANT(READ_SHORT());
			COMPARE_OP_WITH_RIGHT(constant, > );
			NEXT_INSTRUCTION;
		}
		case OP_LESS_CONST: {
		label_op_less_const:
			LfxValue constant = READ_LOCAL_CONSTANT(READ_SHORT());
			COMPARE_OP_WITH_RIGHT(constant, < );
			NEXT_INSTRUCTION;
		}
		case OP_GREATER_EQUAL_CONST: {
		label_op_greater_equal_const:
			LfxValue constant = READ_LOCAL_CONSTANT(READ_SHORT());
			COMPARE_OP_WITH_RIGHT(constant, >= );
			NEXT_INSTRUCTION;
		}
		case OP_LESS_EQUAL_CONST: {
		label_op_less_equal_const:
			LfxValue constant = READ_LOCAL_CONSTANT(READ_SHORT());
			COMPARE_OP_WITH_RIGHT(constant, <= );
			NEXT_INSTRUCTION;
		}
		case OP_EQUAL_CONST_NUMBER: {
		label_op_equal_const_number:
			//number only,16bits index into function's own constants
			LfxValue constant = READ_LOCAL_CONSTANT(READ_SHORT());
			vm->cache.stackTop[-1] = BOOL_VAL(valuesEqual(vm->cache.stackTop[-1], constant));
			NEXT_INSTRUCTION;
		}
		case OP_NOT_EQUAL_CONST_NUMBER: {
		label_op_not_equal_const_number:
			//number only,16bits index into function's own constants
			LfxValue constant = READ_LOCAL_CONSTANT(READ_SHORT());
			vm->cache.stackTop[-1] = BOOL_VAL(!valuesEqual(vm->cache.stackTop[-1], constant));
			NEXT_INSTRUCTION;
		}

		case OP_ADD_LOCAL: {
		label_op_add_local:
			uint32_t index = READ_SHORT();
			LfxValue local = frame->slots[index];

			// might cause gc,so can't decrease first
			if (IS_NUMBER(vm->cache.stackTop[-1]) && IS_NUMBER(local)) {
				vm->cache.stackTop[-1] = NUMBER_VAL(AS_NUMBER(vm->cache.stackTop[-1]) + AS_NUMBER(local));
				NEXT_INSTRUCTION;
			}
			else if (IS_STRING(vm->cache.stackTop[-1]) && IS_STRING(local)) {
				ObjString* result = connectString(AS_STRING(vm->cache.stackTop[-1]), AS_STRING(local));
				vm->cache.stackTop[-1] = OBJ_VAL(result);
				NEXT_INSTRUCTION;
			}

			RUNTIME_ERROR("Operands must be two numbers or two strings.");
		}
		case OP_SUBTRACT_LOCAL: {
		label_op_subtract_local:
			uint32_t index = READ_SHORT();
			LfxValue local = frame->slots[index];

			BINARY_OP_WITH_RIGHT(NUMBER_VAL, local, -);
			NEXT_INSTRUCTION;
		}
		case OP_MULTIPLY_LOCAL: {
		label_op_multiply_local:
			uint32_t index = READ_SHORT();
			LfxValue local = frame->slots[index];

			BINARY_OP_WITH_RIGHT(NUMBER_VAL, local, *);
			NEXT_INSTRUCTION;
		}
		case OP_DIVIDE_LOCAL: {
		label_op_divide_local:
			uint32_t index = READ_SHORT();
			LfxValue local = frame->slots[index];

			BINARY_OP_WITH_RIGHT(NUMBER_VAL, local, / );
			NEXT_INSTRUCTION;
		}
		case OP_MODULUS_LOCAL: {
		label_op_modulus_local:
			uint32_t index = READ_SHORT();
			LfxValue local = frame->slots[index];

			/* both operands must be numbers */
			if (IS_NUMBER(vm->cache.stackTop[-1]) && IS_NUMBER(local)) {
				/* compute the result in place */
				vm->cache.stackTop[-1] = NUMBER_VAL(fmod(AS_NUMBER(vm->cache.stackTop[-1]), AS_NUMBER(local)));
				NEXT_INSTRUCTION;
			}
			else {
				RUNTIME_ERROR("Operands must be numbers.");
			}
		}
		case OP_EQUAL_LOCAL: {
		label_op_equal_local:
			uint32_t index = READ_SHORT();
			LfxValue local = frame->slots[index];
			vm->cache.stackTop[-1] = BOOL_VAL(valuesEqual(vm->cache.stackTop[-1], local));
			NEXT_INSTRUCTION;
		}
		case OP_NOT_EQUAL_LOCAL: {
		label_op_not_equal_local:
			uint32_t index = READ_SHORT();
			LfxValue local = frame->slots[index];
			vm->cache.stackTop[-1] = BOOL_VAL(!valuesEqual(vm->cache.stackTop[-1], local));
			NEXT_INSTRUCTION;
		}
		case OP_GREATER_LOCAL: {
		label_op_greater_local:
			uint32_t index = READ_SHORT();
			LfxValue local = frame->slots[index];
			COMPARE_OP_WITH_RIGHT(local, > );
			NEXT_INSTRUCTION;
		}
		case OP_LESS_LOCAL: {
		label_op_less_local:
			uint32_t index = READ_SHORT();
			LfxValue local = frame->slots[index];
			COMPARE_OP_WITH_RIGHT(local, < );
			NEXT_INSTRUCTION;
		}
		case OP_GREATER_EQUAL_LOCAL: {
		label_op_greater_equal_local:
			uint32_t index = READ_SHORT();
			LfxValue local = frame->slots[index];
			COMPARE_OP_WITH_RIGHT(local, >= );
			NEXT_INSTRUCTION;
		}
		case OP_LESS_EQUAL_LOCAL: {
		label_op_less_equal_local:
			uint32_t index = READ_SHORT();
			LfxValue local = frame->slots[index];
			COMPARE_OP_WITH_RIGHT(local, <= );
			NEXT_INSTRUCTION;
		}

		//fused compare + jump-if-false:[op:8][localA:16][localB:16][offset:16]
		//no stack traffic, jump when compare is false
		case OP_JIF_LESS_LL: {
		label_op_jif_less_ll:
			uint32_t indexA = READ_SHORT();
			uint32_t indexB = READ_SHORT();
			uint16_t offset = READ_SHORT();
			LfxValue left = frame->slots[indexA];
			LfxValue right = frame->slots[indexB];
			JIF_CMP_WITH_RIGHT(left, right, offset, < );
		}
		case OP_JIF_LESS_EQUAL_LL: {
		label_op_jif_less_equal_ll:
			uint32_t indexA = READ_SHORT();
			uint32_t indexB = READ_SHORT();
			uint16_t offset = READ_SHORT();
			LfxValue left = frame->slots[indexA];
			LfxValue right = frame->slots[indexB];
			JIF_CMP_WITH_RIGHT(left, right, offset, <= );
		}
		case OP_JIF_EQUAL_LL: {
		label_op_jif_equal_ll:
			uint32_t indexA = READ_SHORT();
			uint32_t indexB = READ_SHORT();
			uint16_t offset = READ_SHORT();
			if (valuesEqual(frame->slots[indexA], frame->slots[indexB])) NEXT_INSTRUCTION;
			ip += offset;
			NEXT_INSTRUCTION;
		}
		case OP_JIF_NOT_EQUAL_LL: {
		label_op_jif_not_equal_ll:
			uint32_t indexA = READ_SHORT();
			uint32_t indexB = READ_SHORT();
			uint16_t offset = READ_SHORT();
			if (!valuesEqual(frame->slots[indexA], frame->slots[indexB])) NEXT_INSTRUCTION;
			ip += offset;
			NEXT_INSTRUCTION;
		}

		//fused compare + jump-if-false, number constant:[op:8][local:16][const:16][offset:16]
		//the constant is compile-time guaranteed number,only the local side is checked
		case OP_JIF_LESS_LC_NUMBER: {
		label_op_jif_less_lc_number:
			uint32_t index = READ_SHORT();
			LfxValue constant = READ_LOCAL_CONSTANT(READ_SHORT());
			uint16_t offset = READ_SHORT();
			LfxValue local = frame->slots[index];
			JIF_CMP_NUM_WITH_RIGHT(local, constant, offset, < );
		}
		case OP_JIF_LESS_EQUAL_LC_NUMBER: {
		label_op_jif_less_equal_lc_number:
			uint32_t index = READ_SHORT();
			LfxValue constant = READ_LOCAL_CONSTANT(READ_SHORT());
			uint16_t offset = READ_SHORT();
			LfxValue local = frame->slots[index];
			JIF_CMP_NUM_WITH_RIGHT(local, constant, offset, <= );
		}
		case OP_JIF_GREATER_LC_NUMBER: {
		label_op_jif_greater_lc_number:
			uint32_t index = READ_SHORT();
			LfxValue constant = READ_LOCAL_CONSTANT(READ_SHORT());
			uint16_t offset = READ_SHORT();
			LfxValue local = frame->slots[index];
			JIF_CMP_NUM_WITH_RIGHT(local, constant, offset, > );
		}
		case OP_JIF_GREATER_EQUAL_LC_NUMBER: {
		label_op_jif_greater_equal_lc_number:
			uint32_t index = READ_SHORT();
			LfxValue constant = READ_LOCAL_CONSTANT(READ_SHORT());
			uint16_t offset = READ_SHORT();
			LfxValue local = frame->slots[index];
			JIF_CMP_NUM_WITH_RIGHT(local, constant, offset, >= );
		}
		case OP_JIF_EQUAL_LC_NUMBER: {
		label_op_jif_equal_lc_number:
			uint32_t index = READ_SHORT();
			LfxValue constant = READ_LOCAL_CONSTANT(READ_SHORT());
			uint16_t offset = READ_SHORT();
			if (valuesEqual(frame->slots[index], constant)) NEXT_INSTRUCTION;
			ip += offset;
			NEXT_INSTRUCTION;
		}
		case OP_JIF_NOT_EQUAL_LC_NUMBER: {
		label_op_jif_not_equal_lc_number:
			uint32_t index = READ_SHORT();
			LfxValue constant = READ_LOCAL_CONSTANT(READ_SHORT());
			uint16_t offset = READ_SHORT();
			if (!valuesEqual(frame->slots[index], constant)) NEXT_INSTRUCTION;
			ip += offset;
			NEXT_INSTRUCTION;
		}

		//fused equality, non-number constant:[op:8][local:16][const:24][offset:16]
		case OP_JIF_EQUAL_LC: {
		label_op_jif_equal_lc:
			uint32_t index = READ_SHORT();
			LfxValue constant = READ_CONSTANT(READ_24bits());
			uint16_t offset = READ_SHORT();
			if (valuesEqual(frame->slots[index], constant)) NEXT_INSTRUCTION;
			ip += offset;
			NEXT_INSTRUCTION;
		}
		case OP_JIF_NOT_EQUAL_LC: {
		label_op_jif_not_equal_lc:
			uint32_t index = READ_SHORT();
			LfxValue constant = READ_CONSTANT(READ_24bits());
			uint16_t offset = READ_SHORT();
			if (!valuesEqual(frame->slots[index], constant)) NEXT_INSTRUCTION;
			ip += offset;
			NEXT_INSTRUCTION;
		}

		case OP_NOT_LOCAL: {
		label_op_not_local:
			uint32_t index = READ_SHORT();
			LfxValue local = frame->slots[index];

			stack_push(vm, BOOL_VAL(isFalsey(local)));
			NEXT_INSTRUCTION;
		}
		case OP_NEGATE_LOCAL: {
		label_op_negate_local:
			uint32_t index = READ_SHORT();
			LfxValue local = frame->slots[index];

			if (IS_NUMBER(local)) {
				stack_push(vm, NUMBER_VAL(-AS_NUMBER(local)));
				NEXT_INSTRUCTION;
			}
			else {
				RUNTIME_ERROR("Operand must be a number.");
			}
		}
		case OP_CEDE: {
		label_op_cede:
			//hand execution control back to the host; the frame/stack stay live
			frame->ip = ip;
			vm->ceding = 1;
			return INTERPRET_CEDE;
		}
		}
	}

	//the place the error happens
#undef UPDATE_FRAME
#undef READ_BYTE
#undef READ_SHORT
#undef READ_24bits
#undef READ_CONSTANT
#undef READ_LOCAL_CONSTANT
#undef BINARY_OP
#undef BINARY_OP_WITH_RIGHT
#undef COMPARE_OP
#undef COMPARE_OP_WITH_RIGHT
#undef JIF_CMP_WITH_RIGHT
#undef JIF_CMP_NUM_WITH_RIGHT
#undef RUNTIME_ERROR
}

bool interpret_compile(LfxVM* vm, C_STR source)
{
	if (vm->cache.frameCount != 0 || vm->ceding) return false;

#if LOG_COMPILE_TIMING
	uint64_t time_compile = get_milliseconds();
#endif

	ObjFunction* function = compile(source, TYPE_SCRIPT);
	if (function == NULL) return false;

#if LOG_COMPILE_TIMING
	double time_compile_f = (get_milliseconds() - time_compile);
	fprintf(stderr, "[Log] Finished compiling in %g ms.\n", time_compile_f);
#endif

	ObjClosure* closure = newClosure(function);
	stack_push(vm, OBJ_VAL(closure));

	if (!call(vm, closure, 0)) {
		stack_reset(vm);
		vm->ceding = 0;
		return false;
	}

	//now the frame/stack are live, so the VM can be ceded/resumed
	vm->ceding = 1;
	return true;
}

InterpretResult interpret_execute(LfxVM* vm)
{
	if (!vm_isCeding(vm) || vm->cache.frameCount == 0) return INTERPRET_RUNTIME_ERROR;

	vm->ceding = 0;

#if LOG_EXECUTE_TIMING
	uint64_t time_run = get_milliseconds();
#endif

	InterpretResult result = run(vm);

#if LOG_EXECUTE_TIMING
	double time_run_f = (get_milliseconds() - time_run);
	fprintf(stderr, "[Log] Finished executing in %g ms.\n", time_run_f);
#endif

	//CEDE keeps the ceded execution resumable; everything else is idle again
	if (result == INTERPRET_CEDE) {
		vm->ceding = 1;
	}
	else {
		//an imported module failing to compile aborts without runtimeError(vm, )
		if (vm->cache.frameCount != 0) stack_reset(vm);
		vm->ceding = 0;
	}
	return result;
}

InterpretResult interpret_repl(LfxVM* vm, C_STR source) {
	if (!interpret_compile(vm, source)) {
		stack_reset(vm);
		vm->ceding = 0;
		return INTERPRET_COMPILE_ERROR;
	}

	InterpretResult res = interpret_execute(vm);

	if (res == INTERPRET_CEDE) {
		//the REPL has no host to resume from: drop the ceded execution
		stack_reset(vm);
		vm->ceding = 0;
		res = INTERPRET_RUNTIME_ERROR;
	}
	else {
		stack_reset(vm);//clean stack after use
	}
	return res;
}