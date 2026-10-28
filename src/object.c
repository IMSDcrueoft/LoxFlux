/*
 * MIT License
 * Copyright (c) 2025-2026 IMSDcrueoft (https://github.com/IMSDcrueoft)
 * See LICENSE file in the root directory for full license text.
*/
#include "object.h"
#include "compiler.h"
#include "vm.h"
#include "hash.h"
#include "memory.h"
#include "gc.h"
#include "allocator.h"
#include <assert.h>

#if DEBUG_LOG_GC
const C_STR objTypeInfo[] = {
	[OBJ_CLASS] = {"class"},
	[OBJ_CLOSURE] = {"closure"},
	[OBJ_FUNCTION] = {"function"},
	[OBJ_NATIVE] = {"native"},
	[OBJ_UPVALUE] = {"upValue"},
	[OBJ_STRING] = {"string"},
	[OBJ_BOUND_METHOD] = {"boundMethod"},
	[OBJ_INSTANCE] = {"instance"},
	[OBJ_ITERATOR] = {"iterator"},
	[OBJ_FIBER] = {"fiber"},
	[OBJ_STRING_BUILDER] = {"stringBuilder"},
	[OBJ_ARRAY] = {"array"},
	[OBJ_ARRAY_F64] = {"arrayF64"},
	[OBJ_ARRAY_F32] = {"arrayF32"},
	[OBJ_ARRAY_U32] = {"arrayU32"},
	[OBJ_ARRAY_I32] = {"arrayI32"},
	[OBJ_ARRAY_U16] = {"arrayU16"},
	[OBJ_ARRAY_I16] = {"arrayI16"},
	[OBJ_ARRAY_U8] = {"arrayU8"},
	[OBJ_ARRAY_I8] = {"arrayI8"},
};
#endif

#define ALLOCATE_OBJ(type, objectType) \
    (type*)allocateObject(sizeof(type), objectType)

#define ALLOCATE_FLEX_OBJ(type,objectType,byteSize) \
    (type*)allocateObject(byteSize, objectType)

HOT_FUNCTION
static Obj* allocateObject(size_t size, ObjType type) {
	Obj* object = NULL;

	//link the objects
	switch (type) {
	case OBJ_STRING: {
		//strings come from the fixed-VA string arena (append-only bump, the
		//whole arena is released at vm exit); they are NOT linked into the
		//object lists anymore, the arena owns their lifetime
		ObjString* string = (ObjString*)arenaBump_alloc(&vm.stringArena, size);
		if (string == NULL) {
			fprintf(stderr, "Memory reallocation failed!\n");
			exit(1);
		}

		memset(&string->obj, 0, sizeof(Obj));
		string->obj.type = OBJ_STRING;
		object = (Obj*)string;
		break;
	}
	case OBJ_FUNCTION:
	case OBJ_NATIVE: {
		object = (Obj*)reallocate_no_gc(NULL, 0, size);
		OBJ_PTR_SET_NEXT(object, vm.objects_no_gc);
		object->type = type;
		object->isMarked = !vm.gcMark;
		vm.objects_no_gc = object;
		break;
	}
	default:
		object = (Obj*)reallocate(NULL, 0, size);
		OBJ_PTR_SET_NEXT(object, vm.objects);
		object->type = type;
		object->isMarked = !vm.gcMark;
		vm.objects = object;
		break;
	}

#if DEBUG_LOG_GC
	fprintf(stderr, "[gc] %p allocate %zu for (%s)\n", (void*)object, size, objTypeInfo[type]);
#endif

	return object;
}

HOT_FUNCTION
ObjUpvalue* newUpvalue(LfxValue* slot)
{
	ObjUpvalue* upvalue = ALLOCATE_OBJ(ObjUpvalue, OBJ_UPVALUE);
	upvalue->location = slot;
	//upvalue->location_offset = offset;
	upvalue->closed = NIL_VAL;
	upvalue->next = NULL;
	return upvalue;
}

ObjFunction* newFunction(Compiler* compiler) {
	// materialize the final function from the compiler's embedded state (read-only:
	// the compiler owns the embedded state and detaches the moved buffers itself)
	// (ALLOCATE_FLEX_OBJ takes the total size: header + FAM; links into vm.objects_no_gc)
	uint32_t constantsCount = compiler->constants.count;
	ObjFunction* function = ALLOCATE_FLEX_OBJ(ObjFunction, OBJ_FUNCTION, sizeof(ObjFunction) + sizeof(LfxValue) * constantsCount);
	Obj header = function->obj;//keep list link/type/mark state set by allocateObject
	memcpy(function, &compiler->function, sizeof(ObjFunction));//carries id/name/arity/upvalueCount/chunk/caches over
	function->obj = header;

	memcpy(function->constants, compiler->constants.values, sizeof(LfxValue) * constantsCount);//always copy, count may be 0
	function->constantsCount = constantsCount;
	function->cacheCount = compiler->cacheCount;//real slot count for the VM

	return function;
}

HOT_FUNCTION
ObjClosure* newClosure(ObjFunction* function) {
	ObjUpvalue** upvalues = ALLOCATE(ObjUpvalue*, function->upvalueCount);
	for (uint32_t i = 0; i < function->upvalueCount; i++) {
		upvalues[i] = NULL;
	}

	ObjClosure* closure = ALLOCATE_OBJ(ObjClosure, OBJ_CLOSURE);
	closure->function = function;
	closure->upvalues = upvalues;
	closure->upvalueCount = function->upvalueCount;

	return closure;
}

HOT_FUNCTION
ObjBoundMethod* newBoundMethod(LfxValue receiver, ObjClosure* method)
{
	ObjBoundMethod* bound = ALLOCATE_OBJ(ObjBoundMethod, OBJ_BOUND_METHOD);
	bound->receiver = receiver;
	bound->method = method;
	return bound;
}

//create native function
ObjNative* newNative(LfxNativeFn function) {
	ObjNative* native = ALLOCATE_OBJ(ObjNative, OBJ_NATIVE);
	native->function = function;
	return native;
}

ObjClass* newClass(ObjString* name)
{
	ObjClass* klass = ALLOCATE_OBJ(ObjClass, OBJ_CLASS);
	klass->name = name;
	klass->initializer = NIL_VAL;
	klass->methods.isGlobal = false;
	klass->methods.isFrozen = false;
	klass->methods.isEternal = false;
	klass->methods.isPoisoned = false;
	klass->superclass = NULL;
	table_init(&klass->methods);
	return klass;
}

HOT_FUNCTION
ObjInstance* newInstance(ObjClass* klass) {
	ObjInstance* instance = ALLOCATE_OBJ(ObjInstance, OBJ_INSTANCE);
	instance->klass = klass;
	instance->fields.isGlobal = false;
	instance->fields.isFrozen = false;
	instance->fields.isEternal = false;
	instance->fields.isPoisoned = false;
	table_init(&instance->fields);
	return instance;
}

ObjFiber* newFiber()
{
	ObjFiber* fiber = allocFiber();

	//allocFiber() may return a recycled fiber, so initialize every execution
	//field here; the stack arena itself is owned/reused by memory.c
	fiber->core.stack = (LfxValue*)fiber->stackArena.base;
	fiber->core.stackTop = fiber->core.stack;
	fiber->core.stackBoundary = (LfxValue*)((char*)fiber->stackArena.base + fiber->stackArena.commitTop);

	fiber->core.frames = ALLOCATE_NO_GC(CallFrame, LOXFLUX_FRAMES_INIT);
	fiber->core.frameCount = 0;
	fiber->core.frameCapacity = LOXFLUX_FRAMES_INIT;

	fiber->core.openUpvalues = NULL;
	fiber->resumer = NULL;
	fiber->status = FIBER_PENDING;
	fiber->isRunning = 0;
	fiber->result = NIL_VAL;

	return fiber;
}

HOT_FUNCTION
ObjArray* newArray(ObjType type) {
	ObjArray* array = ALLOCATE_OBJ(ObjArray, type);
	array->capacity = 0;
	array->length = 0;
	array->payload = NULL;
	return array;
}

HOT_FUNCTION
ObjIterator* newIterator(Obj* target)
{
	ObjIterator* iterator = ALLOCATE_OBJ(ObjIterator, OBJ_ITERATOR);
	iterator->type = OBJ_PTR_GET_TYPE(target);
	iterator->isDone = false;
	iterator->index = 0;
	iterator->target = target;
	return iterator;
}

HOT_FUNCTION
void reserveArray(ObjArray* array, uint64_t size)
{
	size = (size + 7) & ~7;
	if (size < array->capacity) return;
	if (size > ARRAYLIKE_MAX) {
		fprintf(stderr, "ArrayLike size overflow");
		exit(1);
	}

#define GROW_TYPED_ARRAY(type, ptr, size) reallocate(ptr, sizeof(type) * array->capacity, sizeof(type) * size)
	void* newPayload = NULL;

	switch (OBJ_GET_TYPE(array->obj)) {
	case OBJ_ARRAY:
		newPayload = GROW_TYPED_ARRAY(LfxValue, array->payload, size);
		break;
	case OBJ_ARRAY_F64:
		newPayload = GROW_TYPED_ARRAY(double, array->payload, size);
		break;
	case OBJ_ARRAY_F32:
		newPayload = GROW_TYPED_ARRAY(float, array->payload, size);
		break;
	case OBJ_ARRAY_U32:
		newPayload = GROW_TYPED_ARRAY(uint32_t, array->payload, size);
		break;
	case OBJ_ARRAY_I32:
		newPayload = GROW_TYPED_ARRAY(int32_t, array->payload, size);
		break;
	case OBJ_ARRAY_U16:
		newPayload = GROW_TYPED_ARRAY(uint16_t, array->payload, size);
		break;
	case OBJ_ARRAY_I16:
		newPayload = GROW_TYPED_ARRAY(int16_t, array->payload, size);
		break;
	case OBJ_ARRAY_U8:
		newPayload = GROW_TYPED_ARRAY(uint8_t, array->payload, size);
		break;
	case OBJ_ARRAY_I8:
		newPayload = GROW_TYPED_ARRAY(int8_t, array->payload, size);
		break;
	case OBJ_STRING_BUILDER:
		newPayload = GROW_TYPED_ARRAY(char, array->payload, size);
		break;
	}

	array->payload = newPayload;
	array->capacity = (uint32_t)size;
#undef GROW_TYPED_ARRAY
}

//If a duplicate is found, return it; otherwise, return null.
static inline ObjString* deduplicateString(C_STR chars, uint32_t length, uint64_t hash) {
	return tableFindString(&vm.strings, chars, length, hash);
}

//will check '\\' '\"' '\n' '\t'
ObjString* copyString(C_STR chars, uint32_t length, bool escapeChars)
{
	uint32_t heapSize = sizeof(ObjString) + 1;
	ObjString* string = NULL;
	uint64_t hash;

	if (!escapeChars) {
		hash = HASH_64bits(chars, length);
		string = deduplicateString(chars, length, hash);
		if (string == NULL) {
			//create string
			heapSize += length;
			string = ALLOCATE_FLEX_OBJ(ObjString, OBJ_STRING, heapSize);

			memcpy(string->chars, chars, length);
			string->chars[length] = '\0';
			string->length = length;

			string->hash = hash;
			string->symbol = INVALID_OBJ_STRING_SYMBOL;

			//stack_push(OBJ_VAL(string));
			tableSet_string(&vm.strings, string);
			//stack_pop();
		}

		return string;
	}
	else {
		//the unescaped content is never longer than the source, so the source
		//length bounds the scratch need; short literals stay on the stack,
		//long ones use a transient heap buffer (always released below)
		char stackScratch[512];
		char* scratch = NULL;
		char* content = stackScratch;
		if (length > (uint32_t)sizeof(stackScratch)) {
			scratch = ALLOCATE_NO_GC(char, length);
			content = scratch;
		}

		//single fused pass: unescape into the scratch (writeIndex ends up as
		//the unescaped length; unescaping never grows the content)
		uint32_t writeIndex = 0;
		for (uint32_t readIndex = 0; readIndex < length;) {
			//copy the run of plain characters
			uint32_t start = readIndex;
			while (readIndex < length && chars[readIndex] != '\\') {
				++readIndex;
			}
			if (start < readIndex) {
				memcpy(content + writeIndex, chars + start, readIndex - start);
				writeIndex += readIndex - start;
			}

			if (readIndex >= length) {
				break;
			}

			++readIndex;//consume the backslash
			if (readIndex < length) {
				switch (chars[readIndex]) {
				case '\\':
				case '\"':
					content[writeIndex++] = chars[readIndex];
					break;
				case 'n':
					content[writeIndex++] = '\n';
					break;
				case 't':
					content[writeIndex++] = '\t';
					break;
				default:
					//unknown escape: keep both characters verbatim
					content[writeIndex++] = '\\';
					content[writeIndex++] = chars[readIndex];
					break;
				}
				++readIndex;
			}
			else {
				//trailing backslash: keep it verbatim (unreachable via the
				//scanner, which treats \" as an escaped quote)
				content[writeIndex++] = '\\';
			}
		}

		//probe the pool with the unescaped content: a hit costs zero allocations
		const uint64_t escapedHash = HASH_64bits(content, writeIndex);
		ObjString* interned = deduplicateString(content, writeIndex, escapedHash);
		if (interned == NULL) {
		//genuinely new content: allocate, copy, intern
		string = ALLOCATE_FLEX_OBJ(ObjString, OBJ_STRING, sizeof(ObjString) + 1 + writeIndex);
		memcpy(string->chars, content, writeIndex);
			string->chars[writeIndex] = '\0';
			string->length = writeIndex;
			string->hash = escapedHash;
			string->symbol = INVALID_OBJ_STRING_SYMBOL;
		}

		if (scratch != NULL) {
			FREE_ARRAY_NO_GC(char, scratch, length);
		}

		if (interned != NULL) {
			return interned;
		}

		tableSet_string(&vm.strings, string);
		return string;
	}
}

ObjString* connectString(ObjString* strA, ObjString* strB) {
	const uint32_t length = strA->length + strB->length;

	//hash the two segments without materializing the joined content
	HashStream stream;
	hashStreamBegin(&stream, length);
	hashStreamUpdate(&stream, strA->chars, strA->length);
	hashStreamUpdate(&stream, strB->chars, strB->length);
	const uint64_t hash = hashStreamFinalize(&stream);

	//probe first: a dedup hit costs zero allocations and no list surgery
	ObjString* interned = tableFindConcatString(&vm.strings, strA->chars, strA->length, strB->chars, strB->length, hash);
	if (interned != NULL) {
		return interned;
	}

	//genuinely new content: allocate, assemble, intern
	ObjString* string = ALLOCATE_FLEX_OBJ(ObjString, OBJ_STRING, sizeof(ObjString) + length + 1);

	memcpy(string->chars, strA->chars, strA->length);
	memcpy(string->chars + strA->length, strB->chars, strB->length);
	string->chars[length] = '\0';
	string->length = length;
	string->hash = hash;
	string->symbol = INVALID_OBJ_STRING_SYMBOL;

	tableSet_string(&vm.strings, string);
	return string;
}

//memcmp-style three-way ordering for string/stringBuilder pairs:
//byte compare over the common prefix, then the shorter operand is the smaller one
int compareStringLike(LfxValue a, LfxValue b) {
	C_STR charsA;
	C_STR charsB;
	uint32_t lengthA;
	uint32_t lengthB;

	if (IS_STRING(a)) {
		ObjString* string = AS_STRING(a);
		charsA = string->chars;
		lengthA = string->length;
	}
	else {//stringBuilder
		ObjArray* stringBuilder = AS_ARRAY(a);
		charsA = (C_STR)stringBuilder->payload;
		lengthA = stringBuilder->length;
	}

	if (IS_STRING(b)) {
		ObjString* string = AS_STRING(b);
		charsB = string->chars;
		lengthB = string->length;
	}
	else {//stringBuilder
		ObjArray* stringBuilder = AS_ARRAY(b);
		charsB = (C_STR)stringBuilder->payload;
		lengthB = stringBuilder->length;
	}

	uint32_t common = (lengthA < lengthB) ? lengthA : lengthB;
	int cmp = memcmp(charsA, charsB, common);
	if (cmp != 0) return cmp;
	return (lengthA < lengthB) ? -1 : ((lengthA > lengthB) ? 1 : 0);
}

static void printFunction(FILE* stream, ObjFunction* function) {
	if (function->name == NULL) {
		fprintf(stream, "<script> (%d)", function->id);
	}
	else if (function->name->length == 0) {
		fprintf(stream, "<lambda> (%d)", function->id);
	}
	else {
		fprintf(stream, "<fn %s> (%d)", function->name->chars, function->id);
	}
}

static void printArrayLike(FILE* stream, ObjArray* array, bool isExpand) {
	if (OBJ_GET_TYPE(array->obj) == OBJ_STRING_BUILDER) {
		if (array->payload != NULL) {
			fprintf(stream, "%s", (STR)array->payload);
		}
		else {
			fprintf(stream, "<empty stringBuilder>");
		}
		return;
	}

	if (!isExpand) {
		switch (OBJ_GET_TYPE(array->obj)) {
		case OBJ_ARRAY:
			fprintf(stream, "<array>");
			break;
		case OBJ_ARRAY_F64:
			fprintf(stream, "<array-f64>");
			break;
		case OBJ_ARRAY_F32:
			fprintf(stream, "<array-f32>");
			break;
		case OBJ_ARRAY_U32:
			fprintf(stream, "<array-u32>");
			break;
		case OBJ_ARRAY_I32:
			fprintf(stream, "<array-i32>");
			break;
		case OBJ_ARRAY_U16:
			fprintf(stream, "<array-u16>");
			break;
		case OBJ_ARRAY_I16:
			fprintf(stream, "<array-i16>");
			break;
		case OBJ_ARRAY_U8:
			fprintf(stream, "<array-u8>");
			break;
		case OBJ_ARRAY_I8:
			fprintf(stream, "<array-i8>");
			break;
		}
	}
	else {
		if (array->length > 0) {
			char buffer[40];

			fprintf(stream, "[ ");
			for (uint32_t i = 0; i < array->length;) {
				switch (OBJ_GET_TYPE(array->obj)) {
				case OBJ_ARRAY: //don't expand now ,or it's too slow and too much
					printValue(stream, ARRAY_ELEMENT(array, LfxValue, i), false);
					break;
				case OBJ_ARRAY_F64: {
					convert_adaptive_double(ARRAY_ELEMENT(array, double, i), buffer, sizeof(buffer));
					fprintf(stream, "%s", buffer);
					break;
				}
				case OBJ_ARRAY_F32: {
					convert_adaptive_double(ARRAY_ELEMENT(array, float, i), buffer, sizeof(buffer));
					fprintf(stream, "%s", buffer);
					break;
				}
				case OBJ_ARRAY_U32:
					fprintf(stream, "%u", ARRAY_ELEMENT(array, uint32_t, i));
					break;
				case OBJ_ARRAY_I32:
					fprintf(stream, "%d", ARRAY_ELEMENT(array, int32_t, i));
					break;
				case OBJ_ARRAY_U16:
					fprintf(stream, "%d", ARRAY_ELEMENT(array, uint16_t, i));
					break;
				case OBJ_ARRAY_I16:
					fprintf(stream, "%d", ARRAY_ELEMENT(array, int16_t, i));
					break;
				case OBJ_ARRAY_U8:
					fprintf(stream, "%d", ARRAY_ELEMENT(array, uint8_t, i));
					break;
				case OBJ_ARRAY_I8:
					fprintf(stream, "%d", ARRAY_ELEMENT(array, int8_t, i));
					break;
				}

				if (++i < array->length) {
					fprintf(stream, ", ");
				}
			}
			fprintf(stream, " ]");
		}
		else {
			fprintf(stream, "[]");
		}
	}
}

void printObjectTo(FILE* stream, LfxValue value, bool isExpand) {
	switch (OBJ_TYPE(value)) {
	case OBJ_CLASS: {
		ObjString* name = AS_CLASS(value)->name;
		if (name != NULL) {
			fprintf(stream, "%s (class)", name->chars);
		}
		else {
			fprintf(stream, "$anon (class)");
		}
		break;
	}
	case OBJ_INSTANCE: {
		//klass is never NULL: class-less instances point at vm.defaultClass
		fprintf(stream, "%s (instance)", AS_INSTANCE(value)->klass->name->chars);
		break;
	}
	case OBJ_BOUND_METHOD:
		printFunction(stream, AS_BOUND_METHOD(value)->method->function);
		break;
	case OBJ_CLOSURE:
		printFunction(stream, AS_CLOSURE(value)->function);
		break;
	case OBJ_FUNCTION:
		printFunction(stream, AS_FUNCTION(value));
		break;
	case OBJ_NATIVE:
		fprintf(stream, "<native fn>");
		break;
	case OBJ_STRING:
		fprintf(stream, "%s", AS_STRING(value)->chars);
		break;
	case OBJ_UPVALUE:
		fprintf(stream, "<upvalue>");
		break;
	case OBJ_FIBER:
		fprintf(stream, "<fiber>");
		break;
	case OBJ_ITERATOR:
		fprintf(stream, "<iterator>");
		break;
	default: {
		if (isIndexableArray(value)) {
			ObjArray* array = AS_ARRAY(value);
			printArrayLike(stream, array, isExpand);
		}
		break;
	}
	}
}

StringEntry* getStringEntryInPool(ObjString* string)
{
	return tableGetStringEntry(&vm.strings, string);
}

//string and stringBuilder both carry raw bytes and every mutator (ctor,
//append, slice, readFile) keeps a terminator right past the length: return
//the internal buffer as-is, no copy needed
C_STR getBufferFromStringLike(LfxValue value)
{
	if (IS_STRING(value)) {
		return AS_STRING(value)->chars;
	}

	if (IS_STRING_BUILDER(value)) {
		ObjArray* builder = AS_ARRAY(value);

		//a violated invariant here is an internal bug, not user input
		assert(builder->payload != NULL && builder->capacity > builder->length && ((const char*)builder->payload)[builder->length] == '\0');

		return builder->payload;
	}

	return NULL;
}