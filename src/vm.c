/*
 * MIT License
 * Copyright (c) 2025 IM&SD (https://github.com/IMSDcrueoft)
 * See LICENSE file in the root directory for full license text.
*/
#include "vm.h"
#include "object.h"
#include "builtinModule.h"
#include "native.h"
#include "gc.h"

#if DEBUG_TRACE_EXECUTION
#include "debug.h"
#endif

#if LOG_EXECUTE_TIMING
#include "timer.h"
#endif

//the global shared vm
VM vm;
//the builtin modules
Table builtinModules[BUILTIN_MODULE_COUNT];
//ip for debug
uint8_t** ip_error = NULL;
#if LOG_KIPS
uint64_t byteCodeCount;
#endif

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

static bool throwError(Value error) {
	printf("[ThrowError] ");
	printValue(error);
	printf("\n");

	if ((vm.frameCount - 1) >= 0) {
		vm.frames[vm.frameCount - 1].ip = *ip_error;
	}

	for (int32_t i = vm.frameCount - 1; i >= 0; i--) {
		CallFrame* frame = &vm.frames[i];
    
		ObjFunction* function = frame->closure->function;
		size_t instruction = frame->ip - function->chunk.code - 1;

		uint32_t line = getLine(&function->chunk.lines, (uint32_t)instruction);

		fprintf(stderr, "[line %d] in ", line);
		if (function->name == NULL) {
			fprintf(stderr, "script\n");
		}
		else {
			fprintf(stderr, "%s()\n", function->name->chars);
		}
	}

	stack_reset();
	return false;
}

static void runtimeError(C_STR format, ...) {
	printf("[RuntimeError] ");

	va_list args;
	va_start(args, format);
	vfprintf(stderr, format, args);
	va_end(args);
	fputs("\n", stderr);

	if ((vm.frameCount - 1) >= 0) {
		vm.frames[vm.frameCount - 1].ip = *ip_error;
	}

	for (int32_t i = vm.frameCount - 1; i >= 0; i--) {
		CallFrame* frame = &vm.frames[i];
		ObjFunction* function = frame->closure->function;
		size_t instruction = frame->ip - function->chunk.code - 1;

		uint32_t line = getLine(&function->chunk.lines, (uint32_t)instruction);

		fprintf(stderr, "[line %d] in ", line);
		if (function->name == NULL) {
			fprintf(stderr, "script\n");
		}
		else {
			fprintf(stderr, "%s()\n", function->name->chars);
		}
	}

	stack_reset();
}

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

static inline void stack_replace(Value val) {
	vm.stackTop[-1] = val;
}

Value stack_pop()
{
	vm.stackTop--;
	return *vm.stackTop;
}

#define STACK_PEEK(distance) (vm.stackTop[-1 - distance])

void defineNative(C_STR name, NativeFn function) {
	stack_push(OBJ_VAL(copyString(name, (uint32_t)strlen(name), false)));
	stack_push(OBJ_VAL(newNative(function)));
	tableSet(&vm.globals, AS_STRING(vm.stack[0]), vm.stack[1]);
	stack_pop();
	stack_pop();
}

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

	table_init(&vm.globals);
	vm.globals.type = TABLE_GLOBAL;//remind this
	table_init(&vm.strings);
	vm.strings.type = TABLE_NORMAL;
	numberTable_init(&vm.numbers);

	vm.objects = NULL;

	//init gray stack
	vm.grayCount = 0;
	vm.grayCapacity = 0;
	vm.grayStack = NULL;

	//set
	vm.bytesAllocated = 0;
	vm.nextGC = GC_HEAP_BEGIN;

	//import native funcs
	importNative();
}

void vm_free()
{
	valueArray_free(&vm.constants);
	valueHoles_free(&vm.constantHoles);

	table_free(&vm.globals);
	table_free(&vm.strings);
	numberTable_free(&vm.numbers);

	freeObjects();

	//realease the stack
	ptrdiff_t capacity = vm.stackBoundary - vm.stack;
	FREE_ARRAY_NO_GC(Value, vm.stack, capacity);
	vm.stack = NULL;
	vm.stackTop = NULL;
	vm.stackBoundary = NULL;
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

static bool call_native(NativeFn native, int argCount) {
	C_STR errorInfo = NULL;
	Value result = native(argCount, vm.stackTop - argCount, &errorInfo);
	if (errorInfo != NULL) {
		runtimeError("Error in Native -> %s.", errorInfo);
		return false;
	}
	vm.stackTop -= argCount + 1;
	stack_push(result);
	return true;
}

static bool callValue(Value callee, int argCount) {
	if (IS_OBJ(callee)) {
		switch (OBJ_TYPE(callee)) {
		case OBJ_CLOSURE: return call(AS_CLOSURE(callee), argCount);
		case OBJ_NATIVE: return call_native(AS_NATIVE(callee), argCount);
		}
	}

	runtimeError("Can only call functions and classes.");
	return false;
}

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

static void closeUpvalues(Value* last) { 
	while (vm.openUpvalues != NULL && vm.openUpvalues->location >= last) {
		ObjUpvalue* upvalue = vm.openUpvalues;

		//if one upValue closed,it's location is it's closed's pointer
		upvalue->closed = *upvalue->location;
		upvalue->location = &upvalue->closed;
		vm.openUpvalues = upvalue->next;
	}
}

static inline bool isFalsey(Value value) {
	return IS_NIL(value) || (IS_BOOL(value) && !AS_BOOL(value));
}

static inline bool isTruthy(Value value) {
	return !IS_NIL(value) && (!IS_BOOL(value) || AS_BOOL(value));
}

//to run code in vm
static InterpretResult run()
{
	CallFrame* frame = &vm.frames[vm.frameCount - 1];
	uint8_t* ip = frame->ip;
	//if error,use this to print
	ip_error = &ip;

#define READ_BYTE() (*(ip++))
#define READ_SHORT() (ip += 2, (uint16_t)(ip[-2] | (ip[-1] << 8)))
#define READ_24bits() (ip += 3, (uint32_t)(ip[-3] | (ip[-2] << 8) | (ip[-1] << 16)))
#define READ_CONSTANT(index) (vm.constants.values[(index)])

	// push(pop() op pop())
#define BINARY_OP(valueType,op)																	\
    do {																						\
		/* Pop the top two values from the stack */												\
        vm.stackTop--;																			\
		if (!IS_NUMBER(vm.stackTop[0]) || !IS_NUMBER(vm.stackTop[-1])) {						\
			runtimeError("Operands must be numbers.");									\
			return INTERPRET_RUNTIME_ERROR;														\
		}																						\
        /* Perform the operation and push the result back */									\
		vm.stackTop[-1] = valueType(AS_NUMBER(vm.stackTop[-1]) op AS_NUMBER(vm.stackTop[0]));	\
	} while (false)

#define BINARY_OP_MODULUS(valueType)																	\
    do {																								\
		/* Pop the top two values from the stack */														\
        vm.stackTop--;																					\
		if (!IS_NUMBER(vm.stackTop[0]) || !IS_NUMBER(vm.stackTop[-1])) {								\
			runtimeError("Operands must be numbers.");											\
			return INTERPRET_RUNTIME_ERROR;																\
		}																								\
        /* Perform the operation and push the result back */											\
		vm.stackTop[-1] = valueType(fmod(AS_NUMBER(vm.stackTop[-1]),AS_NUMBER(vm.stackTop[0])));	\
	} while (false)

	while (true) //let it loop
	{
#if LOG_KIPS
		++byteCodeCount;
#endif

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

		switch (instruction)
		{
		case OP_CONSTANT: {
			uint32_t index = READ_SHORT();
			Value constant = READ_CONSTANT(index);
			stack_push(constant);
			break;
		}
		case OP_CONSTANT_LONG: {
			uint32_t index = READ_24bits();
			Value constant = READ_CONSTANT(index);
			stack_push(constant);
			break;
		}
		case OP_CLOSURE: {
			uint32_t index = READ_SHORT();
			Value constant = READ_CONSTANT(index);
			ObjFunction* function = AS_FUNCTION(constant);
			ObjClosure* closure = newClosure(function);
			stack_push(OBJ_VAL(closure));

			for (int32_t i = 0; i < closure->upvalueCount; i++) {
				uint8_t isLocal = READ_BYTE();
				uint16_t index = READ_SHORT();
				if (isLocal) {
					closure->upvalues[i] = captureUpvalue(frame->slots + index);
				}
				else {
					closure->upvalues[i] = frame->closure->upvalues[index];
				}
			}
			break;
		}
		case OP_CLOSURE_LONG: {
			uint32_t index = READ_24bits();
			Value constant = READ_CONSTANT(index);
			ObjFunction* function = AS_FUNCTION(constant);
			ObjClosure* closure = newClosure(function);
			stack_push(OBJ_VAL(closure));

			for (int32_t i = 0; i < closure->upvalueCount; i++) {
				uint8_t isLocal = READ_BYTE();
				uint16_t index = READ_SHORT();
				if (isLocal) {
					closure->upvalues[i] = captureUpvalue(frame->slots + index);
				}
				else {
					closure->upvalues[i] = frame->closure->upvalues[index];
				}
			}
			break;
		}
		case OP_DEFINE_GLOBAL: {
			Value constant = READ_CONSTANT(READ_SHORT());
			ObjString* name = AS_STRING(constant);
			--vm.stackTop;
      
			tableSet_g(&vm.globals, name, *vm.stackTop);
			break;
		}
		case OP_DEFINE_GLOBAL_LONG: {
			Value constant = READ_CONSTANT(READ_24bits());
			ObjString* name = AS_STRING(constant);
			--vm.stackTop;

			tableSet_g(&vm.globals, name, *vm.stackTop);
			break;
		}
		case OP_GET_GLOBAL: {
			Value constant = READ_CONSTANT(READ_SHORT());
			ObjString* name = AS_STRING(constant);
			Value value;

			if (!tableGet_g(&vm.globals, name, &value)) {
				runtimeError("Undefined variable '%s'.", name->chars);
				return INTERPRET_RUNTIME_ERROR;
			}
			stack_push(value);
			break;
		}
		case OP_GET_GLOBAL_LONG: {
			Value constant = READ_CONSTANT(READ_24bits());
			ObjString* name = AS_STRING(constant);
			Value value;

			if (!tableGet_g(&vm.globals, name, &value)) {
				runtimeError("Undefined variable '%s'.", name->chars);
				return INTERPRET_RUNTIME_ERROR;
			}
			stack_push(value);
			break;
		}
		case OP_SET_GLOBAL: {
			Value constant = READ_CONSTANT(READ_SHORT());
			ObjString* name = AS_STRING(constant);

			if (tableSet_g(&vm.globals, name, vm.stackTop[-1])) {
				//lox dont allow setting undefined one
				tableDelete_g(&vm.globals, name);
				runtimeError("Undefined variable '%s'.", name->chars);
				return INTERPRET_RUNTIME_ERROR;
			}
			break;
		}
		case OP_SET_GLOBAL_LONG: {
			Value constant = READ_CONSTANT(READ_24bits());
			ObjString* name = AS_STRING(constant);

			if (tableSet_g(&vm.globals, name, vm.stackTop[-1])) {
				//lox dont allow setting undefined one
				tableDelete_g(&vm.globals, name);
				runtimeError("Undefined variable '%s'.", name->chars);
				return INTERPRET_RUNTIME_ERROR;
			}
			break;
		}
		case OP_GET_UPVALUE: {
			uint8_t slot = READ_BYTE();
			stack_push(*frame->closure->upvalues[slot]->location);
			break;
		}
		case OP_SET_UPVALUE: {
			uint8_t slot = READ_BYTE();
			*frame->closure->upvalues[slot]->location = vm.stackTop[-1];
			break;
		}
		case OP_NIL: stack_push(NIL_VAL); break;
		case OP_TRUE: stack_push(BOOL_VAL(true)); break;
		case OP_FALSE: stack_push(BOOL_VAL(false)); break;
		case OP_EQUAL: {
			vm.stackTop--;
			vm.stackTop[-1] = BOOL_VAL(valuesEqual(vm.stackTop[-1], vm.stackTop[0]));
			break;
		}
		case OP_NOT_EQUAL: {
			vm.stackTop--;
			vm.stackTop[-1] = BOOL_VAL(!valuesEqual(vm.stackTop[-1], vm.stackTop[0]));
			break;
		}
		case OP_GREATER:  BINARY_OP(BOOL_VAL, > ); break;
		case OP_LESS:     BINARY_OP(BOOL_VAL, < ); break;
		case OP_GREATER_EQUAL:  BINARY_OP(BOOL_VAL, >= ); break;
		case OP_LESS_EQUAL:     BINARY_OP(BOOL_VAL, <= ); break;

		case OP_ADD: {
			// might cause gc,so can't decrease first
			if (IS_STRING(vm.stackTop[-2]) && IS_STRING(vm.stackTop[-1])) {
				ObjString* result = connectString(AS_STRING(vm.stackTop[-2]), AS_STRING(vm.stackTop[-1]));
				vm.stackTop[-2] = OBJ_VAL(result);
				vm.stackTop--;
				break;
			}
			else if (IS_NUMBER(vm.stackTop[-2]) && IS_NUMBER(vm.stackTop[-1])) {
				vm.stackTop[-2] = NUMBER_VAL(AS_NUMBER(vm.stackTop[-2]) + AS_NUMBER(vm.stackTop[-1]));
				vm.stackTop--;
				break;
			}
			else {
				runtimeError("Operands must be two numbers or two strings.");
				return INTERPRET_RUNTIME_ERROR;
			}
		}
		case OP_SUBTRACT: BINARY_OP(NUMBER_VAL, -); break;
		case OP_MULTIPLY: BINARY_OP(NUMBER_VAL, *); break;
		case OP_DIVIDE:   BINARY_OP(NUMBER_VAL, / ); break;
		case OP_MODULUS:  BINARY_OP_MODULUS(NUMBER_VAL); break;

		case OP_NOT: {
			vm.stackTop[-1] = BOOL_VAL(isFalsey(vm.stackTop[-1]));
			break;
		}
		case OP_NEGATE: {
			if (!IS_NUMBER(vm.stackTop[-1])) {
				runtimeError("Operand must be a number.");
				return INTERPRET_RUNTIME_ERROR;
			}
			AS_NUMBER(vm.stackTop[-1]) = -AS_NUMBER(vm.stackTop[-1]);
			break;
		}
		case OP_PRINT: {
#if DEBUG_MODE
			printf("[print] ");
#endif
			printValue(stack_pop());
			printf("\n");
			break;
		}
		case OP_THROW: {
			//if solved break else error
			if (throwError(stack_pop())) {
				break;
			}
			return INTERPRET_RUNTIME_ERROR;
		}
		case OP_GET_LOCAL: {
			uint32_t index = READ_SHORT();
			stack_push(frame->slots[index]);
			break;
		}
		case OP_SET_LOCAL: {
			uint32_t index = READ_SHORT();
			frame->slots[index] = vm.stackTop[-1];
			break;
		}
		case OP_CLOSE_UPVALUE:
			closeUpvalues(vm.stackTop - 1);
			stack_pop();
			break;
		case OP_POP: {
			stack_pop();
			break;
		}
		case OP_POP_N: {
			uint32_t index = READ_SHORT();
			vm.stackTop -= index;
			break;
		}
		case OP_JUMP: {
			uint16_t offset = READ_SHORT();
			ip += offset;
			break;
		}
		case OP_LOOP: {
			uint16_t offset = READ_SHORT();
			ip -= offset;
			break;
		}
		case OP_JUMP_IF_FALSE: {
			uint16_t offset = READ_SHORT();
			if (isFalsey(vm.stackTop[-1])) ip += offset;
			break;
		}
		case OP_JUMP_IF_FALSE_POP: {
			uint16_t offset = READ_SHORT();
			if (isFalsey(vm.stackTop[-1])) ip += offset;
			--vm.stackTop;
			break;
		}
		case OP_JUMP_IF_TRUE: {
			uint16_t offset = READ_SHORT();
			if (isTruthy(vm.stackTop[-1])) ip += offset;
			break;
		}
		case OP_CALL: {
			uint8_t argCount = READ_BYTE();
			frame->ip = ip;//change before call
			if (!callValue(STACK_PEEK(argCount), argCount)) {
				return INTERPRET_RUNTIME_ERROR;
			}
			//we entered the function
			frame = &vm.frames[vm.frameCount - 1];
			ip = frame->ip;//restore after call
			break;
		}
		case OP_RETURN: {
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
			break;
		}

		case OP_MODULE_GLOBAL:
			//Not implemented
			stack_push(NUMBER_VAL(OP_MODULE_GLOBAL));
			break;
		case OP_MODULE_BUILTIN:
			stack_push(NUMBER_VAL(READ_BYTE()));
			break;
		case OP_DEBUGGER: {
			//no op
#if DEBUG_MODE

#endif
			break;
		}
		}
	}

	//the place the error happens
#undef READ_BYTE
#undef READ_SHORT
#undef READ_24bits
#undef READ_CONSTANT
#undef BINARY_OP
#undef BINARY_OP_MODULUS
}

InterpretResult interpret(C_STR source)
{
	ObjFunction* function = compile(source);
	if (function == NULL) return INTERPRET_COMPILE_ERROR;

	stack_push(OBJ_VAL(function));
	ObjClosure* closure = newClosure(function);
	stack_replace(OBJ_VAL(closure));
	call(closure, 0);

#if LOG_EXECUTE_TIMING
	uint64_t time_run = get_nanoseconds();
#endif

#if LOG_KIPS
	byteCodeCount = 0;
#endif

	InterpretResult result = run();

#if LOG_COMPILE_TIMING
	double time_ms = (get_nanoseconds() - time_run) * 1e-6;
	printf("[Log] Finished executing in %g ms.\n", time_ms);
#endif

#if LOG_KIPS
	printf("[Log] Finished executing at %g kips.\n", byteCodeCount / time_ms);
	byteCodeCount = 0;
#endif

	return result;
}

InterpretResult interpret_repl(C_STR source) {
	InterpretResult res = interpret(source);
	stack_reset();//clean stack after use
	return res;
}