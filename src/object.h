/*
 * MIT License
 * Copyright (c) 2025-2026 IMSDcrueoft (https://github.com/IMSDcrueoft)
 * See LICENSE file in the root directory for full license text.
*/
#pragma once
#include "common.h"
#include "value.h"
#include "table.h"
#include "chunk.h"
#include "arena_bump.h"
#include <assert.h>

//compress the ptr to 48bits
#ifndef LOXFLUX_COMPRESS_OBJ_HEADER
#define LOXFLUX_COMPRESS_OBJ_HEADER 1
#endif // !LOXFLUX_COMPRESS_OBJ_HEADER

// no need to compress under 32bits
#if !LOXFLUX_BUILD_64BIT
#undef LOXFLUX_COMPRESS_OBJ_HEADER
#endif

typedef enum {
	//objects that don't gc
	OBJ_STRING,
	OBJ_NATIVE,
	OBJ_FUNCTION,

	//objects gc able
	OBJ_UPVALUE,
	OBJ_CLOSURE,
	OBJ_BOUND_METHOD,
	OBJ_CLASS,
	OBJ_INSTANCE,

	//iter
	OBJ_ITERATOR,

	//coroutine
	OBJ_FIBER,

	//string builder and array are typed arrays
	OBJ_STRING_BUILDER,
	//array
	OBJ_ARRAY,
	OBJ_ARRAY_F64,
	OBJ_ARRAY_F32,
	OBJ_ARRAY_U32,
	OBJ_ARRAY_I32,
	OBJ_ARRAY_U16,
	OBJ_ARRAY_I16,
	OBJ_ARRAY_U8,
	OBJ_ARRAY_I8,
} ObjType;

//@object.type()
typedef enum {
	TYPE_STRING_BOOL,
	TYPE_STRING_NIL,
	TYPE_STRING_NUMBER,
	TYPE_STRING_STRING,
	TYPE_STRING_STRING_BUILDER,
	TYPE_STRING_FUNCTION,
	TYPE_STRING_NATIVE,
	TYPE_STRING_CLASS,
	TYPE_STRING_OBJECT,
	TYPE_STRING_ITERATOR,
	TYPE_STRING_FIBER,
	TYPE_STRING_ARRAY,
	TYPE_STRING_ARRAY_F64,
	TYPE_STRING_ARRAY_F32,
	TYPE_STRING_ARRAY_U32,
	TYPE_STRING_ARRAY_I32,
	TYPE_STRING_ARRAY_U16,
	TYPE_STRING_ARRAY_I16,
	TYPE_STRING_ARRAY_U8,
	TYPE_STRING_ARRAY_I8,

	TYPE_STRING_COUNT,
} TypeStringType;

#if DEBUG_LOG_GC
extern const C_STR objTypeInfo[];
#endif

#if LOXFLUX_COMPRESS_OBJ_HEADER
struct Obj {
	union {
		struct
		{
			uint8_t type;
			uint8_t isMarked;
			uint8_t padding[6]; //high 48bits for ptr low48bits
		};
		uintptr_t boxedNext;	//gc list next ptr, stored in the upper 48 bits (nextPtr << 16); the low 16 bits hold type + isMarked
	};
};
#define OBJ_PTR_SET_NEXT(obj,nextPtr)	((obj)->boxedNext = ((obj)->boxedNext & UINT16_MAX) | ((uintptr_t)nextPtr << 16))
#define OBJ_PTR_GET_NEXT(obj)			(Obj*)((obj)->boxedNext >> 16)

static inline Obj stateLess_obj_header(ObjType objType) {
	Obj o = { .boxedNext = (uintptr_t)NULL << 16 };
	o.isMarked = 1;
	o.type = objType;
	return o;
}

#else
struct Obj {
	struct
	{
		uint8_t type;
		uint8_t isMarked;
	};
	struct Obj* next;
};
#define OBJ_PTR_SET_NEXT(obj,nextPtr)	((obj)->next = nextPtr)
#define OBJ_PTR_GET_NEXT(obj)			((obj)->next)

static inline Obj stateLess_obj_header(ObjType objType) {
	return (Obj) { .next = NULL, .isMarked = 1, .type = objType };
}

#endif

//inline cache slot: cached probe position of a table entry
//capacity==0 means "never filled" (a real table capacity is never 0) → guaranteed slow path
//u16 pairs: tables bigger than UINT16_MAX are simply not cached (backfill-side guard)
//pointer fields: method invoke cache (klass/closure); always NULL for field sites (GC marks non-NULL ones)
typedef struct {
	uint32_t capacity;
	uint32_t index;
	void* extraA; //invoke: ObjClass*
	void* extraB; //invoke: ObjClosure*
} InlineCacheSlot;

typedef struct {
	Obj obj;
	uint16_t arity;
	uint16_t upvalueCount;
	uint32_t id;
	Chunk chunk;			// function bytecodes

	ObjString* name;
	InlineCacheSlot* caches;			// inline cache slots, sized at compile time, runtime only overwrites (numbers + gc-marked pointers)
	uint32_t cacheCount;	// number of allocated slots (capped at UINT8_MAX: the slot operand is u8)

	uint32_t constantsCount; // number of constants in the FAM (sealed at endCompiler; code indexes are 16-bit)
	LfxValue constants[];
} ObjFunction;

_Static_assert(sizeof(ObjFunction) % sizeof(LfxValue) == 0, "ObjFunction size must be a multiple of sizeof(LfxValue) so the constants FAM stays aligned.");
_Static_assert(offsetof(ObjFunction, constants) % _Alignof(LfxValue) == 0, "ObjFunction FAM offset must be aligned to LfxValue alignment.");

#define CLOSED_OBJ_UPVALUE_LOCATION UINT32_MAX

typedef struct ObjUpvalue {
	Obj obj;
	LfxValue closed; //closed value
	//ptrdiff_t location_offset; // store the offset, so we can update location when stack grow
	LfxValue* location;
	struct ObjUpvalue* next;
} ObjUpvalue;

typedef struct {
	Obj obj;
	uint32_t upvalueCount;
	ObjUpvalue** upvalues;
	ObjFunction* function;
} ObjClosure;

typedef struct {
	Obj obj;
	LfxValue receiver;
	ObjClosure* method;
} ObjBoundMethod;

//argCount and argValues
typedef LfxValue(*LfxNativeFn)(LfxVM* vm, int argCount, LfxValue* args);

typedef struct {
	Obj obj;
	LfxNativeFn function;
} ObjNative;

typedef struct ObjClass {
	Obj obj;
	ObjString* name;
	Table methods;
	ObjClosure* initializer;//fast path cache
	struct ObjClass* superclass;//recorded by OP_INHERIT,walked by instanceof
} ObjClass;

typedef struct {
	Obj obj;
	ObjClass* klass;
	Table fields;
	//fields shadowing a method name is tracked by fields.isPoisoned (kept inside Table's padding
	//so ObjInstance stays 8+8+24 = 40 bytes)
} ObjInstance;

// get the poisoned flag of an instance, which is stored in the extendPayload of the fields table
#define INSTANCE_POISON(instance) ((instance)->fields.isPoisoned)

#define INVALID_OBJ_STRING_SYMBOL UINT32_MAX
struct ObjString {
	Obj obj;
	uint32_t symbol; // used to boost global hash table
	uint32_t length; // the real length,not include '\0'
	uint64_t hash; // the hash
	char chars[];
};

//begin at 8 and align to 8, when < 64,mul 2, then *1.5 and align 8
//ARRAYLIKE_MAX + 7 & ~7 => ARRAYLIKE_MAX
#define ARRAYLIKE_MAX 0xfffffff8
typedef struct {
	Obj obj;
	uint32_t length;
	uint32_t capacity;
	void* payload;
} ObjArray;

//opaque iterator object for foreach
typedef struct {
	Obj obj;
	uint8_t type;		// target type snapshot
	uint8_t isDone;
	uint16_t padding;

	uint32_t index;		// current index of array or hashTable
	Obj* target;
} ObjIterator;

//frames live in a no-GC heap array indexed by the dispatch loop; capacity grows
//geometrically up to LOXFLUX_FRAMES_MAX, and the dispatch loop must refresh its frame
//pointer after any call that may have grown the array
//CallFrame layout still matters for cache-line packing; re-verify dispatch
//benchmarks before trusting hot-loop numbers
typedef struct {
	ObjClosure* closure;

	LfxValue* slots; //first available slot
	//ptrdiff_t slotsOffset;// offset of the first slot in the vm stack, not a ptr, so that we can realloc the stack when needed
	
	// used to avoid register spill(x86 amd64)
	InlineCacheSlot* caches;// IC slots of frame->closure->function, resolved once per call to avoid per-op deref chains
	LfxValue* constants;//cached constants base of the current function,refreshed at every frame switch

	uint8_t* ip; 
} CallFrame;

// status of fiber
typedef enum {
	FIBER_PENDING,
	FIBER_SETTLED,
	FIBER_FAILED,
	FIBER_CANCELLED
} FiberStatus;

typedef struct {
	LfxValue* stack;
	//saved stack top when this fiber is suspended; VM keeps its own stackTop
	//cache while this fiber is current
	LfxValue* stackTop;
	//the only push boundary: the committed edge of the stack arena; crossing it
	//means commit more pages or (at the VA end = currentFiber->stackArena.capacity) overflow
	LfxValue* stackBoundary;

	CallFrame* frames;
	uint32_t frameCount;
	uint32_t frameCapacity;

	ObjUpvalue* openUpvalues;
} FiberCore;

typedef struct ObjFiber {
	Obj obj;

	FiberCore core;

	// caller of ObjFiber(need gc track)
	struct ObjFiber* resumer;

	FiberStatus status;
	// is running or not (avoid loop call)
	uint32_t isRunning;

	//result of coroutine or catched error
	LfxValue result;

	//fixed-base backing storage for the stack (VA never moves, lazy commit)
	ArenaBumpAllocator stackArena;
} ObjFiber;

#define OBJ_GET_TYPE(obj)			((obj).type)
#define OBJ_SET_TYPE(obj,objType)	((obj).type = objType)
#define OBJ_PTR_GET_TYPE(obj)		((obj)->type)

#define OBJ_TYPE(value)				OBJ_PTR_GET_TYPE(AS_OBJ(value))
#define IS_CLOSURE(value)			isObjType(value, OBJ_CLOSURE)
#define IS_FUNCTION(value)			isObjType(value, OBJ_FUNCTION)
#define IS_NATIVE(value)			isObjType(value, OBJ_NATIVE)
#define IS_BOUND_METHOD(value)		isObjType(value, OBJ_BOUND_METHOD)
#define IS_CLASS(value)				isObjType(value, OBJ_CLASS)
#define IS_INSTANCE(value)			isObjType(value, OBJ_INSTANCE)
#define IS_FIBER(value)				isObjType(value, OBJ_FIBER)
#define IS_ITERATOR(value)			isObjType(value, OBJ_ITERATOR)
#define IS_STRING(value)			isObjType(value, OBJ_STRING)
#define IS_STRING_BUILDER(value)	isObjType(value, OBJ_STRING_BUILDER)
#define IS_ARRAY(value)				isObjType(value, OBJ_ARRAY)

#define OBJ_IS_TYPE(array, arrayType)		(OBJ_GET_TYPE(array->obj) == arrayType)
#define ARRAY_ELEMENT(array, type, index)	(((type*)array->payload)[index])
#define ARRAY_IN_RANGE(array, index)		((index >= 0) && (index < array->length))

//element load/store switch inlined at the call site: array/index/val are
//expanded per case, so pass side-effect-free lvalues; illegal types are only
//caught by the debug assert
#define GET_INDEXABLE_ELEMENT(array, index, out) { \
	uint8_t t = OBJ_GET_TYPE((array)->obj); \
	assert(t >= OBJ_STRING_BUILDER && t <= OBJ_ARRAY_I8); \
	switch (t) { \
	case OBJ_STRING_BUILDER: out = NUMBER_VAL(ARRAY_ELEMENT(array, uint8_t, index)); break; \
	case OBJ_ARRAY:          out = ARRAY_ELEMENT(array, LfxValue, index); break; \
	case OBJ_ARRAY_F64:      out = NUMBER_VAL(ARRAY_ELEMENT(array, double, index)); break; \
	case OBJ_ARRAY_F32:      out = NUMBER_VAL(ARRAY_ELEMENT(array, float, index)); break; \
	case OBJ_ARRAY_U32:      out = NUMBER_VAL(ARRAY_ELEMENT(array, uint32_t, index)); break; \
	case OBJ_ARRAY_I32:      out = NUMBER_VAL(ARRAY_ELEMENT(array, int32_t, index)); break; \
	case OBJ_ARRAY_U16:      out = NUMBER_VAL(ARRAY_ELEMENT(array, uint16_t, index)); break; \
	case OBJ_ARRAY_I16:      out = NUMBER_VAL(ARRAY_ELEMENT(array, int16_t, index)); break; \
	case OBJ_ARRAY_U8:       out = NUMBER_VAL(ARRAY_ELEMENT(array, uint8_t, index)); break; \
	case OBJ_ARRAY_I8:       out = NUMBER_VAL(ARRAY_ELEMENT(array, int8_t, index)); break; \
	} \
}

	//value slots store raw; typed slots store 0 for non-numbers
#define SET_INDEXABLE_ELEMENT(array, index, val) { \
	uint8_t t = OBJ_GET_TYPE((array)->obj); \
	assert(t >= OBJ_STRING_BUILDER && t <= OBJ_ARRAY_I8); \
	switch (t) { \
	case OBJ_ARRAY:     ARRAY_ELEMENT(array, LfxValue, index) = val; break; \
	case OBJ_ARRAY_F64: ARRAY_ELEMENT(array, double, index) = IS_NUMBER(val) ? AS_NUMBER(val) : 0.0; break; \
	case OBJ_ARRAY_F32: ARRAY_ELEMENT(array, float, index) = IS_NUMBER(val) ? double_to_f32(AS_NUMBER(val)) : 0.0f; break; \
	case OBJ_ARRAY_U32: ARRAY_ELEMENT(array, uint32_t, index) = IS_NUMBER(val) ? double_to_u32(AS_NUMBER(val)) : 0; break; \
	case OBJ_ARRAY_I32: ARRAY_ELEMENT(array, int32_t, index) = IS_NUMBER(val) ? double_to_i32(AS_NUMBER(val)) : 0; break; \
	case OBJ_ARRAY_U16: ARRAY_ELEMENT(array, uint16_t, index) = IS_NUMBER(val) ? (uint16_t)double_to_u32(AS_NUMBER(val)) : 0; break; \
	case OBJ_ARRAY_I16: ARRAY_ELEMENT(array, int16_t, index) = IS_NUMBER(val) ? (int16_t)double_to_i32(AS_NUMBER(val)) : 0; break; \
	case OBJ_ARRAY_U8:  ARRAY_ELEMENT(array, uint8_t, index) = IS_NUMBER(val) ? (uint8_t)double_to_u32(AS_NUMBER(val)) : 0; break; \
	case OBJ_ARRAY_I8:  ARRAY_ELEMENT(array, int8_t, index) = IS_NUMBER(val) ? (int8_t)double_to_i32(AS_NUMBER(val)) : 0; break; \
	} \
}

#define AS_CLOSURE(value)			((ObjClosure*)AS_OBJ(value))
#define AS_FUNCTION(value)			((ObjFunction*)AS_OBJ(value))
#define AS_BOUND_METHOD(value)		((ObjBoundMethod*)AS_OBJ(value))
#define AS_CLASS(value)				((ObjClass*)AS_OBJ(value))
#define AS_INSTANCE(value)			((ObjInstance*)AS_OBJ(value))
#define AS_FIBER(value)				((ObjFiber*)AS_OBJ(value))
#define AS_ITERATOR(value)			((ObjIterator*)AS_OBJ(value))
#define AS_NATIVE(value)			(((ObjNative*)AS_OBJ(value))->function)
#define AS_STRING(value)			((ObjString*)AS_OBJ(value))
#define AS_ARRAY(value)				((ObjArray*)AS_OBJ(value))

static inline bool isObjType(LfxValue value, ObjType type) {
	return IS_OBJ(value) && AS_OBJ(value)->type == type;
}

//array and stringBuilder (not including const string)
static inline bool isArrayLike(LfxValue value) {
	return IS_OBJ(value) && AS_OBJ(value)->type >= OBJ_ARRAY;//enum type
}

static inline bool isIndexableArray(LfxValue value) {
	return IS_OBJ(value) && AS_OBJ(value)->type >= OBJ_STRING_BUILDER;//enum type
}

static inline bool isTypedArray(LfxValue value) {
	return IS_OBJ(value) && (AS_OBJ(value)->type >= OBJ_ARRAY_F64);//enum type
}

//string or stringBuilder
static inline bool isStringLike(LfxValue value) {
	return IS_STRING(value) || IS_STRING_BUILDER(value);
}

ObjString* copyString(LfxVM* vm, C_STR chars, uint32_t length, bool escapeChars);
ObjString* connectString(LfxVM* vm, ObjString* strA, ObjString* strB);

//memcmp-style three-way ordering for string/stringBuilder pairs (assumes isStringLike on both):
//byte compare over the common prefix, then the shorter operand is the smaller one
int compareStringLike(LfxValue a, LfxValue b);

//stream-targeted core: renders an object value (class/instance/function/array/...)
void printObjectTo(FILE* stream, LfxValue value, bool isExpand);

ObjUpvalue* newUpvalue(LfxVM* vm, LfxValue* slot);
struct Compiler;
ObjFunction* newFunction(LfxVM* vm, struct Compiler* compiler);
ObjClosure* newClosure(LfxVM* vm, ObjFunction* function);
ObjBoundMethod* newBoundMethod(LfxVM* vm, LfxValue receiver, ObjClosure* method);
ObjNative* newNative(LfxVM* vm, LfxNativeFn function);
ObjClass* newClass(LfxVM* vm, ObjString* name);
ObjInstance* newInstance(LfxVM* vm, ObjClass* klass);
ObjFiber* newFiber(LfxVM* vm);
ObjArray* newArray(LfxVM* vm, ObjType type);
ObjIterator* newIterator(LfxVM* vm, Obj* target);

void reserveArray(LfxVM* vm, ObjArray* array, uint64_t size);

//extracts a NUL-terminated C buffer from a string-like value: strings return
//their internal chars, stringBuilders return their payload (every mutator
//keeps a terminator right past the length, asserted in debug builds) — the
//result is owned by the value, never free it. NULL for any other value kind.
C_STR getBufferFromStringLike(LfxValue value);

#undef LOXFLUX_COMPRESS_OBJ_HEADER