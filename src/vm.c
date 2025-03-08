#include "vm.h"
#include "object.h"
#include "builtinModule.h"

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

#if LOG_BIPMS
uint64_t byteCodeCount;
#endif

static void stack_reset()
{
	//reset the pointer
	vm.stackTop = vm.stack;
}

static void runtimeError(C_STR format, ...) {
	va_list args;
	va_start(args, format);
	vfprintf(stderr, format, args);
	va_end(args);
	fputs("\n", stderr);

	size_t instruction = vm.ip - vm.chunk->code - 1;
	uint32_t line = getLine(&vm.chunk->lines, (uint32_t)instruction);
	fprintf(stderr, "[line %d] in script\n", line);

	stack_reset();
}

void vm_init()
{
	vm.stack = NULL;
	vm.stackTop = NULL;
	vm.stackBoundary = NULL;

	vm.stack = (Value*)reallocate(NULL, 0, sizeof(Value) * (STACK_MAX));
	vm.stackBoundary = vm.stack + STACK_MAX;

	stack_reset();

	table_init(&vm.globals);
	vm.globals.type = TABLE_GLOBAL;//remind this
	table_init(&vm.strings);
	vm.strings.type = TABLE_NORMAL;
	numberTable_init(&vm.numbers);

	vm.objects = NULL;
}

void vm_free()
{
	table_free(&vm.globals);
	table_free(&vm.strings);
	numberTable_free(&vm.numbers);

	freeObjects();

	//realease the stack
	FREE_ARRAY(Value, vm.stack, vm.stackBoundary - vm.stack);
	vm.stack = NULL;
	vm.stackTop = NULL;
	vm.stackBoundary = NULL;
}

static inline void stack_push(Value value)
{
	*vm.stackTop = value;

	if (++vm.stackTop == vm.stackBoundary) {
		uint32_t oldCapacity = (uint32_t)(vm.stackBoundary - vm.stack);
		uint32_t capacity = GROW_CAPACITY(oldCapacity);

		if (capacity > UINT24_COUNT) {
			runtimeError("Stack overflow.");
			return;
		}

		vm.stack = (Value*)reallocate(vm.stack, sizeof(Value) * (oldCapacity), sizeof(Value) * (capacity));
		vm.stackBoundary = vm.stack + capacity;		//need fresh
		vm.stackTop = vm.stack + oldCapacity;		//need fresh
	}
}

static inline Value stack_pop()
{
	vm.stackTop--;
	return *vm.stackTop;
}

static inline Value stack_peek(uint32_t distance) {
	return vm.stackTop[-1 - distance];
}

static inline bool isFalsey(Value value) {
	return IS_NIL(value) || (IS_BOOL(value) && !AS_BOOL(value));
}

static inline bool isTruthy(Value value) {
	return !IS_NIL(value) && (!IS_BOOL(value) || AS_BOOL(value));
}

//for op_const
static const Value imm[] = {
	{.type = VAL_NUMBER,.as.number = 0 },
	{.type = VAL_NUMBER,.as.number = 1 },
	{.type = VAL_NUMBER,.as.number = 2 },
	{.type = VAL_NUMBER,.as.number = 10 }
};

//to run code in vm
static InterpretResult run()
{
#define READ_BYTE() (*(vm.ip++))
#define READ_SHORT() (vm.ip += 2, (uint16_t)(vm.ip[-2] | (vm.ip[-1] << 8)))
#define READ_24bits() (vm.ip += 3, (uint32_t)(vm.ip[-3] | (vm.ip[-2] << 8) | (vm.ip[-1] << 16)))

#if !DEBUG_MODE
#define READ_CONSTANT(index) (vm.chunk->constants.values[(index)])
#else
#define READ_CONSTANT(index) (vm.chunk->constants.values[(index)])
#endif

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
#if LOG_BIPMS
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
		disassembleInstruction(vm.chunk, (uint32_t)(vm.ip - vm.chunk->code));
#endif // DEBUG_TRACE_EXECUTION

		uint8_t instruction = READ_BYTE();
		uint8_t high2bit = instruction >> 6;
		uint32_t index;
		Value constant;

		switch (instruction & 0b00111111)
		{
		case OP_CONSTANT: {
			index = READ_BYTE() | (high2bit << 8);
			constant = READ_CONSTANT(index);
			stack_push(constant);
			break;
		}
		case OP_CONSTANT_SHORT: {
			index = READ_SHORT() | (high2bit << 16);
			constant = READ_CONSTANT(index);
			stack_push(constant);
			break;
		}
		case OP_CONSTANT_LONG: {
			index = READ_24bits();
			constant = READ_CONSTANT(index);
			stack_push(constant);
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
			vm.stackTop--;
			if (IS_STRING(vm.stackTop[0]) && IS_STRING(vm.stackTop[-1])) {
				ObjString* result = connectString(AS_STRING(vm.stackTop[-1]), AS_STRING(vm.stackTop[0]));
				vm.stackTop[-1] = OBJ_VAL(result);
				break;
			}
			else if (IS_NUMBER(vm.stackTop[0]) && IS_NUMBER(vm.stackTop[-1])) {
				vm.stackTop[-1] = NUMBER_VAL(AS_NUMBER(vm.stackTop[-1]) + AS_NUMBER(vm.stackTop[0]));
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
			printValue(stack_pop());
			printf("\n");
			break;
		}
		case OP_DEFINE_GLOBAL: {
			switch (high2bit)
			{
			case 0: constant = READ_CONSTANT(READ_BYTE()); break;
			case 1: constant = READ_CONSTANT(READ_SHORT()); break;
			case 2: constant = READ_CONSTANT(READ_24bits()); break;
			}
			ObjString* name = AS_STRING(constant);
			--vm.stackTop;
			tableSet(&vm.globals, name, *vm.stackTop);
			break;
		}
		case OP_GET_GLOBAL: {
			switch (high2bit)
			{
			case 0: constant = READ_CONSTANT(READ_BYTE()); break;
			case 1: constant = READ_CONSTANT(READ_SHORT()); break;
			case 2: constant = READ_CONSTANT(READ_24bits()); break;
			}

			ObjString* name = AS_STRING(constant);
			Value value;
			if (!tableGet(&vm.globals, name, &value)) {
				runtimeError("Undefined variable '%s'.", name->chars);
				return INTERPRET_RUNTIME_ERROR;
			}
			stack_push(value);
			break;
		}
		case OP_SET_GLOBAL: {
			switch (high2bit)
			{
			case 0: constant = READ_CONSTANT(READ_BYTE()); break;
			case 1: constant = READ_CONSTANT(READ_SHORT()); break;
			case 2: constant = READ_CONSTANT(READ_24bits()); break;
			}

			ObjString* name = AS_STRING(constant);
			if (tableSet(&vm.globals, name, vm.stackTop[-1])) {
				//lox dont allow setting undefined one
				tableDelete(&vm.globals, name);
				runtimeError("Undefined variable '%s'.", name->chars);
				return INTERPRET_RUNTIME_ERROR;
			}
			break;
		}

		case OP_GET_LOCAL: {
			index = READ_BYTE() | (high2bit << 8);
			stack_push(vm.stack[index]);
			break;
		}
		case OP_SET_LOCAL: {
			index = READ_BYTE() | (high2bit << 8);
			vm.stack[index] = vm.stackTop[-1];
			break;
		}
		case OP_POP: {
			stack_pop();
			break;
		}
		case OP_POP_N: {
			index = (READ_BYTE() | (high2bit << 8)) + 1;
			vm.stackTop -= index;
			break;
		}
		case OP_JUMP: {
			uint16_t offset = READ_SHORT();
			vm.ip += offset;
			break;
		}
		case OP_LOOP: {
			uint16_t offset = READ_SHORT();
			vm.ip -= offset;
			break;
		}
		case OP_JUMP_IF_FALSE: {
			uint16_t offset = READ_SHORT();
			if (isFalsey(vm.stackTop[-1])) vm.ip += offset;
			vm.stackTop -= high2bit;
			break;
		}
		case OP_JUMP_IF_TRUE: {
			uint16_t offset = READ_SHORT();
			if (isTruthy(vm.stackTop[-1])) vm.ip += offset;
			vm.stackTop -= high2bit;
			break;
		}
		case OP_RETURN: {
			// Exit interpreter.
			return INTERPRET_OK;
		}

		case OP_IMM: stack_push(imm[high2bit]); break;

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
	Chunk chunk;
	chuck_init(&chunk);

	if (!compile(source, &chunk)) {
		chunk_free(&chunk);
		return INTERPRET_COMPILE_ERROR;
	}

	vm.chunk = &chunk;
	vm.ip = vm.chunk->code;

#if LOG_EXECUTE_TIMING
	uint64_t time_run = get_nanoseconds();
#endif

#if LOG_BIPMS
	byteCodeCount = 0;
#endif

	InterpretResult result = run();

#if LOG_COMPILE_TIMING
	double time_ms = (get_nanoseconds() - time_run) * 1e-6;
	printf("Log: Finished executing in %g ms.\n", time_ms);
#endif

#if LOG_BIPMS
	printf("Log: Finished executing at %g bi/ms.\n", byteCodeCount / time_ms);
	byteCodeCount = 0;
#endif
	//we free it first
	chunk_free(&chunk);
	return result;
}

InterpretResult require(C_STR source, Chunk* chunk) {
	//remind this,when we compile,the ip might changed
	uint32_t ipOffset = 0;

	if (vm.chunk == chunk) {
		ipOffset = vm.ip - vm.chunk->code;
	}

	if (!compile(source, chunk)) {
		chunk_free_errorCode(chunk, ipOffset);

		vm.chunk = chunk;
		vm.ip = vm.chunk->code + ipOffset;

		return INTERPRET_COMPILE_ERROR;
	}

	if (vm.chunk == NULL) {
		vm.chunk = chunk;
		vm.ip = vm.chunk->code;
	}
	else {
		vm.chunk = chunk;
		vm.ip = vm.chunk->code + ipOffset;
	}

	return INTERPRET_OK;
}

InterpretResult eval(C_STR source, Chunk* chunk)
{
	if (require(source, chunk) != INTERPRET_OK) {
		return INTERPRET_COMPILE_ERROR;
	}

#if LOG_EXECUTE_TIMING
	uint64_t time_run = get_nanoseconds();
#endif

#if LOG_BIPMS
	byteCodeCount = 0;
#endif

	//don't reset the ip
	InterpretResult result = run();

#if LOG_COMPILE_TIMING
	double time_ms = (get_nanoseconds() - time_run) * 1e-6;
	printf("Log: Finished executing in %g ms.\n", time_ms);
#endif

#if LOG_BIPMS
	printf("Log: Finished executing at %g bi/ms.\n", byteCodeCount / time_ms);
	byteCodeCount = 0;
#endif
	return result;
}