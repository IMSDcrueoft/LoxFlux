/*
 * MIT License
 * Copyright (c) 2025 IMSDcrueoft (https://github.com/IMSDcrueoft)
 * See LICENSE file in the root directory for full license text.
*/
#include "debug.h"
#if  DEBUG_TRACE_EXECUTION || DEBUG_PRINT_CODE
#include "nativeBuiltin.h"
#include "vm.h"

COLD_FUNCTION
static uint32_t simpleInstruction(C_STR name, uint32_t offset) {
	printf("%s\n", name);
	//OP_RETURN 1
	return offset + 1;
}

COLD_FUNCTION
static uint32_t builtinStruction(C_STR name, Chunk* chunk, uint32_t offset) {
	uint32_t slot = chunk->code[offset + 1];
	switch (slot)
	{
	case MODULE_MATH:
		printf("%-16s %-10s\n", name, "@math");
		break;
	case MODULE_ARRAY:
		printf("%-16s %-10s\n", name, "@array");
		break;
	case MODULE_OBJECT:
		printf("%-16s %-10s\n", name, "@object");
		break;
	case MODULE_STRING:
		printf("%-16s %-10s\n", name, "@string");
		break;
	case MODULE_TIME:
		printf("%-16s %-10s\n", name, "@time");
		break;
	case MODULE_FILE:
		printf("%-16s %-10s\n", name, "@file");
		break;
	case MODULE_SYSTEM:
		printf("%-16s %-10s\n", name, "@system");
		break;
	}
	return offset + 2;
}

COLD_FUNCTION
static uint32_t byteInstruction(C_STR name, Chunk* chunk, uint32_t offset) {
	uint32_t slot = chunk->code[offset + 1];
	printf("%-16s %4d\n", name, slot);
	return offset + 2;
}

COLD_FUNCTION
static uint32_t jumpInstruction(C_STR name, int32_t sign, Chunk* chunk, uint32_t offset) {
	uint16_t jump = (uint16_t)chunk->code[offset + 1];
	jump |= (chunk->code[offset + 2] << 8);

	printf("%-16s %4d -> %d\n", name, offset, offset + 3 + sign * jump);
	return offset + 3;
}

COLD_FUNCTION
static uint32_t modifyLocalInstruction(C_STR name, Chunk* chunk, uint32_t offset) {
	uint32_t slot = ((uint32_t)chunk->code[offset + 1]) | ((uint32_t)chunk->code[offset + 2] << 8);
	printf("%-16s %4d\n", name, slot);
	return offset + 3;
}

COLD_FUNCTION
static uint32_t modifyGlobalInstruction(C_STR name, Chunk* chunk, uint32_t offset) {
	uint32_t constant = ((uint32_t)chunk->code[offset + 1]) | ((uint32_t)chunk->code[offset + 2] << 8);
	printf("%-16s %4d '", name, constant);
	printValue(vm.constants.values[constant]);
	printf("'\n");
	return offset + 3;
}

COLD_FUNCTION
static uint32_t modifyGlobalLongInstruction(C_STR name, Chunk* chunk, uint32_t offset) {
	uint32_t constant = ((uint32_t)chunk->code[offset + 1]) | ((uint32_t)chunk->code[offset + 2] << 8) | ((uint32_t)chunk->code[offset + 3] << 16);
	printf("%-16s %4d '", name, constant);
	printValue(vm.constants.values[constant]);
	printf("'\n");
	return offset + 4;
}

COLD_FUNCTION
static uint32_t constantInstruction(C_STR name, Chunk* chunk, uint32_t offset) {
	//16bit index
	uint16_t constant = ((uint32_t)chunk->code[offset + 1]) | ((uint32_t)chunk->code[offset + 2] << 8);

	printf("%-16s %4d '", name, constant);
	printValue(vm.constants.values[constant]);
	printf("'\n");

	//OP_CONSTANT_SHORT 3
	return offset + 3;
}

COLD_FUNCTION
static uint32_t constantInstruction_long(C_STR name, Chunk* chunk, uint32_t offset) {
	//24bit index
	uint32_t constant = ((uint32_t)chunk->code[offset + 1]) | ((uint32_t)chunk->code[offset + 2] << 8) | ((uint32_t)chunk->code[offset + 3] << 16);

	printf("%-16s %4d '", name, constant);
	printValue(vm.constants.values[constant]);
	printf("'\n");

	//OP_CONSTANT_LONG 4
	return offset + 4;
}

COLD_FUNCTION
uint32_t disassembleInstruction(Chunk* chunk, uint32_t offset) {
	printf("%04d ", offset);

	if (offset > 0 && getLine(&chunk->lines, offset) == getLine(&chunk->lines, offset - 1)) {
		printf("   | ");//means it's the same line
	}
	else {
		printf("%4d ", getLine(&chunk->lines, offset));
	}

	uint8_t instruction = chunk->code[offset];

	switch (instruction) {
	case OP_CALL:
		return byteInstruction("OP_CALL", chunk, offset);
	case OP_RETURN:
		return simpleInstruction("OP_RETURN", offset);
	case OP_THROW:
		return simpleInstruction("OP_THROW", offset);
	case OP_POP:
		return simpleInstruction("OP_POP", offset);
	case OP_PRINT:
		return simpleInstruction("OP_PRINT", offset);
	case OP_ADD:
		return simpleInstruction("OP_ADD", offset);
	case OP_SUBTRACT:
		return simpleInstruction("OP_SUBTRACT", offset);
	case OP_MULTIPLY:
		return simpleInstruction("OP_MULTIPLY", offset);
	case OP_DIVIDE:
		return simpleInstruction("OP_DIVIDE", offset);
	case OP_MODULUS:
		return simpleInstruction("OP_MODULUS", offset);
	case OP_NEGATE:
		return simpleInstruction("OP_NEGATE", offset);

	case OP_CONSTANT:
		return constantInstruction("OP_CONSTANT", chunk, offset);
	case OP_CONSTANT_LONG:
		return constantInstruction_long("OP_CONSTANT_LONG", chunk, offset);

	case OP_CLOSURE:{
		uint16_t constant = ((uint32_t)chunk->code[offset + 1]) | ((uint32_t)chunk->code[offset + 2] << 8);

		printf("%-16s %4d '", "OP_CLOSURE", constant);
		printValue(vm.constants.values[constant]);
		printf("'\n");

		//OP_CONSTANT_SHORT 3
		offset += 3;

		ObjFunction* function = AS_FUNCTION(vm.constants.values[constant]);
		for (int32_t j = 0; j < function->upvalueCount; j++) {
			int32_t isLocal = chunk->code[offset++];
			uint16_t index = chunk->code[offset++];
			index |= (chunk->code[offset++] << 8);

			printf("%04d      |                     %s %d\n",
				offset - 3, isLocal ? "local" : "upvalue", index);
		}
		return offset;
	}
	case OP_CLOSURE_LONG:{
		//24bit index
		uint32_t constant = ((uint32_t)chunk->code[offset + 1]) | ((uint32_t)chunk->code[offset + 2] << 8) | ((uint32_t)chunk->code[offset + 3] << 16);

		printf("%-16s %4d '", "OP_CLOSURE_LONG", constant);
		printValue(vm.constants.values[constant]);
		printf("'\n");

		//OP_CONSTANT_LONG 4
		offset += 4;

		ObjFunction* function = AS_FUNCTION(vm.constants.values[constant]);
		for (int32_t j = 0; j < function->upvalueCount; j++) {
			int32_t isLocal = chunk->code[offset++];
			uint16_t index = chunk->code[offset++];
			index |= (chunk->code[offset++] << 8);

			printf("%04d      |                     %s %d\n",
				offset - 3, isLocal ? "local" : "upvalue", index);
		}
		return offset;
	}
	case OP_GET_UPVALUE:
		return byteInstruction("OP_GET_UPVALUE", chunk, offset);
	case OP_SET_UPVALUE:
		return byteInstruction("OP_SET_UPVALUE", chunk, offset);
	case OP_CLOSE_UPVALUE:
		return simpleInstruction("OP_CLOSE_UPVALUE", offset);
	case OP_NIL:
		return simpleInstruction("OP_NIL", offset);
	case OP_TRUE:
		return simpleInstruction("OP_TRUE", offset);
	case OP_FALSE:
		return simpleInstruction("OP_FALSE", offset);
	case OP_NOT:
		return simpleInstruction("OP_NOT", offset);
	case OP_EQUAL:
		return simpleInstruction("OP_EQUAL", offset);
	case OP_GREATER:
		return simpleInstruction("OP_GREATER", offset);
	case OP_LESS:
		return simpleInstruction("OP_LESS", offset);
	case OP_NOT_EQUAL:
		return simpleInstruction("OP_NOT_EQUAL", offset);
	case OP_GREATER_EQUAL:
		return simpleInstruction("OP_GREATER_EQUAL", offset);
	case OP_LESS_EQUAL:
		return simpleInstruction("OP_LESS_EQUAL", offset);

	case OP_DEFINE_GLOBAL:
		return modifyGlobalInstruction("OP_DEFINE_GLOBAL", chunk, offset);
	case OP_DEFINE_GLOBAL_LONG:
		return modifyGlobalLongInstruction("OP_DEFINE_GLOBAL_LONG", chunk, offset);
	case OP_GET_GLOBAL:
		return modifyGlobalInstruction("OP_GET_GLOBAL", chunk, offset);
	case OP_GET_GLOBAL_LONG:
		return modifyGlobalLongInstruction("OP_GET_GLOBAL_LONG", chunk, offset);
	case OP_SET_GLOBAL:
		return modifyGlobalInstruction("OP_SET_GLOBAL", chunk, offset);
	case OP_SET_GLOBAL_LONG:
		return modifyGlobalLongInstruction("OP_SET_GLOBAL_LONG", chunk, offset);
	case OP_CLASS:
		return constantInstruction("OP_CLASS", chunk, offset);
	case OP_CLASS_LONG:
		return constantInstruction_long("OP_CLASS_LONG", chunk, offset);

	case OP_GET_PROPERTY:
		return constantInstruction("OP_GET_PROPERTY", chunk, offset);
	case OP_GET_PROPERTY_LONG:
		return constantInstruction("OP_GET_PROPERTY_LONG", chunk, offset);
	case OP_SET_PROPERTY:
		return constantInstruction("OP_SET_PROPERTY", chunk, offset);
	case OP_SET_PROPERTY_LONG:
		return constantInstruction("OP_SET_PROPERTY_LONG", chunk, offset);

	case OP_GET_SUBSCRIPT:
		return simpleInstruction("OP_GET_SUBSCRIPT", offset);
	case OP_SET_SUBSCRIPT:
		return simpleInstruction("OP_SET_SUBSCRIPT", offset);
	case OP_NEW_ARRAY:
		return byteInstruction("OP_NEW_ARRAY", chunk, offset);

	case OP_GET_LOCAL:
		return modifyLocalInstruction("OP_GET_LOCAL", chunk, offset);
	case OP_SET_LOCAL:
		return modifyLocalInstruction("OP_SET_LOCAL", chunk, offset);
	case OP_POP_N:
		return modifyLocalInstruction("OP_POP_N", chunk, offset);

	case OP_JUMP:
		return jumpInstruction("OP_JUMP", 1, chunk, offset);
	case OP_JUMP_IF_FALSE:
	case OP_JUMP_IF_FALSE_POP:
		return jumpInstruction("OP_JUMP_IF_FALSE", 1, chunk, offset);
	case OP_JUMP_IF_TRUE:
		return jumpInstruction("OP_JUMP_IF_TRUE", 1, chunk, offset);
	case OP_LOOP:
		return jumpInstruction("OP_LOOP", -1, chunk, offset);

	case OP_MODULE_GLOBAL:
		return simpleInstruction("OP_MODULE_GLOBAL", offset);
	case OP_MODULE_BUILTIN:
		return builtinStruction("OP_MODULE", chunk, offset);
	default:
		printf("Unknown opcode %d offset = %d\n", instruction, offset);
		return offset + 1;
	}
}

COLD_FUNCTION
void disassembleChunk(Chunk* chunk, C_STR name) {
	printf("== %s ==\n", name);

	uint32_t offset = 0;
	while (offset < chunk->count) {
		offset = disassembleInstruction(chunk, offset);
	}

	printf("== %s end ==\n", name);
}
#endif