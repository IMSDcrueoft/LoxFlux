/*
 * MIT License
 * Copyright (c) 2025 IMSDcrueoft (https://github.com/IMSDcrueoft)
 * See LICENSE file in the root directory for full license text.
*/
#include "vm.h"
#include "object.h"
#include "gc.h"
#include "file.h"
#include "allocator.h"

#if DEBUG_TRACE_EXECUTION
#include "debug.h"
#endif

#if LOG_MODE
#include "timer.h"
#endif

//the global shared vm
VM vm;

COLD_FUNCTION
static void stack_reset()
{
	//reset the pointer
	vm.stackTop = vm.stack;

	//set all to nil
	for (Value* ptr = vm.stack; ptr < vm.stackBoundary; ++ptr) {
		*ptr = NIL_VAL;
	}

	vm.frameCount = 0;
	vm.openUpvalues = NULL;
}

COLD_FUNCTION
static ObjFunction* getCachedScript(STR absolutePath) {
	// only to seek with absolute path
	ObjString* path = copyString(absolutePath, (uint32_t)strlen(absolutePath), false);

	// get deduplicate script
	StringEntry* entry = tableGetScriptEntry(&vm.scripts, path);
	ObjFunction* function = (entry != NULL) ? AS_FUNCTION(vm.constants.values[entry->index]) : NULL;

	//only add here, so no need check index
	if (function == NULL) {
		//this readFile function never return null
		STR source = readFile(absolutePath);

		function = compile(source, TYPE_MODULE);
		mem_free(source);// free memory

		if (function != NULL) {
			// add to pool
			tableSet_script(&vm.scripts, path, addConstant(OBJ_VAL(function)));
		}
	}

	return function;
}

COLD_FUNCTION
static bool throwError(Value error, C_STR format, ...) {
	fprintf(stderr, "[RuntimeError] ");

	va_list args;
	va_start(args, format);
	vfprintf(stderr, format, args);
	va_end(args);
	fputs("\n", stderr);

	for (int32_t i = vm.frameCount - 1; i >= 0; i--) {
		CallFrame* frame = &vm.frames[i];

		ObjFunction* function = frame->closure->function;
		uint64_t instruction = frame->ip - function->chunk.code - 1;

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

	printf("[ErrorInfo] ");
	printValue(error);
	printf("\n");

	stack_reset();
	return false;
}

COLD_FUNCTION
static void runtimeError(C_STR format, ...) {
	fprintf(stderr, "[RuntimeError] ");

	va_list args;
	va_start(args, format);
	vfprintf(stderr, format, args);
	va_end(args);
	fputs("\n", stderr);

	for (int32_t i = vm.frameCount - 1; i >= 0; i--) {
		CallFrame* frame = &vm.frames[i];
		ObjFunction* function = frame->closure->function;
		uint64_t instruction = frame->ip - function->chunk.code - 1;

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

	stack_reset();
}

HOT_FUNCTION
void stack_push(Value value)
{
	*vm.stackTop = value;

	if (++vm.stackTop == vm.stackBoundary) {
		ptrdiff_t oldCapacity = vm.stackBoundary - vm.stack;
		uint32_t capacity = GROW_CAPACITY(oldCapacity);

		if (capacity > STACK_MAX_SIZE) {
			runtimeError("Stack overflow.");
			return;
		}

		// remind that we realloc the ptr, so we need to update all ptrRef unless we use index to record
		Value* oldStack = vm.stack;
		vm.stack = GROW_ARRAY_NO_GC(Value, vm.stack, oldCapacity, capacity);
		vm.stackBoundary = vm.stack + capacity;		//need fresh
		vm.stackTop = vm.stack + oldCapacity;		//need fresh

		// since stack grow, we need to update all ptrRef to the stack, otherwise they will point to the old stack and cause errors
		if (oldStack != vm.stack) {
			// update all stack frames so it is safe now
			for (int32_t i = vm.frameCount - 1; i >= 0; i--) {
				CallFrame* frame = &vm.frames[i];
				frame->slots = vm.stack + frame->slotsOffset;
			}

			// update all upvalues if needed
			for (ObjUpvalue* upvalue = vm.openUpvalues;upvalue != NULL;upvalue = upvalue->next) {
				if (upvalue->location_offset != CLOSED_OBJ_UPVALUE_LOCATION) {
					upvalue->location = vm.stack + upvalue->location_offset;
				}
			}
		}
	}
}

HOT_FUNCTION
void stack_replace(Value val) {
	vm.stackTop[-1] = val;
}

HOT_FUNCTION
Value stack_pop()
{
	vm.stackTop--;
	return *vm.stackTop;
}

#define STACK_PEEK(distance) (vm.stackTop[-1 - distance])

COLD_FUNCTION
void defineNative_math_number(C_STR name, double value) {
	tableSet(&vm.builtins[MODULE_MATH].fields,
		copyString(name, (uint32_t)strlen(name), false),
		NUMBER_VAL(value)
	);
}

COLD_FUNCTION
void defineNative_math(C_STR name, NativeFn function) {
	tableSet(&vm.builtins[MODULE_MATH].fields,
		copyString(name, (uint32_t)strlen(name), false),
		OBJ_VAL(newNative(function))
	);
}

COLD_FUNCTION
void defineNative_array(C_STR name, NativeFn function) {
	tableSet(&vm.builtins[MODULE_ARRAY].fields,
		copyString(name, (uint32_t)strlen(name), false),
		OBJ_VAL(newNative(function))
	);
}

COLD_FUNCTION
void defineNative_object(C_STR name, NativeFn function) {
	tableSet(&vm.builtins[MODULE_OBJECT].fields,
		copyString(name, (uint32_t)strlen(name), false),
		OBJ_VAL(newNative(function))
	);
}

COLD_FUNCTION
void defineNative_string(C_STR name, NativeFn function) {
	tableSet(&vm.builtins[MODULE_STRING].fields,
		copyString(name, (uint32_t)strlen(name), false),
		OBJ_VAL(newNative(function))
	);
}

COLD_FUNCTION
void defineNative_time(C_STR name, NativeFn function) {
	tableSet(&vm.builtins[MODULE_TIME].fields,
		copyString(name, (uint32_t)strlen(name), false),
		OBJ_VAL(newNative(function))
	);
}

COLD_FUNCTION
void defineNative_ctor(C_STR name, NativeFn function) {
	tableSet(&vm.builtins[MODULE_CTOR].fields,
		copyString(name, (uint32_t)strlen(name), false),
		OBJ_VAL(newNative(function))
	);
}

COLD_FUNCTION
void defineNative_system(C_STR name, NativeFn function) {
	tableSet(&vm.builtins[MODULE_SYSTEM].fields,
		copyString(name, (uint32_t)strlen(name), false),
		OBJ_VAL(newNative(function))
	);
}

COLD_FUNCTION
void defineNative_global(C_STR name, NativeFn function) {
	//stack_push(OBJ_VAL(copyString(name, (uint32_t)strlen(name), false)));
	//stack_push(OBJ_VAL(newNative(function)));
	//tableSet(&vm.globals.fields, AS_STRING(vm.stack[0]), vm.stack[1]);
	//stack_pop();
	//stack_pop();

	tableSet(&vm.globals.fields,
		copyString(name, (uint32_t)strlen(name), false),
		OBJ_VAL(newNative(function))
	);
}

COLD_FUNCTION
static void importBuiltins() {
	for (uint32_t i = 0; i < BUILTIN_MODULE_COUNT; ++i) {
		vm.builtins[i] = (ObjInstance){
		.obj = stateLess_obj_header(OBJ_INSTANCE),
		.klass = NULL,
		.fields = {.isGlobal = false,.isFrozen = false}//remind this
		};
	}

	//init
	table_init(&vm.builtins[MODULE_MATH].fields);
	table_init(&vm.builtins[MODULE_ARRAY].fields);
	table_init(&vm.builtins[MODULE_OBJECT].fields);
	table_init(&vm.builtins[MODULE_STRING].fields);
	table_init(&vm.builtins[MODULE_TIME].fields);
	table_init(&vm.builtins[MODULE_CTOR].fields);
	table_init(&vm.builtins[MODULE_SYSTEM].fields);

	importNative_math();
	importNative_array();
	importNative_object();
	importNative_string();
	importNative_time();
	importNative_ctor();
	importNative_system();

	for (uint32_t i = 0; i < BUILTIN_MODULE_COUNT; ++i) {
		vm.builtins[i].fields.isFrozen = true;//remind this,we can't set first
	}
}

COLD_FUNCTION
static void removeBuiltins() {
	for (uint32_t i = 0; i < BUILTIN_MODULE_COUNT; ++i) {
		table_free(&vm.builtins[i].fields);
	}
}

COLD_FUNCTION
static void initTypeStrings() {
	for (uint32_t i = 0; i < TYPE_STRING_COUNT; ++i) {
		vm.typeStrings[i] = NULL;
	}

	vm.typeStrings[TYPE_STRING_BOOL] = copyString("boolean", (uint32_t)strlen("boolean"), false);
	vm.typeStrings[TYPE_STRING_NIL] = copyString("nil", (uint32_t)strlen("nil"), false);
	vm.typeStrings[TYPE_STRING_NUMBER] = copyString("number", (uint32_t)strlen("number"), false);
	vm.typeStrings[TYPE_STRING_STRING] = copyString("string", (uint32_t)strlen("string"), false);
	vm.typeStrings[TYPE_STRING_STRING_BUILDER] = copyString("stringBuilder", (uint32_t)strlen("stringBuilder"), false);
	vm.typeStrings[TYPE_STRING_FUNCTION] = copyString("function", (uint32_t)strlen("function"), false);
	vm.typeStrings[TYPE_STRING_NATIVE] = copyString("native", (uint32_t)strlen("native"), false);
	vm.typeStrings[TYPE_STRING_CLASS] = copyString("class", (uint32_t)strlen("class"), false);
	vm.typeStrings[TYPE_STRING_OBJECT] = copyString("object", (uint32_t)strlen("object"), false);
	vm.typeStrings[TYPE_STRING_ARRAY] = copyString("array", (uint32_t)strlen("array"), false);
	vm.typeStrings[TYPE_STRING_ARRAY_F64] = copyString("array-f64", (uint32_t)strlen("array-f64"), false);
	vm.typeStrings[TYPE_STRING_ARRAY_F32] = copyString("array-f32", (uint32_t)strlen("array-f32"), false);
	vm.typeStrings[TYPE_STRING_ARRAY_U32] = copyString("array-u32", (uint32_t)strlen("array-u32"), false);
	vm.typeStrings[TYPE_STRING_ARRAY_I32] = copyString("array-i32", (uint32_t)strlen("array-i32"), false);
	vm.typeStrings[TYPE_STRING_ARRAY_U16] = copyString("array-u16", (uint32_t)strlen("array-u16"), false);
	vm.typeStrings[TYPE_STRING_ARRAY_I16] = copyString("array-i16", (uint32_t)strlen("array-i16"), false);
	vm.typeStrings[TYPE_STRING_ARRAY_U8] = copyString("array-u8", (uint32_t)strlen("array-u8"), false);
	vm.typeStrings[TYPE_STRING_ARRAY_I8] = copyString("array-i8", (uint32_t)strlen("array-i8"), false);
}

COLD_FUNCTION
void vm_init()
{
	vm.stack = NULL;
	vm.stackTop = NULL;
	vm.stackBoundary = NULL;

	//init global
	valueArray_init(&vm.constants);
	valueHoles_init(&vm.constantHoles);

	vm.stack = ALLOCATE_NO_GC(Value, STACK_INITIAL_SIZE);
	vm.stackBoundary = vm.stack + STACK_INITIAL_SIZE;

	stack_reset();

	// init global 
	vm.globals = (ObjInstance){
		.obj = stateLess_obj_header(OBJ_INSTANCE),
		.klass = NULL,
		.fields = {.isGlobal = true,.isFrozen = false}//remind this
	};

	table_init(&vm.globals.fields);

	stringTable_init(&vm.scripts);
	stringTable_init(&vm.strings);

	vm.objects = NULL;
	vm.objects_no_gc = NULL;

	//init gray stack
	vm.grayCount = 0;
	vm.grayCapacity = 0;
	vm.grayStack = NULL;

	vm.functionID = 0;
	//set
	vm.bytesAllocated = 0;
	vm.bytesAllocated_no_gc = 0;
	vm.nextGC = GC_HEAP_BEGIN;
	vm.beginGC = GC_HEAP_BEGIN;
	vm.gcMark = true; //bool value
	vm.gcWorking = false; //bool value

	//import the builtins
	importBuiltins();

	//import native funcs
	importNative_global();

	vm.initString = NULL;
	vm.initString = copyString("init", strlen("init"), false);
	initTypeStrings();

	//this is for literal object
	vm.emptyClass = (ObjClass){
		.obj = stateLess_obj_header(OBJ_CLASS),
		.name = copyString("<object>", strlen("<object>"), false),
		.initializer = NIL_VAL
	};
	table_init(&vm.emptyClass.methods);
}

COLD_FUNCTION
void vm_register_ic_function(ObjFunction* function) {
	if (vm.icFuncCapacity < vm.icFuncCount + 1) {
		uint32_t capacity = GROW_CAPACITY(vm.icFuncCapacity);
		vm.icFuncs = GROW_ARRAY_NO_GC(ObjFunction*, vm.icFuncs, vm.icFuncCapacity, capacity);
		vm.icFuncCapacity = capacity;
	}
	vm.icFuncs[vm.icFuncCount++] = function;
}

void vm_free()
{
	//release the immortal functions' cache arrays (sized cacheCount+1 after shrink-to-fit)
	for (uint32_t i = 0; i < vm.icFuncCount; i++) {
		ObjFunction* fn = vm.icFuncs[i];
		FREE_ARRAY_NO_GC(InlineCacheSlot, fn->caches, (uint32_t)fn->cacheCount + 1);
		fn->caches = NULL;
	}

	FREE_ARRAY_NO_GC(ObjFunction*, vm.icFuncs, vm.icFuncCapacity);
	vm.icFuncs = NULL;
	vm.icFuncCount = 0;
	vm.icFuncCapacity = 0;

	valueArray_free(&vm.constants);
	valueHoles_free(&vm.constantHoles);

	table_free(&vm.globals.fields);
	stringTable_free(&vm.scripts);
	stringTable_free(&vm.strings);

	vm.initString = NULL;
	for (uint32_t i = 0; i < TYPE_STRING_COUNT; ++i) {
		vm.typeStrings[i] = NULL;
	}
	freeObjects();

	//realease the stack
	ptrdiff_t capacity = vm.stackBoundary - vm.stack;
	FREE_ARRAY_NO_GC(Value, vm.stack, capacity);
	vm.stack = NULL;
	vm.stackTop = NULL;
	vm.stackBoundary = NULL;

	vm.initString = NULL;
	removeBuiltins();

	table_free(&vm.emptyClass.methods);
}

uint32_t getConstantSize()
{
	return vm.constants.count;
}

//return the constant index
uint32_t addConstant(Value value)
{
	uint32_t index = valueHoles_get(&vm.constantHoles);
	if (index == VALUEHOLES_EMPTY) {
		stack_push(value);//prevent GC errors
		valueArray_write(&vm.constants, value);
		stack_pop();
		return vm.constants.count - 1;
	}
	else {
		valueArray_writeAt(&vm.constants, value, index);
		valueHoles_pop(&vm.constantHoles);
		return index;
	}
}

static void getTypeof() {
	Value val = vm.stackTop[-1];

	if (IS_NUMBER(val)) {
		stack_replace(OBJ_VAL(vm.typeStrings[TYPE_STRING_NUMBER]));
		return;
	}
	else if (IS_BOOL(val)) {
		stack_replace(OBJ_VAL(vm.typeStrings[TYPE_STRING_BOOL]));
		return;
	}
	else if (IS_NIL(val)) {
		stack_replace(OBJ_VAL(vm.typeStrings[TYPE_STRING_NIL]));
		return;
	}
	else {
		switch (OBJ_TYPE(val)) {
		case OBJ_STRING:
			stack_replace(OBJ_VAL(vm.typeStrings[TYPE_STRING_STRING]));
			return;
		case OBJ_STRING_BUILDER:
			stack_replace(OBJ_VAL(vm.typeStrings[TYPE_STRING_STRING_BUILDER]));
			return;
		case OBJ_CLOSURE:
		case OBJ_BOUND_METHOD:
			stack_replace(OBJ_VAL(vm.typeStrings[TYPE_STRING_FUNCTION]));
			return;
		case OBJ_NATIVE:
			stack_replace(OBJ_VAL(vm.typeStrings[TYPE_STRING_NATIVE]));
			return;
		case OBJ_CLASS:
			stack_replace(OBJ_VAL(vm.typeStrings[TYPE_STRING_CLASS]));
			return;
		case OBJ_INSTANCE:
			stack_replace(OBJ_VAL(vm.typeStrings[TYPE_STRING_OBJECT]));
			return;
		case OBJ_ARRAY:
			stack_replace(OBJ_VAL(vm.typeStrings[TYPE_STRING_ARRAY]));
			return;
		case OBJ_ARRAY_F64:
			stack_replace(OBJ_VAL(vm.typeStrings[TYPE_STRING_ARRAY_F64]));
			return;
		case OBJ_ARRAY_F32:
			stack_replace(OBJ_VAL(vm.typeStrings[TYPE_STRING_ARRAY_F32]));
			return;
		case OBJ_ARRAY_U32:
			stack_replace(OBJ_VAL(vm.typeStrings[TYPE_STRING_ARRAY_U32]));
			return;
		case OBJ_ARRAY_I32:
			stack_replace(OBJ_VAL(vm.typeStrings[TYPE_STRING_ARRAY_I32]));
			return;
		case OBJ_ARRAY_U16:
			stack_replace(OBJ_VAL(vm.typeStrings[TYPE_STRING_ARRAY_U16]));
			return;
		case OBJ_ARRAY_I16:
			stack_replace(OBJ_VAL(vm.typeStrings[TYPE_STRING_ARRAY_I16]));
			return;
		case OBJ_ARRAY_U8:
			stack_replace(OBJ_VAL(vm.typeStrings[TYPE_STRING_ARRAY_U8]));
			return;
		case OBJ_ARRAY_I8:
			stack_replace(OBJ_VAL(vm.typeStrings[TYPE_STRING_ARRAY_I8]));
			return;
		}
	}

	stack_replace(NIL_VAL);
}

HOT_FUNCTION
static bool call(ObjClosure* closure, int argCount) {
	//if (argCount > closure->function->arity) {
	//	runtimeError("Expected %d arguments but got %d.",
	//		closure->function->arity, argCount);
	//	return false;
	//}

	if (vm.frameCount == FRAMES_MAX) {
		runtimeError("Stack overflow.");
		return false;
	}

	// append missing args
	while (argCount < closure->function->arity) {
		stack_push(NIL_VAL);
		++argCount;
	}

	CallFrame* frame = &vm.frames[vm.frameCount++];// add after call
	frame->closure = closure;
	frame->ip = closure->function->chunk.code;
	//-1 is for function itself
	frame->slots = vm.stackTop - argCount - 1;
	frame->slotsOffset = (ptrdiff_t)(frame->slots - vm.stack);
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
static bool call_native(NativeFn native, int argCount) {
	Value* stackTop = vm.stackTop - argCount;//store top, we don't know if native push stack (avoiding gc)
	Value result = native(argCount, stackTop);
	vm.stackTop = stackTop;//restore the top
	stack_replace(result);
	return true;
}

static inline void defineMethod(ObjString* name) {
	Value method = vm.stackTop[-1];
	ObjClass* klass = AS_CLASS(vm.stackTop[-2]);

	tableSet(&klass->methods, name, method);
	if (name == vm.initString) {//inline cache
		klass->initializer = method;
	}
	vm.stackTop--;
}

HOT_FUNCTION
static bool callValue(Value callee, int argCount) {
	if (IS_OBJ(callee)) {
		switch (OBJ_TYPE(callee)) {
		case OBJ_CLOSURE: return call(AS_CLOSURE(callee), argCount);
		case OBJ_NATIVE: return call_native(AS_NATIVE(callee), argCount);
		case OBJ_BOUND_METHOD: {
			ObjBoundMethod* bound = AS_BOUND_METHOD(callee);
			STACK_PEEK(argCount) = bound->receiver;//bind 'this' to logic slot[0]
			return call(bound->method, argCount);
		}
		case OBJ_CLASS: {
			ObjClass* klass = AS_CLASS(callee);
			STACK_PEEK(argCount) = OBJ_VAL(newInstance(klass));

			//call init with fast path
			if (NOT_NIL(klass->initializer)) {
				return call(AS_CLOSURE(klass->initializer), argCount);
			}
			else if (argCount != 0) {
				runtimeError("Expected 0 arguments for initializer but got %d.", argCount);
				return false;
			}
			return true;
		}
		}
	}

	runtimeError("Can only call functions and classes.");
	return false;
}

HOT_FUNCTION
static inline bool invokeFromClass(ObjClass* klass, ObjString* name, int argCount) {
	Value method;
	if ((klass == NULL) || !tableGet(&klass->methods, name, &method)) {
		runtimeError("Undefined property '%s'.", name->chars);
		return false;
	}
	return call(AS_CLOSURE(method), argCount);
}

//HOT_FUNCTION
//static inline bool invoke(ObjString* name, int argCount) {
//	Value receiver = STACK_PEEK(argCount);
//	if (!IS_INSTANCE(receiver)) {
//		runtimeError("Only instances have methods.");
//		return false;
//	}
//	ObjInstance* instance = AS_INSTANCE(receiver);
//
//	Value value;
//	if (tableGet(&instance->fields, name, &value)) {
//		STACK_PEEK(argCount) = value;
//		return callValue(value, argCount);
//	}
//
//	return invokeFromClass(instance->klass, name, argCount);
//}

HOT_FUNCTION
static void bindMethod(ObjClass* klass, ObjString* name) {
	Value method;
	if (tableGet(&klass->methods, name, &method)) {
		ObjBoundMethod* bound = newBoundMethod(vm.stackTop[-1], AS_CLOSURE(method));
		stack_replace(OBJ_VAL(bound));
	}
	else {
		//don't throw
		//runtimeError("Undefined property '%s'.", name->chars);
		stack_replace(NIL_VAL);
	}
}

HOT_FUNCTION
static ObjUpvalue* captureUpvalue(Value* local) {
	ObjUpvalue* prevUpvalue = NULL;
	ObjUpvalue* upvalue = vm.openUpvalues;

	//compare stack ptr(search from deepest,so [next] is upper)
	while (upvalue != NULL && upvalue->location > local) {
		prevUpvalue = upvalue;
		upvalue = upvalue->next;
	}

	if (upvalue != NULL && upvalue->location == local) {
		return upvalue;
	}

	ObjUpvalue* createdUpvalue = newUpvalue(local, (ptrdiff_t)(local - vm.stack));

	//insert it
	createdUpvalue->next = upvalue;
	if (prevUpvalue == NULL) {
		vm.openUpvalues = createdUpvalue;
	}
	else {
		prevUpvalue->next = createdUpvalue;
	}

	return createdUpvalue;
}

HOT_FUNCTION
static void closeUpvalues(Value* last) {
	while (vm.openUpvalues != NULL && vm.openUpvalues->location >= last) {
		ObjUpvalue* upvalue = vm.openUpvalues;

		//if one upValue closed,it's location is it's closed's pointer
		upvalue->closed = *upvalue->location;
		upvalue->location = &upvalue->closed;
		upvalue->location_offset = CLOSED_OBJ_UPVALUE_LOCATION;
		vm.openUpvalues = upvalue->next;
	}
}

HOT_FUNCTION
static inline bool isFalsey(Value value) {
	return IS_NIL(value) || (IS_BOOL(value) && !AS_BOOL(value));
}

HOT_FUNCTION
static inline bool isTruthy(Value value) {
	return !IS_NIL(value) && (!IS_BOOL(value) || AS_BOOL(value));
}

HOT_FUNCTION
//returns the advanced ip on success,NULL on failure (no operand bytes are
//consumed on failure paths, so no rollback is needed)
static uint8_t* bitInstruction(uint8_t bitOpType, uint8_t* ip) {
#define READ_BYTE() (*(ip)++)
#define READ_WORD() (ip += 4, (uint32_t)((ip[-4]) | ((uint32_t)ip[-3] << 8) | ((uint32_t)ip[-2] << 16) | ((uint32_t)ip[-1] << 24)))

#define BINARAY_OP_BIT(op)																			\
    do {																							\
		/* Pop the top two values from the stack */													\
		if (IS_NUMBER(vm.stackTop[-2]) && IS_NUMBER(vm.stackTop[-1])) {								\
			/* Perform the operation and push the result back */									\
			vm.stackTop[-2] = NUMBER_VAL((int32_t)AS_NUMBER(vm.stackTop[-2]) op (int32_t)AS_NUMBER(vm.stackTop[-1]));	\
			vm.stackTop--;																			\
			return ip;																				\
		}																							\
	} while (false)

#define BINARAY_OP_BIT_IMM(op)																\
    do {																					\
		/* Pop the top two values from the stack */											\
		if (IS_NUMBER(vm.stackTop[-1])) {													\
			int32_t constant = READ_WORD();													\
			/* Perform the operation and push the result back */							\
			vm.stackTop[-1] = NUMBER_VAL((int32_t)AS_NUMBER(vm.stackTop[-1]) op constant);	\
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
		if (IS_NUMBER(vm.stackTop[-1])) {
			vm.stackTop[-1] = NUMBER_VAL(~(int32_t)AS_NUMBER(vm.stackTop[-1]));
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
		if (IS_NUMBER(vm.stackTop[-2]) && IS_NUMBER(vm.stackTop[-1])) {
			int32_t shiftBits = AS_NUMBER(vm.stackTop[-1]);

			if (shiftBits >= 0) {
				vm.stackTop[-2] = NUMBER_VAL((int32_t)AS_NUMBER(vm.stackTop[-2]) << (shiftBits & 31));
				vm.stackTop--;
			}
			else {
				vm.stackTop[-2] = NUMBER_VAL(0);
				vm.stackTop--;
			}
			return ip;
		}
		break;
	}
	case BIT_OP_SAR: {
	label_bit_sar:
		if (IS_NUMBER(vm.stackTop[-2]) && IS_NUMBER(vm.stackTop[-1])) {
			int32_t shiftBits = AS_NUMBER(vm.stackTop[-1]);

			if (shiftBits >= 0) {
				vm.stackTop[-2] = NUMBER_VAL((int32_t)AS_NUMBER(vm.stackTop[-2]) >> (shiftBits & 31));
				vm.stackTop--;
			}
			else {
				vm.stackTop[-2] = NUMBER_VAL(0);
				vm.stackTop--;
			}
			return ip;
		}
		break;
	}
	case BIT_OP_SHR: {
	label_bit_shr:
		if (IS_NUMBER(vm.stackTop[-2]) && IS_NUMBER(vm.stackTop[-1])) {
			int32_t shiftBits = AS_NUMBER(vm.stackTop[-1]);

			if (shiftBits >= 0) {
				vm.stackTop[-2] = NUMBER_VAL((uint32_t)AS_NUMBER(vm.stackTop[-2]) >> (shiftBits & 31));
				vm.stackTop--;
			}
			else {
				vm.stackTop[-2] = NUMBER_VAL(0);
				vm.stackTop--;
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
		if (IS_NUMBER(vm.stackTop[-1])) {
			int32_t shiftBits = READ_BYTE();

			if (shiftBits >= 0) {
				vm.stackTop[-1] = NUMBER_VAL((int32_t)AS_NUMBER(vm.stackTop[-1]) << shiftBits);
			}
			else {
				vm.stackTop[-1] = NUMBER_VAL(0);
			}
			return ip;
		}
		break;
	}
	case BIT_OP_SARI: {
	label_bit_sari:
		if (IS_NUMBER(vm.stackTop[-1])) {
			int32_t shiftBits = READ_BYTE();

			if (shiftBits >= 0) {
				vm.stackTop[-1] = NUMBER_VAL((int32_t)AS_NUMBER(vm.stackTop[-1]) >> shiftBits);
			}
			else {
				vm.stackTop[-1] = NUMBER_VAL(0);
			}
			return ip;
		}
		break;
	}
	case BIT_OP_SHRI: {
	label_bit_shri:
		if (IS_NUMBER(vm.stackTop[-1])) {
			int32_t shiftBits = READ_BYTE();

			if (shiftBits >= 0) {
				vm.stackTop[-1] = NUMBER_VAL((uint32_t)AS_NUMBER(vm.stackTop[-1]) >> shiftBits);
			}
			else {
				vm.stackTop[-1] = NUMBER_VAL(0);
			}
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

//IC slow paths: extracted from the dispatch handlers to keep run()'s code small
//(cold + noinline so a single call site cannot be inlined back into the hot dispatch loop)

COLD_FUNCTION
NOINLINE_FUNCTION
static void icGetPropertySlow(ObjInstance* instance, ObjString* name, uint8_t slot, InlineCacheSlot* icCache) {
	Value value;
	Entry* entry;
	if (tableGetEntry(&instance->fields, name, &value, &entry)) {
		//backfill cache
		if (slot != 0 && instance->fields.capacity <= UINT16_MAX) {
			icCache[slot].capacity = instance->fields.capacity;
			icCache[slot].index = (entry - instance->fields.entries);
		}
		stack_replace(value);
		return;
	}
	//don't throw error
	if (instance->klass != NULL) {
		bindMethod(instance->klass, name);
	}
	else {
		//klass-less instances (builtin modules) have no method fallback:undefined property reads nil
		//(without this the instance itself would leak out as the read result)
		stack_replace(NIL_VAL);
	}
}

COLD_FUNCTION
NOINLINE_FUNCTION
static void icSetPropertySlow(ObjInstance* instance, ObjString* name, Value newValue, uint8_t slot, InlineCacheSlot* icCache) {
	if (NOT_NIL(newValue)) {
		Value oldValue;
		Entry* entry;
		if (tableGetEntry(&instance->fields, name, &oldValue, &entry)) {
			//existing field: single-probe direct write + backfill
			entry->value = newValue;
			if (slot != 0 && instance->fields.capacity <= UINT16_MAX) {
				icCache[slot].capacity = instance->fields.capacity;
				icCache[slot].index = (entry - instance->fields.entries);
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
			if (instance->klass == NULL) {
				//nothing to shadow
			}
			else if (slot != 0 && (ObjClass*)icCache[slot].extraA == instance->klass) {
				//memo hit:skip the methods table probe
				if (icCache[slot].extraB != NULL) {
					INSTANCE_POISON(instance) = 1;
				}
			}
			else {
				Value methodValue;
				if (tableGet(&instance->klass->methods, name, &methodValue)) {
					INSTANCE_POISON(instance) = 1;
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
	else {
		tableDelete(&instance->fields, name);
	}
}

//returns true if a callee was entered,false if a runtime error was reported
COLD_FUNCTION
NOINLINE_FUNCTION
static bool icInvokeSlow(ObjInstance* instance, ObjString* method, uint8_t argCount, uint8_t slot, InlineCacheSlot* icCache) {
	//slow path: field hit shadows method → poison + callValue
	Value value;
	Entry* entry;
	if (tableGetEntry(&instance->fields, method, &value, &entry)) {
		INSTANCE_POISON(instance) = 1;
		STACK_PEEK(argCount) = value;
		return callValue(value, argCount);
	}

	//method lookup (klass may be NULL → undefined)
	if (instance->klass == NULL) {
		runtimeError("Undefined property '%s'.", method->chars);
		return false;
	}
	Value methodValue;
	if (!tableGetEntry(&instance->klass->methods, method, &methodValue, &entry)) {
		runtimeError("Undefined property '%s'.", method->chars);
		return false;
	}
	//backfill cache
	if (slot != 0) {
		icCache[slot].extraA = instance->klass;
		icCache[slot].extraB = AS_CLOSURE(methodValue);
	}
	return call(AS_CLOSURE(methodValue), argCount);
}

//to run code in vm
HOT_FUNCTION
static InterpretResult run()
{
	CallFrame* frame = &vm.frames[vm.frameCount - 1];
	register uint8_t* ip = frame->ip;
	//cached constants base of the current function,refreshed at every frame switch
	Value* constants = frame->closure->function->constants.values;

#define UPDATE_FRAME()	\
	do {				\
		frame = &vm.frames[vm.frameCount - 1];\
		ip = frame->ip; \
		constants = frame->closure->function->constants.values;	\
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

		[OP_GET_PROPERTY] = && label_op_get_property,
		[OP_SET_PROPERTY] = && label_op_set_property,
		[OP_SET_PROPERTY_POP] = && label_op_set_property_pop,
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

		[OP_INSTANCE_OF] = && label_op_instance_of,
		[OP_TYPE_OF] = && label_op_type_of,
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
	};
#endif

#define READ_BYTE() (*(ip++))
#define READ_SHORT() (ip += 2, (uint16_t)(ip[-2] | (ip[-1] << 8)))
#define READ_24bits() (ip += 3, (uint32_t)(ip[-3] | (ip[-2] << 8) | (ip[-1] << 16)))
#define READ_CONSTANT(index) (vm.constants.values[(index)])
	//constants in function's own table (number),16bits index
	//cached as a run() local next to frame/ip,refreshed at every frame switch
#define READ_LOCAL_CONSTANT(index) (constants[(index)])

	// push(pop() op pop())
#define BINARY_OP(valueType,op)																		\
    do {																							\
		/* Pop the top two values from the stack */													\
		if (IS_NUMBER(vm.stackTop[-2]) && IS_NUMBER(vm.stackTop[-1])) {								\
			/* Perform the operation and push the result back */									\
			vm.stackTop[-2] = valueType(AS_NUMBER(vm.stackTop[-2]) op AS_NUMBER(vm.stackTop[-1]));	\
			vm.stackTop--;																			\
		} else {														                            \
			RUNTIME_ERROR("Operands must be numbers.");										\
		}																							\
	} while (false)

#define BINARY_OP_WITH_RIGHT(valueType,right,op)											\
    do {																					\
		/* Pop the top two values from the stack */											\
		if (IS_NUMBER(vm.stackTop[-1]) && IS_NUMBER(right)) {								\
			/* Perform the operation and push the result back */							\
			vm.stackTop[-1] = valueType(AS_NUMBER(vm.stackTop[-1]) op AS_NUMBER(right));	\
		} else {																			\
			RUNTIME_ERROR("Operands must be numbers.");										\
		}																					\
	} while (false)

	//ordering comparison (< <= > >=): numbers or string/stringBuilder (memcmp order)
#define COMPARE_OP(op)																		\
	do {																					\
		if (IS_NUMBER(vm.stackTop[-2]) && IS_NUMBER(vm.stackTop[-1])) {						\
			vm.stackTop[-2] = BOOL_VAL(AS_NUMBER(vm.stackTop[-2]) op AS_NUMBER(vm.stackTop[-1]));\
			vm.stackTop--;																	\
		}																					\
		else if (isStringLike(vm.stackTop[-2]) && isStringLike(vm.stackTop[-1])) {			\
			vm.stackTop[-2] = BOOL_VAL(compareStringLike(vm.stackTop[-2], vm.stackTop[-1]) op 0);\
			vm.stackTop--;																	\
		}																					\
		else {																				\
			RUNTIME_ERROR("Operands must be two numbers or two strings.");					\
		}																					\
	} while (false)

	//ordering comparison with a non-stack right operand (local or number constant)
#define COMPARE_OP_WITH_RIGHT(right,op)													\
	do {																					\
		if (IS_NUMBER(vm.stackTop[-1]) && IS_NUMBER(right)) {								\
			vm.stackTop[-1] = BOOL_VAL(AS_NUMBER(vm.stackTop[-1]) op AS_NUMBER(right));		\
		}																					\
		else if (isStringLike(vm.stackTop[-1]) && isStringLike(right)) {					\
			vm.stackTop[-1] = BOOL_VAL(compareStringLike(vm.stackTop[-1], right) op 0);		\
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

//error inside run(): sync the top frame's ip for the stack trace, then raise.
//error paths are cold, so this store costs nothing and keeps `ip` free of any
//stack escape (register-resident across the whole dispatch loop)
#define RUNTIME_ERROR(...)						\
	do {										\
		frame->ip = ip;							\
		runtimeError(__VA_ARGS__);				\
		return INTERPRET_RUNTIME_ERROR;			\
	} while (false)

	while (true) //let it loop
	{
#if DEBUG_TRACE_EXECUTION //print in debug mode
		printf("          ");
		for (Value* slot = vm.stack; slot < vm.stackTop; slot++) {
			printf("[ ");
			printValue(*slot);
			printf(" ]");
		}
		printf("\n");

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
			Value constant = READ_CONSTANT(READ_24bits());
			stack_push(constant);
			NEXT_INSTRUCTION;
		}
		case OP_CONST_NUMBER: {
		label_op_const_number:
			Value constant = READ_LOCAL_CONSTANT(READ_SHORT());
			stack_push(constant);
			NEXT_INSTRUCTION;
		}
		case OP_CLOSURE: {
		label_op_closure:
			Value constant = READ_CONSTANT(READ_24bits());
			ObjFunction* function = AS_FUNCTION(constant);
			ObjClosure* closure = newClosure(function);
			stack_push(OBJ_VAL(closure));

			for (uint32_t i = 0; i < closure->upvalueCount; i++) {
				uint8_t isLocal = READ_BYTE();
				uint16_t index = READ_SHORT();
				if (isLocal) {
					closure->upvalues[i] = captureUpvalue(frame->slots + index);
				}
				else {
					closure->upvalues[i] = frame->closure->upvalues[index];
				}
			}
			NEXT_INSTRUCTION;
		}
		case OP_CLASS: {
		label_op_class:
			Value constant = READ_CONSTANT(READ_24bits());
			ObjString* name = AS_STRING(constant);
			stack_push(OBJ_VAL(newClass(name)));
			NEXT_INSTRUCTION;
		}
		case OP_METHOD: {
		label_op_method:
			Value constant = READ_CONSTANT(READ_24bits());
			ObjString* name = AS_STRING(constant);
			defineMethod(name);
			NEXT_INSTRUCTION;
		}
		case OP_INHERIT: {
		label_op_inherit:
			Value superclass = vm.stackTop[-2];
			if (IS_CLASS(superclass)) {
				ObjClass* subclass = AS_CLASS(vm.stackTop[-1]);
				tableAddAll(&AS_CLASS(superclass)->methods, &subclass->methods);
				//inherited constructor:keep the initializer fast path in sync with the copied methods table
				//(a subclass init declared after this would overwrite it via defineMethod)
				if (IS_NIL(subclass->initializer)) {
					subclass->initializer = AS_CLASS(superclass)->initializer;
				}
				stack_pop(); // Subclass.
			}
			else {
				RUNTIME_ERROR("Superclass must be a class.");
			}
			NEXT_INSTRUCTION;
		}
		case OP_GET_SUPER: {
		label_op_get_super:
			Value constant = READ_CONSTANT(READ_24bits());
			ObjString* name = AS_STRING(constant);
			ObjClass* superclass = AS_CLASS(stack_pop());
			bindMethod(superclass, name);
			NEXT_INSTRUCTION;
		}
		case OP_GET_PROPERTY: {
		label_op_get_property:
			if (!IS_INSTANCE(vm.stackTop[-1])) {
				RUNTIME_ERROR("Only instances have properties.");
			}

			ObjInstance* instance = AS_INSTANCE(vm.stackTop[-1]);
			Value constant = READ_CONSTANT(READ_24bits());
			ObjString* name = AS_STRING(constant);
			uint8_t slot = READ_BYTE();
			//icCache derived per handler,not a run()-wide local,to keep the dispatch loop's live ranges small
			InlineCacheSlot* icCache = frame->caches;

			//inline cache: validate [capacity, entry index] against the live table
			//slot 0 = sentinel slot {0,0}: real table capacity is never 0 → miss; idx bound check guards capacity==0 tables
			{
				Table* fields = &instance->fields;
				if ((uint32_t)icCache[slot].capacity == fields->capacity) {
					uint32_t idx = icCache[slot].index;
					if (idx < fields->capacity && fields->entries[idx].key == name) {
						stack_replace(fields->entries[idx].value);
						NEXT_INSTRUCTION;
					}
				}
			}

			icGetPropertySlow(instance, name, slot, icCache);
			NEXT_INSTRUCTION;
		}
		case OP_SET_PROPERTY: {
		label_op_set_property:
			if (!IS_INSTANCE(vm.stackTop[-2])) {
				RUNTIME_ERROR("Only instances have fields.");
			}

			ObjInstance* instance = AS_INSTANCE(vm.stackTop[-2]);
			Value constant = READ_CONSTANT(READ_24bits());
			ObjString* name = AS_STRING(constant);
			uint8_t slot = READ_BYTE();
			InlineCacheSlot* icCache = frame->caches;

			//inline cache: direct write only for existing entries with non-nil value (nil means delete)
			if (NOT_NIL(vm.stackTop[-1])) {
				Table* fields = &instance->fields;
				if ((uint32_t)icCache[slot].capacity == fields->capacity) {
					uint32_t idx = icCache[slot].index;
					if (idx < fields->capacity && fields->entries[idx].key == name) {
						fields->entries[idx].value = vm.stackTop[-1];
						Value value = stack_pop();
						stack_replace(value);
						NEXT_INSTRUCTION;
					}
				}
			}

			icSetPropertySlow(instance, name, vm.stackTop[-1], slot, icCache);
			Value value = stack_pop();
			stack_replace(value);
			NEXT_INSTRUCTION;
		}
		case OP_SET_PROPERTY_POP: {
		label_op_set_property_pop:
			if (!IS_INSTANCE(vm.stackTop[-2])) {
				RUNTIME_ERROR("Only instances have fields.");
			}

			ObjInstance* instance = AS_INSTANCE(vm.stackTop[-2]);
			Value constant = READ_CONSTANT(READ_24bits());
			ObjString* name = AS_STRING(constant);
			uint8_t slot = READ_BYTE();
			InlineCacheSlot* icCache = frame->caches;

			//inline cache: direct write only for existing entries with non-nil value (nil means delete)
			if (NOT_NIL(vm.stackTop[-1])) {
				Table* fields = &instance->fields;
				if ((uint32_t)icCache[slot].capacity == fields->capacity) {
					uint32_t idx = icCache[slot].index;
					if (idx < fields->capacity && fields->entries[idx].key == name) {
						fields->entries[idx].value = vm.stackTop[-1];
						//pop value and instance
						vm.stackTop -= 2;
						NEXT_INSTRUCTION;
					}
				}
			}

			icSetPropertySlow(instance, name, vm.stackTop[-1], slot, icCache);
			//pop value and instance
			vm.stackTop -= 2;
			NEXT_INSTRUCTION;
		}
		case OP_GET_INDEX: {
		label_op_get_index:
			Value target = vm.stackTop[-1];
			Value constant = READ_LOCAL_CONSTANT(READ_SHORT());
			double num_index = AS_NUMBER(constant);

			if (isIndexableArray(target)) {
				//get array
				ObjArray* array = AS_ARRAY(target);

				if (ARRAY_IN_RANGE(array, num_index)) {
					if (OBJ_IS_TYPE(array, OBJ_ARRAY)) {
						stack_replace(ARRAY_ELEMENT(array, Value, (uint32_t)num_index));
					}
					else {
						stack_replace(getTypedArrayElement(array, (uint32_t)num_index));
					}
				}
				else {
					stack_replace(NIL_VAL);
				}
				NEXT_INSTRUCTION;
			}
			else if (IS_STRING(target)) {
				//get string
				ObjString* string = AS_STRING(target);

				if (ARRAY_IN_RANGE(string, num_index)) {//return ascii
					stack_replace(NUMBER_VAL((uint8_t)(string->chars[(uint32_t)num_index])));
				}
				else {
					stack_replace(NIL_VAL);
				}
				NEXT_INSTRUCTION;
			}

			RUNTIME_ERROR("Only arrayLike,stringBuilder and string can get number subscript.");
		}
		case OP_SET_INDEX: {
		label_op_set_index:
			Value target = vm.stackTop[-2];
			Value value = vm.stackTop[-1];

			Value constant = READ_LOCAL_CONSTANT(READ_SHORT());
			double num_index = AS_NUMBER(constant);

			if (isArrayLike(target)) {
				//get array
				ObjArray* array = AS_ARRAY(target);

				if (ARRAY_IN_RANGE(array, num_index)) {
					if (OBJ_IS_TYPE(array, OBJ_ARRAY)) {
						vm.stackTop[-2] = ARRAY_ELEMENT(array, Value, (uint32_t)num_index) = value;
					}
					else {
						setTypedArrayElement(array, (uint32_t)num_index, value);
						vm.stackTop[-2] = value;
					}

					vm.stackTop -= 1;
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
			Value target = vm.stackTop[-2];
			Value index = vm.stackTop[-1];

			if (isIndexableArray(target)) {
				if (IS_NUMBER(index)) {
					//get array
					ObjArray* array = AS_ARRAY(target);
					double num_index = AS_NUMBER(index);

					vm.stackTop--;//it is number,so pop is allowed
					if (ARRAY_IN_RANGE(array, num_index)) {
						if (OBJ_IS_TYPE(array, OBJ_ARRAY)) {
							stack_replace(ARRAY_ELEMENT(array, Value, (uint32_t)num_index));
						}
						else {
							stack_replace(getTypedArrayElement(array, (uint32_t)num_index));
						}
					}
					else {
						stack_replace(NIL_VAL);
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
					Value value;

					vm.stackTop--;//it is string,we don't gc string so pop is allowed
					if (tableGet(&instance->fields, name, &value)) {
						stack_replace(value);
						NEXT_INSTRUCTION;
					}
					//don't throw error
					if (instance->klass != NULL) {
						bindMethod(instance->klass, name);
					}
					else {
						//klass-less instances (builtin modules):undefined property reads nil
						stack_replace(NIL_VAL);
					}
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

					vm.stackTop--;//it is number,so pop is allowed
					if (ARRAY_IN_RANGE(string, num_index)) {//return ascii
						stack_replace(NUMBER_VAL((uint8_t)(string->chars[(uint32_t)num_index])));
					}
					else {
						stack_replace(NIL_VAL);
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
			Value target = vm.stackTop[-3];
			Value index = vm.stackTop[-2];
			Value value = vm.stackTop[-1];

			if (isArrayLike(target)) {
				if (IS_NUMBER(index)) {
					//get array
					ObjArray* array = AS_ARRAY(target);
					double num_index = AS_NUMBER(index);

					if (ARRAY_IN_RANGE(array, num_index)) {
						if (OBJ_IS_TYPE(array, OBJ_ARRAY)) {
							vm.stackTop[-3] = ARRAY_ELEMENT(array, Value, (uint32_t)num_index) = value;
						}
						else {
							setTypedArrayElement(array, (uint32_t)num_index, value);
							vm.stackTop[-3] = value;
						}

						vm.stackTop -= 2;
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

					if (NOT_NIL(value)) {
						tableSet(&instance->fields, name, value);
					}
					else {
						tableDelete(&instance->fields, name);
					}

					vm.stackTop[-3] = value;
					vm.stackTop -= 2;
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
			Value constant = READ_CONSTANT(READ_24bits());
			ObjString* name = AS_STRING(constant);

			//it's not a new key,no cache
			tableSet(&vm.globals.fields, name, vm.stackTop[-1]);
			vm.stackTop--;//can not dec first,because gc will kill it
			NEXT_INSTRUCTION;
		}
		case OP_GET_GLOBAL: {
		label_op_get_global:
			Value constant = READ_CONSTANT(READ_24bits());
			ObjString* name = AS_STRING(constant);

			//inline find by cache symbol
			if ((name->symbol != INVALID_OBJ_STRING_SYMBOL)) {
				Entry* entry = &vm.globals.fields.entries[name->symbol];

				if (entry->key == name) {
					// We found the key.
					stack_push(entry->value);
					NEXT_INSTRUCTION;
				}
			}

			Value value;
			if (!tableGet(&vm.globals.fields, name, &value)) {
				RUNTIME_ERROR("Undefined variable '%s'.", name->chars);
			}
			stack_push(value);
			NEXT_INSTRUCTION;
		}
		case OP_SET_GLOBAL: {
		label_op_set_global:
			Value constant = READ_CONSTANT(READ_24bits());
			ObjString* name = AS_STRING(constant);

			//inline find by cache symbol(if we found it,it's not new key)
			if ((name->symbol != INVALID_OBJ_STRING_SYMBOL)) {
				Entry* entry = &vm.globals.fields.entries[name->symbol];
				if (entry->key == name) {
					// We found the key.
					entry->value = vm.stackTop[-1];
					NEXT_INSTRUCTION;
				}
			}

			if (tableSet(&vm.globals.fields, name, vm.stackTop[-1])) {
				//lox dont allow setting undefined one
				tableDelete(&vm.globals.fields, name);
				RUNTIME_ERROR("Undefined variable '%s'.", name->chars);
			}
			NEXT_INSTRUCTION;
		}
		case OP_NEW_ARRAY: {
		label_op_new_array:
			uint16_t size = READ_SHORT();
			ObjArray* array = newArray(OBJ_ARRAY);
			//push to prevent gc
			stack_push(OBJ_VAL(array));

			if (size > 0) {
				reserveArray(array, size);//allocate after push stack

				//init the array
				memcpy(array->payload, vm.stackTop - size - 1, sizeof(Value) * size);
				array->length = size;

				//pop the values and the temp array at top
				STACK_PEEK(size) = OBJ_VAL(array);
				vm.stackTop -= size;
			}
			NEXT_INSTRUCTION;
		}
		case OP_NEW_OBJECT: {
		label_op_new_object:
			stack_push(OBJ_VAL(newInstance(&vm.emptyClass)));
			NEXT_INSTRUCTION;
		}
		case OP_NEW_PROPERTY: {
		label_op_new_property:
			ObjInstance* instance = AS_INSTANCE(vm.stackTop[-2]);
			Value constant = READ_CONSTANT(READ_24bits());
			ObjString* name = AS_STRING(constant);
			tableSet(&instance->fields, name, vm.stackTop[-1]);
			stack_pop();
			NEXT_INSTRUCTION;
		}
		case OP_GET_UPVALUE: {
		label_op_get_upvalue:
			uint8_t slot = READ_BYTE();
			stack_push(*frame->closure->upvalues[slot]->location);
			NEXT_INSTRUCTION;
		}
		case OP_SET_UPVALUE: {
		label_op_set_upvalue:
			uint8_t slot = READ_BYTE();
			*frame->closure->upvalues[slot]->location = vm.stackTop[-1];
			NEXT_INSTRUCTION;
		}
		case OP_NIL: {
		label_op_nil:
			stack_push(NIL_VAL);
			NEXT_INSTRUCTION;
		}
		case OP_TRUE: {
		label_op_true:
			stack_push(BOOL_VAL(true));
			NEXT_INSTRUCTION;
		}
		case OP_FALSE: {
		label_op_false:
			stack_push(BOOL_VAL(false));
			NEXT_INSTRUCTION;
		}
		case OP_EQUAL: {
		label_op_equal:
			vm.stackTop[-2] = BOOL_VAL(valuesEqual(vm.stackTop[-2], vm.stackTop[-1]));
			vm.stackTop--;
			NEXT_INSTRUCTION;
		}
		case OP_NOT_EQUAL: {
		label_op_not_equal:
			vm.stackTop[-2] = BOOL_VAL(!valuesEqual(vm.stackTop[-2], vm.stackTop[-1]));
			vm.stackTop--;
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
			bool isInstanceOf = (IS_INSTANCE(vm.stackTop[-2]) && IS_CLASS(vm.stackTop[-1])) && (AS_INSTANCE(vm.stackTop[-2])->klass == AS_CLASS(vm.stackTop[-1]));
			vm.stackTop[-2] = BOOL_VAL(isInstanceOf);
			vm.stackTop--;
			NEXT_INSTRUCTION;
		}
		case OP_TYPE_OF: {
		label_op_type_of:
			getTypeof();
			NEXT_INSTRUCTION;
		}
		case OP_ADD: {
		label_op_add:
			// might cause gc,so can't decrease first
			if (IS_NUMBER(vm.stackTop[-2]) && IS_NUMBER(vm.stackTop[-1])) {
				vm.stackTop[-2] = NUMBER_VAL(AS_NUMBER(vm.stackTop[-2]) + AS_NUMBER(vm.stackTop[-1]));
				vm.stackTop--;
				NEXT_INSTRUCTION;
			}
			else if (IS_STRING(vm.stackTop[-2]) && IS_STRING(vm.stackTop[-1])) {
				ObjString* result = connectString(AS_STRING(vm.stackTop[-2]), AS_STRING(vm.stackTop[-1]));
				vm.stackTop[-2] = OBJ_VAL(result);
				vm.stackTop--;
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
			/* Pop the top two values from the stack */
			if (IS_NUMBER(vm.stackTop[-2]) && IS_NUMBER(vm.stackTop[-1])) {
				/* Perform the operation and push the result back */
				vm.stackTop[-2] = NUMBER_VAL(fmod(AS_NUMBER(vm.stackTop[-2]), AS_NUMBER(vm.stackTop[-1])));
				vm.stackTop--;
				NEXT_INSTRUCTION;
			}
			else {
				RUNTIME_ERROR("Operands must be numbers.");
			}
		}

		case OP_NOT: {
		label_op_not:
			vm.stackTop[-1] = BOOL_VAL(isFalsey(vm.stackTop[-1]));
			NEXT_INSTRUCTION;
		}

		case OP_NEGATE: {
		label_op_negate:
			if (IS_NUMBER(vm.stackTop[-1])) {
				vm.stackTop[-1] = NUMBER_VAL(-AS_NUMBER(vm.stackTop[-1]));
				NEXT_INSTRUCTION;
			}
			else {
				RUNTIME_ERROR("Operand must be a number.");
			}
		}

		case OP_BITWISE: {
		label_op_bitwise:
			uint8_t bitOpType = READ_BYTE();
			uint8_t* nextIp = bitInstruction(bitOpType, ip);
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
#if DEBUG_MODE
			printf("[print] ");
#endif
			printValue(stack_pop());
			printf("\n");
			NEXT_INSTRUCTION;
		}
		case OP_THROW: {
		label_op_throw:
			//if solved break else error
			//sync for the stack trace before the error printer walks the frames
			frame->ip = ip;
			if (throwError(stack_pop(), "An exception was thrown.")) {
				NEXT_INSTRUCTION;
			}
			return INTERPRET_RUNTIME_ERROR;
		}
		case OP_GET_LOCAL: {
		label_op_get_local:
			uint32_t index = READ_SHORT();
			stack_push(frame->slots[index]);
			NEXT_INSTRUCTION;
		}
		case OP_SET_LOCAL: {
		label_op_set_local:
			uint32_t index = READ_SHORT();
			frame->slots[index] = vm.stackTop[-1];
			NEXT_INSTRUCTION;
		}
		case OP_SET_LOCAL_POP: {
		label_op_set_local_pop:
			uint32_t index = READ_SHORT();
			frame->slots[index] = vm.stackTop[-1];
			vm.stackTop--;
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
			closeUpvalues(vm.stackTop - 1);
			stack_pop();
			NEXT_INSTRUCTION;
		}
		case OP_POP: {
		label_op_pop:
			stack_pop();
			NEXT_INSTRUCTION;
		}
		case OP_POP_N: {
		label_op_pop_n:
			uint32_t index = READ_SHORT();
			vm.stackTop -= index;
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
			Value condition = vm.stackTop[-1];
			if (isFalsey(condition)) {
				ip += offset;
				NEXT_INSTRUCTION;
			}
			NEXT_INSTRUCTION;
		}
		case OP_JUMP_IF_FALSE_POP: {
		label_op_jump_if_false_pop:
			uint16_t offset = READ_SHORT();
			Value condition = vm.stackTop[-1];
			vm.stackTop--;
			if (isFalsey(condition)) {
				ip += offset;
				NEXT_INSTRUCTION;
			}
			NEXT_INSTRUCTION;
		}
		case OP_JUMP_IF_TRUE: {
		label_op_jump_if_true:
			uint16_t offset = READ_SHORT();
			Value condition = vm.stackTop[-1];
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

			if (!callValue(STACK_PEEK(argCount), argCount)) {
				return INTERPRET_RUNTIME_ERROR;
			}
			//we entered the function
			UPDATE_FRAME();
			NEXT_INSTRUCTION;
		}
		case OP_INVOKE: {
		label_op_invoke:
			Value constant = READ_CONSTANT(READ_24bits());
			ObjString* method = AS_STRING(constant);
			uint8_t argCount = READ_BYTE();
			uint8_t slot = READ_BYTE();
			InlineCacheSlot* icCache = frame->caches;

			Value receiver = STACK_PEEK(argCount);
			if (!IS_INSTANCE(receiver)) {
				RUNTIME_ERROR("Only instances have methods.");
			}
			ObjInstance* instance = AS_INSTANCE(receiver);

			//inline cache: [klass, closure] pointer pair; klass pointer identity is the guard
			//(methods tables are immutable once instances can exist, so the cached closure is stable)
			if (!INSTANCE_POISON(instance) && instance->klass != NULL) {
				InlineCacheSlot* c = &icCache[slot];
				if ((ObjClass*)c->extraA == instance->klass) {
					frame->ip = ip;//change before call
					if (!call((ObjClosure*)c->extraB, argCount)) {
						return INTERPRET_RUNTIME_ERROR;
					}
					//we entered the function
					UPDATE_FRAME();
					NEXT_INSTRUCTION;
				}
			}

			frame->ip = ip;//change before call
			if (!icInvokeSlow(instance, method, argCount, slot, icCache)) {
				return INTERPRET_RUNTIME_ERROR;
			}
			//we entered the function
			UPDATE_FRAME();
			NEXT_INSTRUCTION;
		}
		case OP_SUPER_INVOKE: {
		label_op_super_invoke:
			Value constant = READ_CONSTANT(READ_24bits());
			ObjString* method = AS_STRING(constant);
			uint8_t argCount = READ_BYTE();

			ObjClass* superclass = AS_CLASS(stack_pop());
			frame->ip = ip;//change before call
			if (!invokeFromClass(superclass, method, argCount)) {
				return INTERPRET_RUNTIME_ERROR;
			}
			UPDATE_FRAME();
			NEXT_INSTRUCTION;
		}
		case OP_RETURN: {
		label_op_return:
			Value result = stack_pop();
			//close all remaining upValues of function
			closeUpvalues(frame->slots);
			if (--vm.frameCount == 0) {
				stack_pop();
				return INTERPRET_OK;
			}

			//vm.stackTop = frame->slots;
			//stack_push(result);

			*frame->slots = result;
			vm.stackTop = frame->slots + 1;

			UPDATE_FRAME();
			NEXT_INSTRUCTION;
		}
		case OP_MODULE_BUILTIN: {
		label_op_module_builtin:
			uint8_t moduleIndex = READ_BYTE();
			stack_push(OBJ_VAL(&vm.builtins[moduleIndex]));
			NEXT_INSTRUCTION;
		}
		case OP_IMPORT: {
		label_op_import:
			Value target = vm.stackTop[-1];
			C_STR path = NULL;

			if (IS_STRING(target)) {
				ObjString* pathString = AS_STRING(target);
				path = pathString->chars;
			}
			else if (IS_STRING_BUILDER(target)) {
				ObjArray* pathStringBuilder = AS_ARRAY(target);
				path = pathStringBuilder->payload;
			}
			else {
				RUNTIME_ERROR("Path to import must be a string or stringBuilder.");
			}

			STR absolutePath = getAbsolutePath(path);

			if (absolutePath == NULL) {
				RUNTIME_ERROR("Failed to get absolute file path.");
			}

			ObjFunction* function = getCachedScript(absolutePath);
			mem_free(absolutePath);// free memory
			if (function == NULL) return INTERPRET_COMPILE_ERROR;

			//same as interpret()
			ObjClosure* closure = newClosure(function);
			stack_replace(OBJ_VAL(closure));

			//call the module
			frame->ip = ip;//change before call

			call(closure, 0);

			//we entered the function
			UPDATE_FRAME();
			NEXT_INSTRUCTION;
		}
		case OP_ADD_CONST: {
		label_op_add_const:
			//number only,16bits index into function's own constants
			Value constant = READ_LOCAL_CONSTANT(READ_SHORT());
			// might cause gc,so can't decrease first
			if (IS_NUMBER(vm.stackTop[-1])) {
				vm.stackTop[-1] = NUMBER_VAL(AS_NUMBER(vm.stackTop[-1]) + AS_NUMBER(constant));
				NEXT_INSTRUCTION;
			}

			RUNTIME_ERROR("Operands must be two numbers or two strings.");
		}
		case OP_SUBTRACT_CONST: {
		label_op_subtract_const:
			Value constant = READ_LOCAL_CONSTANT(READ_SHORT());
			BINARY_OP_WITH_RIGHT(NUMBER_VAL, constant, -);
			NEXT_INSTRUCTION;
		}
		case OP_MULTIPLY_CONST: {
		label_op_multiply_const:
			Value constant = READ_LOCAL_CONSTANT(READ_SHORT());
			BINARY_OP_WITH_RIGHT(NUMBER_VAL, constant, *);
			NEXT_INSTRUCTION;
		}
		case OP_DIVIDE_CONST: {
		label_op_divide_const:
			Value constant = READ_LOCAL_CONSTANT(READ_SHORT());
			BINARY_OP_WITH_RIGHT(NUMBER_VAL, constant, / );
			NEXT_INSTRUCTION;
		}
		case OP_MODULUS_CONST: {
		label_op_modulus_const:
			Value constant = READ_LOCAL_CONSTANT(READ_SHORT());
			/* Pop the top two values from the stack */
			if (IS_NUMBER(vm.stackTop[-1])) {
				/* Perform the operation and push the result back */
				vm.stackTop[-1] = NUMBER_VAL(fmod(AS_NUMBER(vm.stackTop[-1]), AS_NUMBER(constant)));
				NEXT_INSTRUCTION;
			}
			else {
				RUNTIME_ERROR("Operands must be numbers.");
			}
		}
		case OP_EQUAL_CONST: {
		label_op_equal_const:
			//non-number constant,24bits index into vm.constants
			Value constant = READ_CONSTANT(READ_24bits());
			vm.stackTop[-1] = BOOL_VAL(valuesEqual(vm.stackTop[-1], constant));
			NEXT_INSTRUCTION;
		}
		case OP_NOT_EQUAL_CONST: {
		label_op_not_equal_const:
			//non-number constant,24bits index into vm.constants
			Value constant = READ_CONSTANT(READ_24bits());
			vm.stackTop[-1] = BOOL_VAL(!valuesEqual(vm.stackTop[-1], constant));
			NEXT_INSTRUCTION;
		}
		case OP_GREATER_CONST: {
		label_op_greater_const:
			Value constant = READ_LOCAL_CONSTANT(READ_SHORT());
			COMPARE_OP_WITH_RIGHT(constant, > );
			NEXT_INSTRUCTION;
		}
		case OP_LESS_CONST: {
		label_op_less_const:
			Value constant = READ_LOCAL_CONSTANT(READ_SHORT());
			COMPARE_OP_WITH_RIGHT(constant, < );
			NEXT_INSTRUCTION;
		}
		case OP_GREATER_EQUAL_CONST: {
		label_op_greater_equal_const:
			Value constant = READ_LOCAL_CONSTANT(READ_SHORT());
			COMPARE_OP_WITH_RIGHT(constant, >= );
			NEXT_INSTRUCTION;
		}
		case OP_LESS_EQUAL_CONST: {
		label_op_less_equal_const:
			Value constant = READ_LOCAL_CONSTANT(READ_SHORT());
			COMPARE_OP_WITH_RIGHT(constant, <= );
			NEXT_INSTRUCTION;
		}
		case OP_EQUAL_CONST_NUMBER: {
		label_op_equal_const_number:
			//number only,16bits index into function's own constants
			Value constant = READ_LOCAL_CONSTANT(READ_SHORT());
			vm.stackTop[-1] = BOOL_VAL(valuesEqual(vm.stackTop[-1], constant));
			NEXT_INSTRUCTION;
		}
		case OP_NOT_EQUAL_CONST_NUMBER: {
		label_op_not_equal_const_number:
			//number only,16bits index into function's own constants
			Value constant = READ_LOCAL_CONSTANT(READ_SHORT());
			vm.stackTop[-1] = BOOL_VAL(!valuesEqual(vm.stackTop[-1], constant));
			NEXT_INSTRUCTION;
		}

		case OP_ADD_LOCAL: {
		label_op_add_local:
			uint32_t index = READ_SHORT();
			Value local = frame->slots[index];

			// might cause gc,so can't decrease first
			if (IS_NUMBER(vm.stackTop[-1]) && IS_NUMBER(local)) {
				vm.stackTop[-1] = NUMBER_VAL(AS_NUMBER(vm.stackTop[-1]) + AS_NUMBER(local));
				NEXT_INSTRUCTION;
			}
			else if (IS_STRING(vm.stackTop[-1]) && IS_STRING(local)) {
				ObjString* result = connectString(AS_STRING(vm.stackTop[-1]), AS_STRING(local));
				vm.stackTop[-1] = OBJ_VAL(result);
				NEXT_INSTRUCTION;
			}

			RUNTIME_ERROR("Operands must be two numbers or two strings.");
		}
		case OP_SUBTRACT_LOCAL: {
		label_op_subtract_local:
			uint32_t index = READ_SHORT();
			Value local = frame->slots[index];

			BINARY_OP_WITH_RIGHT(NUMBER_VAL, local, -);
			NEXT_INSTRUCTION;
		}
		case OP_MULTIPLY_LOCAL: {
		label_op_multiply_local:
			uint32_t index = READ_SHORT();
			Value local = frame->slots[index];

			BINARY_OP_WITH_RIGHT(NUMBER_VAL, local, *);
			NEXT_INSTRUCTION;
		}
		case OP_DIVIDE_LOCAL: {
		label_op_divide_local:
			uint32_t index = READ_SHORT();
			Value local = frame->slots[index];

			BINARY_OP_WITH_RIGHT(NUMBER_VAL, local, / );
			NEXT_INSTRUCTION;
		}
		case OP_MODULUS_LOCAL: {
		label_op_modulus_local:
			uint32_t index = READ_SHORT();
			Value local = frame->slots[index];

			/* Pop the top two values from the stack */
			if (IS_NUMBER(vm.stackTop[-1]) && IS_NUMBER(local)) {
				/* Perform the operation and push the result back */
				vm.stackTop[-1] = NUMBER_VAL(fmod(AS_NUMBER(vm.stackTop[-1]), AS_NUMBER(local)));
				NEXT_INSTRUCTION;
			}
			else {
				RUNTIME_ERROR("Operands must be numbers.");
			}
		}
		case OP_EQUAL_LOCAL: {
		label_op_equal_local:
			uint32_t index = READ_SHORT();
			Value local = frame->slots[index];
			vm.stackTop[-1] = BOOL_VAL(valuesEqual(vm.stackTop[-1], local));
			NEXT_INSTRUCTION;
		}
		case OP_NOT_EQUAL_LOCAL: {
		label_op_not_equal_local:
			uint32_t index = READ_SHORT();
			Value local = frame->slots[index];
			vm.stackTop[-1] = BOOL_VAL(!valuesEqual(vm.stackTop[-1], local));
			NEXT_INSTRUCTION;
		}
		case OP_GREATER_LOCAL: {
		label_op_greater_local:
			uint32_t index = READ_SHORT();
			Value local = frame->slots[index];
			COMPARE_OP_WITH_RIGHT(local, > );
			NEXT_INSTRUCTION;
		}
		case OP_LESS_LOCAL: {
		label_op_less_local:
			uint32_t index = READ_SHORT();
			Value local = frame->slots[index];
			COMPARE_OP_WITH_RIGHT(local, < );
			NEXT_INSTRUCTION;
		}
		case OP_GREATER_EQUAL_LOCAL: {
		label_op_greater_equal_local:
			uint32_t index = READ_SHORT();
			Value local = frame->slots[index];
			COMPARE_OP_WITH_RIGHT(local, >= );
			NEXT_INSTRUCTION;
		}
		case OP_LESS_EQUAL_LOCAL: {
		label_op_less_equal_local:
			uint32_t index = READ_SHORT();
			Value local = frame->slots[index];
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
			Value left = frame->slots[indexA];
			Value right = frame->slots[indexB];
			JIF_CMP_WITH_RIGHT(left, right, offset, < );
		}
		case OP_JIF_LESS_EQUAL_LL: {
		label_op_jif_less_equal_ll:
			uint32_t indexA = READ_SHORT();
			uint32_t indexB = READ_SHORT();
			uint16_t offset = READ_SHORT();
			Value left = frame->slots[indexA];
			Value right = frame->slots[indexB];
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
			Value constant = READ_LOCAL_CONSTANT(READ_SHORT());
			uint16_t offset = READ_SHORT();
			Value local = frame->slots[index];
			JIF_CMP_NUM_WITH_RIGHT(local, constant, offset, < );
		}
		case OP_JIF_LESS_EQUAL_LC_NUMBER: {
		label_op_jif_less_equal_lc_number:
			uint32_t index = READ_SHORT();
			Value constant = READ_LOCAL_CONSTANT(READ_SHORT());
			uint16_t offset = READ_SHORT();
			Value local = frame->slots[index];
			JIF_CMP_NUM_WITH_RIGHT(local, constant, offset, <= );
		}
		case OP_JIF_GREATER_LC_NUMBER: {
		label_op_jif_greater_lc_number:
			uint32_t index = READ_SHORT();
			Value constant = READ_LOCAL_CONSTANT(READ_SHORT());
			uint16_t offset = READ_SHORT();
			Value local = frame->slots[index];
			JIF_CMP_NUM_WITH_RIGHT(local, constant, offset, > );
		}
		case OP_JIF_GREATER_EQUAL_LC_NUMBER: {
		label_op_jif_greater_equal_lc_number:
			uint32_t index = READ_SHORT();
			Value constant = READ_LOCAL_CONSTANT(READ_SHORT());
			uint16_t offset = READ_SHORT();
			Value local = frame->slots[index];
			JIF_CMP_NUM_WITH_RIGHT(local, constant, offset, >= );
		}
		case OP_JIF_EQUAL_LC_NUMBER: {
		label_op_jif_equal_lc_number:
			uint32_t index = READ_SHORT();
			Value constant = READ_LOCAL_CONSTANT(READ_SHORT());
			uint16_t offset = READ_SHORT();
			if (valuesEqual(frame->slots[index], constant)) NEXT_INSTRUCTION;
			ip += offset;
			NEXT_INSTRUCTION;
		}
		case OP_JIF_NOT_EQUAL_LC_NUMBER: {
		label_op_jif_not_equal_lc_number:
			uint32_t index = READ_SHORT();
			Value constant = READ_LOCAL_CONSTANT(READ_SHORT());
			uint16_t offset = READ_SHORT();
			if (!valuesEqual(frame->slots[index], constant)) NEXT_INSTRUCTION;
			ip += offset;
			NEXT_INSTRUCTION;
		}

		//fused equality, non-number constant:[op:8][local:16][const:24][offset:16]
		case OP_JIF_EQUAL_LC: {
		label_op_jif_equal_lc:
			uint32_t index = READ_SHORT();
			Value constant = READ_CONSTANT(READ_24bits());
			uint16_t offset = READ_SHORT();
			if (valuesEqual(frame->slots[index], constant)) NEXT_INSTRUCTION;
			ip += offset;
			NEXT_INSTRUCTION;
		}
		case OP_JIF_NOT_EQUAL_LC: {
		label_op_jif_not_equal_lc:
			uint32_t index = READ_SHORT();
			Value constant = READ_CONSTANT(READ_24bits());
			uint16_t offset = READ_SHORT();
			if (!valuesEqual(frame->slots[index], constant)) NEXT_INSTRUCTION;
			ip += offset;
			NEXT_INSTRUCTION;
		}

		case OP_NOT_LOCAL: {
		label_op_not_local:
			uint32_t index = READ_SHORT();
			Value local = frame->slots[index];

			stack_push(BOOL_VAL(isFalsey(local)));
			NEXT_INSTRUCTION;
		}
		case OP_NEGATE_LOCAL: {
		label_op_negate_local:
			uint32_t index = READ_SHORT();
			Value local = frame->slots[index];

			if (IS_NUMBER(local)) {
				stack_push(NUMBER_VAL(-AS_NUMBER(local)));
				NEXT_INSTRUCTION;
			}
			else {
				RUNTIME_ERROR("Operand must be a number.");
			}
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

InterpretResult interpret(C_STR source)
{
#if LOG_COMPILE_TIMING
	uint64_t time_compile = get_milliseconds();
#endif

	ObjFunction* function = compile(source, TYPE_SCRIPT);
	if (function == NULL) return INTERPRET_COMPILE_ERROR;

#if LOG_COMPILE_TIMING
	double time_compile_f = (get_milliseconds() - time_compile);
	printf("[Log] Finished compiling in %g ms.\n", time_compile_f);
#endif

	//stack_push(OBJ_VAL(function));
	ObjClosure* closure = newClosure(function);
	//stack_replace(OBJ_VAL(closure));
	stack_push(OBJ_VAL(closure));
	call(closure, 0);

#if LOG_EXECUTE_TIMING
	uint64_t time_run = get_milliseconds();
#endif

	InterpretResult result = run();

#if LOG_EXECUTE_TIMING
	double time_run_f = (get_milliseconds() - time_run);
	printf("[Log] Finished executing in %g ms.\n", time_run_f);
#endif

	return result;
}

InterpretResult interpret_repl(C_STR source) {
	InterpretResult res = interpret(source);
	stack_reset();//clean stack after use
	return res;
}