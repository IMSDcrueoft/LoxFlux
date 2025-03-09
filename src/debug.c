/*
 * MIT License
 * Copyright (c) 2025 IM&SD (https://github.com/IMSDcrueoft)
 * See LICENSE file in the root directory for full license text.
*/
#include "debug.h"
#include "builtinModule.h"

#if  DEBUG_TRACE_EXECUTION

static uint32_t simpleInstruction(C_STR name, uint32_t offset) {
	printf("%s\n", name);
	//OP_RETURN 有1字节
	return offset + 1;
}

static uint32_t immidiateIstruction(C_STR name, uint32_t offset, uint32_t high2bit) {
	printf("%-16s %4d\n", name, (high2bit == 0b11) ? 10 : high2bit);
	//OP_RETURN 有1字节
	return offset + 1;
}

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

static uint32_t byteInstruction(C_STR name, Chunk* chunk, uint32_t offset) {
	uint32_t slot = chunk->code[offset + 1];
	printf("%-16s %4d\n", name, slot);
	return offset + 2;
}

static uint32_t jumpInstruction(C_STR name, int32_t sign, Chunk* chunk, uint32_t offset, uint32_t high2bit) {
	uint16_t jump = (uint16_t)chunk->code[offset + 1];
	jump |= (chunk->code[offset + 2] << 8);

	printf("%-16s %4d -> %d  %-8s\n", name, offset, offset + 3 + sign * jump, high2bit ? "POP" : "");
	return offset + 3;
}

static uint32_t modifyLocalInstruction(C_STR name, Chunk* chunk, uint32_t offset, uint32_t high2bit) {
	uint32_t slot = chunk->code[offset + 1];
	slot |= (high2bit << 8);
	printf("%-16s %4d\n", name, slot + 1);
	return offset + 2;
}

static uint32_t modifyGlobalInstruction(C_STR name, Chunk* chunk, uint32_t offset) {
	uint32_t constant;
	uint32_t high2bit = chunk->code[offset] >> 6;

	switch (high2bit)
	{
	case 0:
		constant = chunk->code[offset + 1];
		printf("%-16s %4d '", name, constant);
		printValue(chunk->constants.values[constant]);
		printf("'\n");
		return offset + 2;
	case 1:
		constant = ((uint32_t)chunk->code[offset + 1]) | ((uint32_t)chunk->code[offset + 2] << 8);
		printf("%-16s %4d '", name, constant);
		printValue(chunk->constants.values[constant]);
		printf("'\n");
		return offset + 3;
	case 2:
		constant = ((uint32_t)chunk->code[offset + 1]) | ((uint32_t)chunk->code[offset + 2] << 8) | ((uint32_t)chunk->code[offset + 3] << 16);
		printf("%-16s %4d '", name, constant);
		printValue(chunk->constants.values[constant]);
		printf("'\n");
		return offset + 4;
	default:
		return offset + 1;
	}
}

static uint32_t constantInstruction(C_STR name, Chunk* chunk, uint32_t offset, uint32_t high2bit) {
	uint32_t constant = chunk->code[offset + 1] | (high2bit << 8);
	printf("%-16s %4d '", name, constant);
	printValue(chunk->constants.values[constant]);
	printf("'\n");

	//OP_CONSTANT 有2字节
	return offset + 2;
}

static uint32_t constantInstruction_short(C_STR name, Chunk* chunk, uint32_t offset, uint32_t high2bit) {
	//合18bit index
	uint16_t constant = ((uint32_t)chunk->code[offset + 1]) | ((uint32_t)chunk->code[offset + 2] << 8) | (high2bit << 16);

	printf("%-16s %4d '", name, constant);
	printValue(chunk->constants.values[constant]);
	printf("'\n");

	//OP_CONSTANT_SHORT 有3字节
	return offset + 3;
}

static uint32_t constantInstruction_long(C_STR name, Chunk* chunk, uint32_t offset) {
	//合24bit index
	uint32_t constant = ((uint32_t)chunk->code[offset + 1]) | ((uint32_t)chunk->code[offset + 2] << 8) | ((uint32_t)chunk->code[offset + 3] << 16);

	printf("%-16s %4d '", name, constant);
	printValue(chunk->constants.values[constant]);
	printf("'\n");

	//OP_CONSTANT_LONG 有4字节
	return offset + 4;
}

uint32_t disassembleInstruction(Chunk* chunk, uint32_t offset) {
	printf("%04d ", offset);

	if (offset > 0 && getLine(&chunk->lines, offset) == getLine(&chunk->lines, offset - 1)) {
		printf("   | ");//means it's the same line
	}
	else {
		printf("%4d ", getLine(&chunk->lines, offset));
	}

	uint8_t instruction = chunk->code[offset];
	uint8_t high2bit = instruction >> 6;

	switch (instruction & 0b00111111) {
	case OP_RETURN:
		return simpleInstruction("OP_RETURN", offset);
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
		return constantInstruction("OP_CONSTANT", chunk, offset, high2bit);
	case OP_CONSTANT_SHORT:
		return constantInstruction_short("OP_CONSTANT_SHORT", chunk, offset, high2bit);
	case OP_CONSTANT_LONG:
		return constantInstruction_long("OP_CONSTANT_LONG", chunk, offset);
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
	case OP_GET_GLOBAL:
		return modifyGlobalInstruction("OP_GET_GLOBAL", chunk, offset);
	case OP_SET_GLOBAL:
		return modifyGlobalInstruction("OP_SET_GLOBAL", chunk, offset);

	case OP_GET_LOCAL:
		return modifyLocalInstruction("OP_GET_LOCAL", chunk, offset, high2bit);
	case OP_SET_LOCAL:
		return modifyLocalInstruction("OP_SET_LOCAL", chunk, offset, high2bit);
	case OP_POP_N:
		return modifyLocalInstruction("OP_POP_N", chunk, offset, high2bit);

	case OP_JUMP:
		return jumpInstruction("OP_JUMP", 1, chunk, offset, high2bit);
	case OP_JUMP_IF_FALSE:
		return jumpInstruction("OP_JUMP_IF_FALSE", 1, chunk, offset, high2bit);
	case OP_JUMP_IF_TRUE:
		return jumpInstruction("OP_JUMP_IF_TRUE", 1, chunk, offset, high2bit);
	case OP_LOOP:
		return jumpInstruction("OP_LOOP", -1, chunk, offset, high2bit);

	case OP_IMM:
		return immidiateIstruction("OP_IMM", offset, high2bit);

	case OP_MODULE_GLOBAL:
		return simpleInstruction("OP_MODULE_GLOBAL", offset);
	case OP_MODULE_BUILTIN:
		return builtinStruction("OP_MODULE", chunk, offset);
	default:
		printf("Unknown opcode %d\n", instruction);
		return offset + 1;
	}
}
#endif //  DEBUG_TRACE_EXECUTION

#if DEBUG_PRINT_CODE
void disassembleChunk(Chunk* chunk, C_STR name) {
	printf("== %s ==\n", name);

	uint32_t offset = 0;
	while (offset < chunk->count) {
		offset = disassembleInstruction(chunk, offset);
	}

	printf("== %s end ==\n", name);
}
#endif