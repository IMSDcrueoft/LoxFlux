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

	if ((vm.frameCount - 1) >= 0) {
		vm.frames[vm.frameCount - 1].ip = *vm.ip_error;
	}

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

	if ((vm.frameCount - 1) >= 0) {
		vm.frames[vm.frameCount - 1].ip = *vm.ip_error;
	}

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

		if (capacity > UINT24_COUNT) {
			runtimeError("Stack overflow.");
			return;
		}

		vm.stack = GROW_ARRAY_NO_GC(Value, vm.stack, oldCapacity, capacity);
		vm.stackBoundary = vm.stack + capacity;		//need fresh
		vm.stackTop = vm.stack + oldCapacity;		//need fresh
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

	tableSet(&vm.globals,
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

	vm.globals = (Table){ .isGlobal = true,.isFrozen = false };//remind this
	table_init(&vm.globals);

	stringTable_init(&vm.scripts);
	stringTable_init(&vm.strings);
	numberTable_init(&vm.numbers);

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

	vm.ip_error = NULL;

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
void vm_free()
{
	valueArray_free(&vm.constants);
	valueHoles_free(&vm.constantHoles);

	table_free(&vm.globals);
	stringTable_free(&vm.scripts);
	stringTable_free(&vm.strings);
	numberTable_free(&vm.numbers);

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

	vm.ip_error = NULL;
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
	if (argCount > closure->function->arity) {
		runtimeError("Expected %d arguments but got %d.",
			closure->function->arity, argCount);
		return false;
	}

	if (vm.frameCount == FRAMES_MAX) {
		runtimeError("Stack overflow.");
		return false;
	}

	while (argCount < closure->function->arity) {
		stack_push(NIL_VAL);
		++argCount;
	}

	CallFrame* frame = &vm.frames[vm.frameCount++];
	frame->closure = closure;
	frame->ip = closure->function->chunk.code;
	//-1 is for function itself
	frame->slots = vm.stackTop - argCount - 1;
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

HOT_FUNCTION
static inline bool invoke(ObjString* name, int argCount) {
	Value receiver = STACK_PEEK(argCount);
	if (!IS_INSTANCE(receiver)) {
		runtimeError("Only instances have methods.");
		return false;
	}
	ObjInstance* instance = AS_INSTANCE(receiver);

	Value value;
	if (tableGet(&instance->fields, name, &value)) {
		STACK_PEEK(argCount) = value;
		return callValue(value, argCount);
	}

	return invokeFromClass(instance->klass, name, argCount);
}

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

	ObjUpvalue* createdUpvalue = newUpvalue(local);

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
static bool bitInstruction(uint8_t bitOpType) {
#define BIARAY_OP_BIT(op)																			\
    do {																							\
		/* Pop the top two values from the stack */													\
		if (IS_NUMBER(vm.stackTop[-2]) && IS_NUMBER(vm.stackTop[-1])) {								\
			/* Perform the operation and push the result back */									\
			vm.stackTop[-2] = NUMBER_VAL((int32_t)AS_NUMBER(vm.stackTop[-2]) op (int32_t)AS_NUMBER(vm.stackTop[-1]));	\
			vm.stackTop--;																			\
			return true;																			\
		}																							\
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
	};

	goto* bit_op_labels[bitOpType];
#endif

	switch (bitOpType)
	{
	case BIT_OP_NOT: {
	label_bit_not:
		if (IS_NUMBER(vm.stackTop[-1])) {
			vm.stackTop[-1] = NUMBER_VAL(~(int32_t)AS_NUMBER(vm.stackTop[-1]));
			return true;
		}
		break;
	}
	case BIT_OP_AND: {
	label_bit_and:
		BIARAY_OP_BIT(&);
		break;
	}
	case BIT_OP_OR: {
	label_bit_or:
		BIARAY_OP_BIT(| );
		break;
	}
	case BIT_OP_XOR: {
	label_bit_xor:
		BIARAY_OP_BIT(^);
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
			return true;
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
			return true;
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
			return true;
		}
		break;
	}
	}
	return false;
#undef BIARAY_OP_BIT
}

//to run code in vm
HOT_FUNCTION
static InterpretResult run()
{
	CallFrame* frame = &vm.frames[vm.frameCount - 1];
	uint8_t* ip = frame->ip;
	//if error,use this to print
	vm.ip_error = &ip;

#if COMPUTE_GOTO
	static void* label_instructions[] = {
		[OP_CONSTANT] = && label_op_constant,

		[OP_GET_LOCAL] = && label_op_get_local,
		[OP_SET_LOCAL] = && label_op_set_local,

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
	};
#endif

#define READ_BYTE() (*(ip++))
#define READ_SHORT() (ip += 2, (uint16_t)(ip[-2] | (ip[-1] << 8)))
#define READ_24bits() (ip += 3, (uint32_t)(ip[-3] | (ip[-2] << 8) | (ip[-1] << 16)))
#define READ_CONSTANT(index) (vm.constants.values[(index)])

	// push(pop() op pop())
#define BINARY_OP(valueType,op)																		\
    do {																							\
		/* Pop the top two values from the stack */													\
		if (IS_NUMBER(vm.stackTop[-2]) && IS_NUMBER(vm.stackTop[-1])) {								\
			/* Perform the operation and push the result back */									\
			vm.stackTop[-2] = valueType(AS_NUMBER(vm.stackTop[-2]) op AS_NUMBER(vm.stackTop[-1]));	\
			vm.stackTop--;																			\
		} else {														                            \
			runtimeError("Operands must be numbers.");										\
			return INTERPRET_RUNTIME_ERROR;															\
		}																							\
	} while (false)

#define BINARY_OP_WITH_RIGHT(valueType,right,op)											\
    do {																					\
		/* Pop the top two values from the stack */											\
		if (IS_NUMBER(vm.stackTop[-1]) && IS_NUMBER(right)) {								\
			/* Perform the operation and push the result back */							\
			vm.stackTop[-1] = valueType(AS_NUMBER(vm.stackTop[-1]) op AS_NUMBER(right));	\
		} else {																			\
			runtimeError("Operands must be numbers.");								\
			return INTERPRET_RUNTIME_ERROR;													\
		}																					\
	} while (false)

//we need continue/break when debug trace
#if !COMPUTE_GOTO || DEBUG_TRACE_EXECUTION
#define NEXT_INSTRUCTION continue
#else //direct threading code
#define NEXT_INSTRUCTION goto *label_instructions[READ_BYTE()]
#endif

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
		disassembleInstruction(&frame->closure->function->chunk, (uint32_t)(ip - frame->closure->function->chunk.code));
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
				stack_pop(); // Subclass.
			}
			else {
				runtimeError("Superclass must be a class.");
				return INTERPRET_RUNTIME_ERROR;
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
				runtimeError("Only instances have properties.");
				return INTERPRET_RUNTIME_ERROR;
			}

			ObjInstance* instance = AS_INSTANCE(vm.stackTop[-1]);
			Value constant = READ_CONSTANT(READ_24bits());
			ObjString* name = AS_STRING(constant);

			Value value;
			if (tableGet(&instance->fields, name, &value)) {
				stack_replace(value);
				NEXT_INSTRUCTION;
			}
			//don't throw error
			if (instance->klass != NULL) {
				bindMethod(instance->klass, name);
			}
			NEXT_INSTRUCTION;
		}
		case OP_SET_PROPERTY: {
		label_op_set_property:
			if (!IS_INSTANCE(vm.stackTop[-2])) {
				runtimeError("Only instances have fields.");
				return INTERPRET_RUNTIME_ERROR;
			}

			ObjInstance* instance = AS_INSTANCE(vm.stackTop[-2]);
			Value constant = READ_CONSTANT(READ_24bits());
			ObjString* name = AS_STRING(constant);
			if (NOT_NIL(vm.stackTop[-1])) {
				tableSet(&instance->fields, name, vm.stackTop[-1]);
			}
			else {
				tableDelete(&instance->fields, name);
			}
			Value value = stack_pop();
			stack_replace(value);
			NEXT_INSTRUCTION;
		}
		case OP_GET_INDEX: {
		label_op_get_index:
			Value target = vm.stackTop[-1];
			Value constant = READ_CONSTANT(READ_24bits());
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

			runtimeError("Only arrayLike,stringBuilder and string can get number subscript.");
			return INTERPRET_RUNTIME_ERROR;
		}
		case OP_SET_INDEX: {
		label_op_set_index:
			Value target = vm.stackTop[-2];
			Value value = vm.stackTop[-1];

			Value constant = READ_CONSTANT(READ_24bits());
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
					runtimeError("Array index out of range.");
					return INTERPRET_RUNTIME_ERROR;
				}
			}

			runtimeError("Only arrayLike can set number subscript.");
			return INTERPRET_RUNTIME_ERROR;
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
					runtimeError("Array subscript must be number.");
					return INTERPRET_RUNTIME_ERROR;
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
					NEXT_INSTRUCTION;
				}
				else {
					runtimeError("Instance subscript must be string.");
					return INTERPRET_RUNTIME_ERROR;
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
					runtimeError("String subscript must be number.");
					return INTERPRET_RUNTIME_ERROR;
				}
			}

			runtimeError("Only instances,arrayLike,stringBuilder and string can get subscript.");
			return INTERPRET_RUNTIME_ERROR;
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
						runtimeError("Array index out of range.");
						return INTERPRET_RUNTIME_ERROR;
					}
				}
				else {
					runtimeError("Array subscript must be number.");
					return INTERPRET_RUNTIME_ERROR;
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
					runtimeError("Instance subscript must be string.");
					return INTERPRET_RUNTIME_ERROR;
				}
			}

			runtimeError("Only instances and arrayLike can set subscript.");
			return INTERPRET_RUNTIME_ERROR;
		}
		case OP_DEFINE_GLOBAL: {
		label_op_define_global:
			Value constant = READ_CONSTANT(READ_24bits());
			ObjString* name = AS_STRING(constant);

			//it's not a new key,no cache
			tableSet(&vm.globals, name, vm.stackTop[-1]);
			vm.stackTop--;//can not dec first,because gc will kill it
			NEXT_INSTRUCTION;
		}
		case OP_GET_GLOBAL: {
		label_op_get_global:
			Value constant = READ_CONSTANT(READ_24bits());
			ObjString* name = AS_STRING(constant);

			//inline find by cache symbol
			if ((name->symbol != INVALID_OBJ_STRING_SYMBOL)) {
				Entry* entry = &vm.globals.entries[name->symbol];

				if (entry->key == name) {
					// We found the key.
					stack_push(entry->value);
					NEXT_INSTRUCTION;
				}
			}

			Value value;
			if (!tableGet(&vm.globals, name, &value)) {
				runtimeError("Undefined variable '%s'.", name->chars);
				return INTERPRET_RUNTIME_ERROR;
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
				Entry* entry = &vm.globals.entries[name->symbol];
				if (entry->key == name) {
					// We found the key.
					entry->value = vm.stackTop[-1];
					NEXT_INSTRUCTION;
				}
			}

			if (tableSet(&vm.globals, name, vm.stackTop[-1])) {
				//lox dont allow setting undefined one
				tableDelete(&vm.globals, name);
				runtimeError("Undefined variable '%s'.", name->chars);
				return INTERPRET_RUNTIME_ERROR;
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
			BINARY_OP(BOOL_VAL, > );
			NEXT_INSTRUCTION;
		}
		case OP_LESS: {
		label_op_less:
			BINARY_OP(BOOL_VAL, < );
			NEXT_INSTRUCTION;
		}
		case OP_GREATER_EQUAL: {
		label_op_greater_equal:
			BINARY_OP(BOOL_VAL, >= );
			NEXT_INSTRUCTION;
		}
		case OP_LESS_EQUAL: {
		label_op_less_equal:
			BINARY_OP(BOOL_VAL, <= );
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

			runtimeError("Operands must be two numbers or two strings.");
			return INTERPRET_RUNTIME_ERROR;
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
				runtimeError("Operands must be numbers.");
				return INTERPRET_RUNTIME_ERROR;
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
				runtimeError("Operand must be a number.");
				return INTERPRET_RUNTIME_ERROR;
			}
		}

		case OP_BITWISE: {
		label_op_bitwise:
			uint8_t bitOpType = READ_BYTE();
			if (bitInstruction(bitOpType)) {
				NEXT_INSTRUCTION;
			}
			else {
				runtimeError("Operands must be numbers.");
				return INTERPRET_RUNTIME_ERROR;
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
			if (isFalsey(vm.stackTop[-1])) ip += offset;
			NEXT_INSTRUCTION;
		}
		case OP_JUMP_IF_FALSE_POP: {
		label_op_jump_if_false_pop:
			uint16_t offset = READ_SHORT();
			if (isFalsey(vm.stackTop[-1])) ip += offset;
			vm.stackTop--;
			NEXT_INSTRUCTION;
		}
		case OP_JUMP_IF_TRUE: {
		label_op_jump_if_true:
			uint16_t offset = READ_SHORT();
			if (isTruthy(vm.stackTop[-1])) ip += offset;
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
			frame = &vm.frames[vm.frameCount - 1];
			ip = frame->ip;//restore after call
			NEXT_INSTRUCTION;
		}
		case OP_INVOKE: {
		label_op_invoke:
			Value constant = READ_CONSTANT(READ_24bits());
			ObjString* method = AS_STRING(constant);
			uint8_t argCount = READ_BYTE();

			frame->ip = ip;//change before call
			if (!invoke(method, argCount)) {
				return INTERPRET_RUNTIME_ERROR;
			}
			//we entered the function
			frame = &vm.frames[vm.frameCount - 1];
			ip = frame->ip;//restore after call
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
			frame = &vm.frames[vm.frameCount - 1];
			ip = frame->ip;//restore after call
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

			frame = &vm.frames[vm.frameCount - 1];
			ip = frame->ip;
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
				runtimeError("Path to import must be a string or stringBuilder.");
				return INTERPRET_RUNTIME_ERROR;
			}

			STR absolutePath = getAbsolutePath(path);

			if (absolutePath == NULL) {
				runtimeError("Failed to get absolute file path.");
				return INTERPRET_RUNTIME_ERROR;
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
			frame = &vm.frames[vm.frameCount - 1];
			ip = frame->ip;//restore after call
			NEXT_INSTRUCTION;
		}
		case OP_ADD_CONST: {
		label_op_add_const:
			Value constant = READ_CONSTANT(READ_24bits());
			// might cause gc,so can't decrease first
			if (IS_NUMBER(vm.stackTop[-1]) && IS_NUMBER(constant)) {
				vm.stackTop[-1] = NUMBER_VAL(AS_NUMBER(vm.stackTop[-1]) + AS_NUMBER(constant));
				NEXT_INSTRUCTION;
			}
			else if (IS_STRING(vm.stackTop[-1]) && IS_STRING(constant)) {
				ObjString* result = connectString(AS_STRING(vm.stackTop[-1]), AS_STRING(constant));
				vm.stackTop[-1] = OBJ_VAL(result);
				NEXT_INSTRUCTION;
			}

			runtimeError("Operands must be two numbers or two strings.");
			return INTERPRET_RUNTIME_ERROR;
		}
		case OP_SUBTRACT_CONST: {
		label_op_subtract_const:
			Value constant = READ_CONSTANT(READ_24bits());
			BINARY_OP_WITH_RIGHT(NUMBER_VAL, constant, -);
			NEXT_INSTRUCTION;
		}
		case OP_MULTIPLY_CONST: {
		label_op_multiply_const:
			Value constant = READ_CONSTANT(READ_24bits());
			BINARY_OP_WITH_RIGHT(NUMBER_VAL, constant, *);
			NEXT_INSTRUCTION;
		}
		case OP_DIVIDE_CONST: {
		label_op_divide_const:
			Value constant = READ_CONSTANT(READ_24bits());
			BINARY_OP_WITH_RIGHT(NUMBER_VAL, constant, / );
			NEXT_INSTRUCTION;
		}
		case OP_MODULUS_CONST: {
		label_op_modulus_const:
			Value constant = READ_CONSTANT(READ_24bits());
			/* Pop the top two values from the stack */
			if (IS_NUMBER(vm.stackTop[-1]) && IS_NUMBER(constant)) {
				/* Perform the operation and push the result back */
				vm.stackTop[-1] = NUMBER_VAL(fmod(AS_NUMBER(vm.stackTop[-1]), AS_NUMBER(constant)));
				NEXT_INSTRUCTION;
			}
			else {
				runtimeError("Operands must be numbers.");
				return INTERPRET_RUNTIME_ERROR;
			}
		}
		case OP_EQUAL_CONST: {
		label_op_equal_const:
			Value constant = READ_CONSTANT(READ_24bits());
			vm.stackTop[-1] = BOOL_VAL(valuesEqual(vm.stackTop[-1], constant));
			NEXT_INSTRUCTION;
		}
		case OP_NOT_EQUAL_CONST: {
		label_op_not_equal_const:
			Value constant = READ_CONSTANT(READ_24bits());
			vm.stackTop[-1] = BOOL_VAL(!valuesEqual(vm.stackTop[-1], constant));
			NEXT_INSTRUCTION;
		}
		case OP_GREATER_CONST: {
		label_op_greater_const:
			Value constant = READ_CONSTANT(READ_24bits());
			BINARY_OP_WITH_RIGHT(BOOL_VAL, constant, > );
			NEXT_INSTRUCTION;
		}
		case OP_LESS_CONST: {
		label_op_less_const:
			Value constant = READ_CONSTANT(READ_24bits());
			BINARY_OP_WITH_RIGHT(BOOL_VAL, constant, < );
			NEXT_INSTRUCTION;
		}
		case OP_GREATER_EQUAL_CONST: {
		label_op_greater_equal_const:
			Value constant = READ_CONSTANT(READ_24bits());
			BINARY_OP_WITH_RIGHT(BOOL_VAL, constant, >= );
			NEXT_INSTRUCTION;
		}
		case OP_LESS_EQUAL_CONST: {
		label_op_less_equal_const:
			Value constant = READ_CONSTANT(READ_24bits());
			BINARY_OP_WITH_RIGHT(BOOL_VAL, constant, <= );
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

			runtimeError("Operands must be two numbers or two strings.");
			return INTERPRET_RUNTIME_ERROR;
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
				runtimeError("Operands must be numbers.");
				return INTERPRET_RUNTIME_ERROR;
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
			BINARY_OP_WITH_RIGHT(BOOL_VAL, local, > );
			NEXT_INSTRUCTION;
		}
		case OP_LESS_LOCAL: {
		label_op_less_local:
			uint32_t index = READ_SHORT();
			Value local = frame->slots[index];
			BINARY_OP_WITH_RIGHT(BOOL_VAL, local, < );
			NEXT_INSTRUCTION;
		}
		case OP_GREATER_EQUAL_LOCAL: {
		label_op_greater_equal_local:
			uint32_t index = READ_SHORT();
			Value local = frame->slots[index];
			BINARY_OP_WITH_RIGHT(BOOL_VAL, local, >= );
			NEXT_INSTRUCTION;
		}
		case OP_LESS_EQUAL_LOCAL: {
		label_op_less_equal_local:
			uint32_t index = READ_SHORT();
			Value local = frame->slots[index];
			BINARY_OP_WITH_RIGHT(BOOL_VAL, local, <= );
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
				break;
			}
			else {
				runtimeError("Operand must be a number.");
				return INTERPRET_RUNTIME_ERROR;
			}
		}
		}
	}

	//the place the error happens
#undef READ_BYTE
#undef READ_SHORT
#undef READ_24bits
#undef READ_CONSTANT
#undef BINARY_OP
#undef BINARY_OP_WITH_RIGHT
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