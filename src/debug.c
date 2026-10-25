/*
 * MIT License
 * Copyright (c) 2025-2026 IMSDcrueoft (https://github.com/IMSDcrueoft)
 * See LICENSE file in the root directory for full license text.
*/
#include "debug.h"
#include "nativeBuiltin.h"
#include "vm.h"
#include <assert.h>

//output stream for all disassembly printing; NULL falls back to stderr.
//disassembly is a diagnostic dump, so it must never pollute the script's
//own stdout (--print-bytecode additionally pins it to stderr explicitly)
static FILE* disasmStream = NULL;

void debug_setDisasmStream(FILE* stream) {
	disasmStream = stream;
}

static void disPrintValue(Value value) {
	printValue((disasmStream != NULL) ? disasmStream : stderr, value, false);
}

#define DIS_PRINTF(...) fprintf((disasmStream != NULL) ? disasmStream : stderr, __VA_ARGS__)

COLD_FUNCTION
static uint32_t simpleInstruction(C_STR name, uint32_t offset) {
	DIS_PRINTF("%s\n", name);
	//OP_RETURN 1
	return offset + 1;
}

COLD_FUNCTION
static uint32_t builtinInStruction(C_STR name, Chunk* chunk, uint32_t offset) {
	uint32_t slot = chunk->code[offset + 1];
	switch (slot)
	{
	case MODULE_MATH:
		DIS_PRINTF("%-16s %-10s\n", name, "@math");
		break;
	case MODULE_ARRAY:
		DIS_PRINTF("%-16s %-10s\n", name, "@array");
		break;
	case MODULE_OBJECT:
		DIS_PRINTF("%-16s %-10s\n", name, "@object");
		break;
	case MODULE_STRING:
		DIS_PRINTF("%-16s %-10s\n", name, "@string");
		break;
	case MODULE_TIME:
		DIS_PRINTF("%-16s %-10s\n", name, "@time");
		break;
	case MODULE_CTOR:
		DIS_PRINTF("%-16s %-10s\n", name, "@ctor");
		break;
	case MODULE_SYSTEM:
		DIS_PRINTF("%-16s %-10s\n", name, "@sys");
		break;
	}
	return offset + 2;
}

COLD_FUNCTION
static uint32_t bitwiseInStruction(C_STR name, Chunk* chunk, uint32_t offset) {
	uint32_t slot = chunk->code[offset + 1];
	//1-byte operand
	uint32_t constant1B = (uint32_t)chunk->code[offset + 2];
	//4-byte operand
	uint32_t constant4B = ((uint32_t)chunk->code[offset + 2]) | ((uint32_t)chunk->code[offset + 3] << 8) | ((uint32_t)chunk->code[offset + 4] << 16) | ((uint32_t)chunk->code[offset + 5] << 24);

	switch (slot)
	{
	case BIT_OP_NOT:
		DIS_PRINTF("%-10s : %-4s\n", name, "NOT");
		break;
	case BIT_OP_AND:
		DIS_PRINTF("%-10s : %-4s\n", name, "AND");
		break;
	case BIT_OP_OR:
		DIS_PRINTF("%-10s : %-4s\n", name, "OR");
		break;
	case BIT_OP_XOR:
		DIS_PRINTF("%-10s : %-4s\n", name, "XOR");
		break;
	case BIT_OP_SHL:
		DIS_PRINTF("%-10s : %-4s\n", name, "SHL");
		break;
	case BIT_OP_SHR:
		DIS_PRINTF("%-10s : %-4s\n", name, "SHR");
		break;
	case BIT_OP_SAR:
		DIS_PRINTF("%-10s : %-4s\n", name, "SAR");
		break;
	case BIT_OP_ANDI:
		DIS_PRINTF("%-10s : %-4s  0x%08X\n", name, "ANDI", constant4B);
		offset += 4;
		break;
	case BIT_OP_ORI:
		DIS_PRINTF("%-10s : %-4s  0x%08X\n", name, "ORI", constant4B);
		offset += 4;
		break;
	case BIT_OP_XORI:
		DIS_PRINTF("%-10s : %-4s  0x%08X\n", name, "XORI", constant4B);
		offset += 4;
		break;
	case BIT_OP_SHLI:
		DIS_PRINTF("%-10s : %-4s  %u\n", name, "SHLI", constant1B);
		offset += 1;
		break;
	case BIT_OP_SHRI:
		DIS_PRINTF("%-10s : %-4s  %u\n", name, "SHRI", constant1B);
		offset += 1;
		break;
	case BIT_OP_SARI:
		DIS_PRINTF("%-10s : %-4s  %u\n", name, "SARI", constant1B);
		offset += 1;
		break;
	}
	return offset + 2;
}

COLD_FUNCTION
static uint32_t byteInstruction(C_STR name, Chunk* chunk, uint32_t offset) {
	uint32_t slot = chunk->code[offset + 1];
	DIS_PRINTF("%-16s %4d\n", name, slot);
	return offset + 2;
}

COLD_FUNCTION
static uint32_t jumpInstruction(C_STR name, int32_t sign, Chunk* chunk, uint32_t offset) {
	uint16_t jump = (uint16_t)chunk->code[offset + 1];
	jump |= (chunk->code[offset + 2] << 8);

	DIS_PRINTF("%-16s %4d -> %d\n", name, offset, offset + 3 + sign * jump);
	return offset + 3;
}

COLD_FUNCTION
static uint32_t shortInstruction(C_STR name, Chunk* chunk, uint32_t offset) {
	uint32_t slot = ((uint32_t)chunk->code[offset + 1]) | ((uint32_t)chunk->code[offset + 2] << 8);
	DIS_PRINTF("%-16s %4d\n", name, slot);
	return offset + 3;
}

COLD_FUNCTION
static uint32_t localToLocalInstruction(C_STR name, Chunk* chunk, uint32_t offset) {
	uint32_t srcIndex = ((uint32_t)chunk->code[offset + 1]) | ((uint32_t)chunk->code[offset + 2] << 8);
	uint32_t desIndex = ((uint32_t)chunk->code[offset + 3]) | ((uint32_t)chunk->code[offset + 4] << 8);
	DIS_PRINTF("%-16s Rs:%4d  Rd:%4d\n", name, srcIndex, desIndex);
	return offset + 5;
}

//fused compare + jump: [op:8][localA:16][localB:16][offset:16]
COLD_FUNCTION
static uint32_t jifLLInstruction(C_STR name, Chunk* chunk, uint32_t offset) {
	uint32_t slotA = ((uint32_t)chunk->code[offset + 1]) | ((uint32_t)chunk->code[offset + 2] << 8);
	uint32_t slotB = ((uint32_t)chunk->code[offset + 3]) | ((uint32_t)chunk->code[offset + 4] << 8);
	uint16_t jump = (uint16_t)(chunk->code[offset + 5] | (chunk->code[offset + 6] << 8));

	DIS_PRINTF("%-16s Rs:%4d  Rd:%4d  %4d -> %d\n", name, slotA, slotB, offset, offset + 7 + jump);
	return offset + 7;
}

//fused compare + jump, number constant: [op:8][local:16][const:16][offset:16]
COLD_FUNCTION
static uint32_t jifLCNumberInstruction(C_STR name, ObjFunction* function, uint32_t offset) {
	uint32_t slot = ((uint32_t)function->chunk.code[offset + 1]) | ((uint32_t)function->chunk.code[offset + 2] << 8);
	uint32_t constant = ((uint32_t)function->chunk.code[offset + 3]) | ((uint32_t)function->chunk.code[offset + 4] << 8);
	uint16_t jump = (uint16_t)(function->chunk.code[offset + 5] | (function->chunk.code[offset + 6] << 8));

	DIS_PRINTF("%-16s R:%4d '", name, slot);
	assert(constant < function->constantsCount);
	disPrintValue(function->constants[constant]);
	DIS_PRINTF("' %4d -> %d\n", offset, offset + 7 + jump);
	return offset + 7;
}

//fused compare + jump, non-number constant: [op:8][local:16][const:24][offset:16]
COLD_FUNCTION
static uint32_t jifLCInstruction(C_STR name, Chunk* chunk, uint32_t offset) {
	uint32_t slot = ((uint32_t)chunk->code[offset + 1]) | ((uint32_t)chunk->code[offset + 2] << 8);
	uint32_t constant = ((uint32_t)chunk->code[offset + 3]) | ((uint32_t)chunk->code[offset + 4] << 8) | ((uint32_t)chunk->code[offset + 5] << 16);
	uint16_t jump = (uint16_t)(chunk->code[offset + 6] | (chunk->code[offset + 7] << 8));

	DIS_PRINTF("%-16s R:%4d '", name, slot);
	disPrintValue(vm.constants.values[constant]);
	DIS_PRINTF("' %4d -> %d\n", offset, offset + 8 + jump);
	return offset + 8;
}

COLD_FUNCTION
static uint32_t constantInstruction(C_STR name, Chunk* chunk, uint32_t offset) {
	//24bit index
	uint32_t constant = ((uint32_t)chunk->code[offset + 1]) | ((uint32_t)chunk->code[offset + 2] << 8) | ((uint32_t)chunk->code[offset + 3] << 16);

	DIS_PRINTF("%-16s %4d '", name, constant);
	disPrintValue(vm.constants.values[constant]);
	DIS_PRINTF("'\n");

	//OP_CONSTANT 4
	return offset + 4;
}

COLD_FUNCTION
static uint32_t numberConstantInstruction(C_STR name, ObjFunction* function, uint32_t offset) {
	//16bit index into function's own constants
	uint32_t constant = ((uint32_t)function->chunk.code[offset + 1]) | ((uint32_t)function->chunk.code[offset + 2] << 8);

	DIS_PRINTF("%-16s %4d '", name, constant);
	assert(constant < function->constantsCount);
	disPrintValue(function->constants[constant]);
	DIS_PRINTF("'\n");

	//OP_CONST_NUMBER 3
	return offset + 3;
}

COLD_FUNCTION
static uint32_t invokeInstruction(C_STR name, Chunk* chunk, uint32_t offset) {
	//24bit index
	uint32_t constant = ((uint32_t)chunk->code[offset + 1]) | ((uint32_t)chunk->code[offset + 2] << 8) | ((uint32_t)chunk->code[offset + 3] << 16);
	uint8_t argCount = chunk->code[offset + 4];
	uint8_t slot = chunk->code[offset + 5];
	DIS_PRINTF("%-16s (%d args) %4d '", name, argCount, constant);
	disPrintValue(vm.constants.values[constant]);
	DIS_PRINTF("' (cache %u)\n", slot);
	return offset + 6;
}

//property instruction: [op][name u24][cache slot u8]
COLD_FUNCTION
static uint32_t cachedPropertyInstruction(C_STR name, Chunk* chunk, uint32_t offset) {
	//24bit index
	uint32_t constant = ((uint32_t)chunk->code[offset + 1]) | ((uint32_t)chunk->code[offset + 2] << 8) | ((uint32_t)chunk->code[offset + 3] << 16);
	uint8_t slot = chunk->code[offset + 4];
	DIS_PRINTF("%-16s %4d '", name, constant);
	disPrintValue(vm.constants.values[constant]);
	DIS_PRINTF("' (cache %u)\n", slot);
	return offset + 5;
}

//fused local property instruction: [op][local u16][name u24][cache slot u8]
COLD_FUNCTION
static uint32_t localPropertyInstruction(C_STR name, Chunk* chunk, uint32_t offset) {
	uint32_t local = ((uint32_t)chunk->code[offset + 1]) | ((uint32_t)chunk->code[offset + 2] << 8);
	uint32_t constant = ((uint32_t)chunk->code[offset + 3]) | ((uint32_t)chunk->code[offset + 4] << 8) | ((uint32_t)chunk->code[offset + 5] << 16);
	uint8_t slot = chunk->code[offset + 6];
	DIS_PRINTF("%-16s R:%4d %4d '", name, local, constant);
	disPrintValue(vm.constants.values[constant]);
	DIS_PRINTF("' (cache %u)\n", slot);
	return offset + 7;
}

COLD_FUNCTION
uint32_t disassembleInstruction(ObjFunction* function, uint32_t offset) {
	Chunk* chunk = &function->chunk;

	DIS_PRINTF("%04d ", offset);

	if (offset > 0 && getLine(&chunk->lines, offset) == getLine(&chunk->lines, offset - 1)) {
		DIS_PRINTF("   | ");//means it's the same line
	}
	else {
		DIS_PRINTF("%4d ", getLine(&chunk->lines, offset));
	}

	uint8_t instruction = chunk->code[offset];

	switch (instruction) {
	case OP_CALL:
		return byteInstruction("OP_CALL", chunk, offset);
	case OP_INVOKE:
		return invokeInstruction("OP_INVOKE", chunk, offset);
	case OP_SUPER_INVOKE:
		return invokeInstruction("OP_SUPER_INVOKE", chunk, offset);
	case OP_RETURN:
		return simpleInstruction("OP_RETURN", offset);
	case OP_ITERATE:
		return jumpInstruction("OP_ITERATE", 1, chunk, offset);
	case OP_FREE_LOCAL:
		return shortInstruction("OP_FREE_LOCAL", chunk, offset);
	case OP_THROW:
		return simpleInstruction("OP_THROW", offset);
	case OP_POP:
		return simpleInstruction("OP_POP", offset);
	case OP_PRINT: {
		uint8_t argCount = chunk->code[offset + 1];
		DIS_PRINTF("%-16s %4u\n", "OP_PRINT", argCount);
		return offset + 2;
	}
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

	case OP_CONST_NUMBER:
		return numberConstantInstruction("OP_CONST_NUMBER", function, offset);

	case OP_CLOSURE: {
		//24bit index
		uint32_t constant = ((uint32_t)chunk->code[offset + 1]) | ((uint32_t)chunk->code[offset + 2] << 8) | ((uint32_t)chunk->code[offset + 3] << 16);

		DIS_PRINTF("%-16s %4d '", "OP_CLOSURE", constant);
		disPrintValue(vm.constants.values[constant]);
		DIS_PRINTF("'\n");

		//OP_CLOSURE 4
		offset += 4;

		ObjFunction* function = AS_FUNCTION(vm.constants.values[constant]);
		for (uint32_t j = 0; j < function->upvalueCount; j++) {
			int32_t isLocal = chunk->code[offset++];
			uint16_t index = chunk->code[offset++];
			index |= (chunk->code[offset++] << 8);

			DIS_PRINTF("%04d      |                     %s %d\n",
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
	case OP_INSTANCE_OF:
		return simpleInstruction("OP_INSTANCE_OF", offset);
	case OP_TYPE_OF:
		return simpleInstruction("OP_TYPE_OF", offset);
	case OP_IN:
		return simpleInstruction("OP_IN", offset);

	case OP_BITWISE:
		return bitwiseInStruction("OP_BITWISE", chunk, offset);

	case OP_DEFINE_GLOBAL:
		return constantInstruction("OP_DEFINE_GLOBAL", chunk, offset);
	case OP_GET_GLOBAL:
		return constantInstruction("OP_GET_GLOBAL", chunk, offset);
	case OP_SET_GLOBAL:
		return constantInstruction("OP_SET_GLOBAL", chunk, offset);
	case OP_CLASS:
		return constantInstruction("OP_CLASS", chunk, offset);
	case OP_INHERIT:
		return simpleInstruction("OP_INHERIT", offset);
	case OP_METHOD:
		return constantInstruction("OP_METHOD", chunk, offset);
	case OP_GET_SUPER:
		return constantInstruction("OP_GET_SUPER", chunk, offset);
	case OP_GET_PROPERTY:
		return cachedPropertyInstruction("OP_GET_PROPERTY", chunk, offset);
	case OP_SET_PROPERTY:
		return cachedPropertyInstruction("OP_SET_PROPERTY", chunk, offset);
	case OP_SET_PROPERTY_POP:
		return cachedPropertyInstruction("OP_SET_PROPERTY_POP", chunk, offset);
	case OP_GET_PROP_LOCAL:
		return localPropertyInstruction("OP_GET_PROP_LOCAL", chunk, offset);
	case OP_SET_PROP_LOCAL:
		return localPropertyInstruction("OP_SET_PROP_LOCAL", chunk, offset);
	case OP_SET_PROP_LOCAL_POP:
		return localPropertyInstruction("OP_SET_PROP_LOCAL_POP", chunk, offset);
	case OP_GET_INDEX:
		return numberConstantInstruction("OP_GET_INDEX", function, offset);
	case OP_SET_INDEX:
		return numberConstantInstruction("OP_SET_INDEX", function, offset);

	case OP_GET_SUBSCRIPT:
		return simpleInstruction("OP_GET_SUBSCRIPT", offset);
	case OP_SET_SUBSCRIPT:
		return simpleInstruction("OP_SET_SUBSCRIPT", offset);

	case OP_NEW_OBJECT:
		return simpleInstruction("OP_NEW_OBJECT", offset);
	case OP_NEW_PROPERTY:
		return constantInstruction("OP_NEW_PROPERTY", chunk, offset);
	case OP_NEW_ITERATOR:
		return simpleInstruction("OP_NEW_ITERATOR", offset);

	case OP_NEW_ARRAY:
		return shortInstruction("OP_NEW_ARRAY", chunk, offset);
	case OP_GET_LOCAL:
		return shortInstruction("OP_GET_LOCAL", chunk, offset);
	case OP_SET_LOCAL:
		return shortInstruction("OP_SET_LOCAL", chunk, offset);
	case OP_SET_LOCAL_POP:
		return shortInstruction("OP_SET_LOCAL_POP", chunk, offset);
	case OP_MOVE_LOCAL:
		return localToLocalInstruction("OP_MOVE_LOCAL", chunk, offset);
	case OP_POP_N:
		return shortInstruction("OP_POP_N", chunk, offset);

	case OP_JUMP:
		return jumpInstruction("OP_JUMP", 1, chunk, offset);
	case OP_JUMP_IF_FALSE:
		return jumpInstruction("OP_JUMP_IF_FALSE", 1, chunk, offset);
	case OP_JUMP_IF_FALSE_POP:
		return jumpInstruction("OP_JUMP_IF_FALSE_POP", 1, chunk, offset);
	case OP_JUMP_IF_TRUE:
		return jumpInstruction("OP_JUMP_IF_TRUE", 1, chunk, offset);
	case OP_LOOP:
		return jumpInstruction("OP_LOOP", -1, chunk, offset);

	case OP_MODULE_BUILTIN:
		return builtinInStruction("OP_MODULE", chunk, offset);
	case OP_IMPORT:
		return simpleInstruction("OP_IMPORT", offset);

	case OP_ADD_CONST:
		return numberConstantInstruction("OP_ADD_CONST", function, offset);
	case OP_SUBTRACT_CONST:
		return numberConstantInstruction("OP_SUBTRACT_CONST", function, offset);
	case OP_MULTIPLY_CONST:
		return numberConstantInstruction("OP_MULTIPLY_CONST", function, offset);
	case OP_DIVIDE_CONST:
		return numberConstantInstruction("OP_DIVIDE_CONST", function, offset);
	case OP_MODULUS_CONST:
		return numberConstantInstruction("OP_MODULUS_CONST", function, offset);
	case OP_EQUAL_CONST:
		return constantInstruction("OP_EQUAL_CONST", chunk, offset);
	case OP_NOT_EQUAL_CONST:
		return constantInstruction("OP_NOT_EQUAL_CONST", chunk, offset);
	case OP_GREATER_CONST:
		return numberConstantInstruction("OP_GREATER_CONST", function, offset);
	case OP_GREATER_EQUAL_CONST:
		return numberConstantInstruction("OP_GREATER_EQUAL_CONST", function, offset);
	case OP_LESS_CONST:
		return numberConstantInstruction("OP_LESS_CONST", function, offset);
	case OP_LESS_EQUAL_CONST:
		return numberConstantInstruction("OP_LESS_EQUAL_CONST", function, offset);
	case OP_EQUAL_CONST_NUMBER:
		return numberConstantInstruction("OP_EQUAL_CONST_NUMBER", function, offset);
	case OP_NOT_EQUAL_CONST_NUMBER:
		return numberConstantInstruction("OP_NOT_EQUAL_CONST_NUMBER", function, offset);

	case OP_ADD_LOCAL:
		return shortInstruction("OP_ADD_LOCAL", chunk, offset);
	case OP_SUBTRACT_LOCAL:
		return shortInstruction("OP_SUBTRACT_LOCAL", chunk, offset);
	case OP_MULTIPLY_LOCAL:
		return shortInstruction("OP_MULTIPLY_LOCAL", chunk, offset);
	case OP_DIVIDE_LOCAL:
		return shortInstruction("OP_DIVIDE_LOCAL", chunk, offset);
	case OP_MODULUS_LOCAL:
		return shortInstruction("OP_MODULUS_LOCAL", chunk, offset);
	case OP_NOT_LOCAL:
		return shortInstruction("OP_NOT_LOCAL", chunk, offset);
	case OP_NEGATE_LOCAL:
		return shortInstruction("OP_NEGATE_LOCAL", chunk, offset);
	case OP_CEDE:
		return simpleInstruction("OP_CEDE", offset);

	case OP_EQUAL_LOCAL:
		return shortInstruction("OP_EQUAL_LOCAL", chunk, offset);
	case OP_NOT_EQUAL_LOCAL:
		return shortInstruction("OP_NOT_EQUAL_LOCAL", chunk, offset);
	case OP_GREATER_LOCAL:
		return shortInstruction("OP_GREATER_LOCAL", chunk, offset);
	case OP_GREATER_EQUAL_LOCAL:
		return shortInstruction("OP_GREATER_EQUAL_LOCAL", chunk, offset);
	case OP_LESS_LOCAL:
		return shortInstruction("OP_LESS_LOCAL", chunk, offset);
	case OP_LESS_EQUAL_LOCAL:
		return shortInstruction("OP_LESS_EQUAL_LOCAL", chunk, offset);

	case OP_JIF_LESS_LL:
		return jifLLInstruction("OP_JIF_LESS_LL", chunk, offset);
	case OP_JIF_LESS_EQUAL_LL:
		return jifLLInstruction("OP_JIF_LESS_EQUAL_LL", chunk, offset);
	case OP_JIF_EQUAL_LL:
		return jifLLInstruction("OP_JIF_EQUAL_LL", chunk, offset);
	case OP_JIF_NOT_EQUAL_LL:
		return jifLLInstruction("OP_JIF_NOT_EQUAL_LL", chunk, offset);

	case OP_JIF_LESS_LC_NUMBER:
		return jifLCNumberInstruction("OP_JIF_LESS_LC_NUMBER", function, offset);
	case OP_JIF_LESS_EQUAL_LC_NUMBER:
		return jifLCNumberInstruction("OP_JIF_LESS_EQUAL_LC_NUMBER", function, offset);
	case OP_JIF_GREATER_LC_NUMBER:
		return jifLCNumberInstruction("OP_JIF_GREATER_LC_NUMBER", function, offset);
	case OP_JIF_GREATER_EQUAL_LC_NUMBER:
		return jifLCNumberInstruction("OP_JIF_GREATER_EQUAL_LC_NUMBER", function, offset);
	case OP_JIF_EQUAL_LC_NUMBER:
		return jifLCNumberInstruction("OP_JIF_EQUAL_LC_NUMBER", function, offset);
	case OP_JIF_NOT_EQUAL_LC_NUMBER:
		return jifLCNumberInstruction("OP_JIF_NOT_EQUAL_LC_NUMBER", function, offset);
	case OP_JIF_EQUAL_LC:
		return jifLCInstruction("OP_JIF_EQUAL_LC", chunk, offset);
	case OP_JIF_NOT_EQUAL_LC:
		return jifLCInstruction("OP_JIF_NOT_EQUAL_LC", chunk, offset);

	default:
		DIS_PRINTF("Unknown opcode %d offset = %d\n", instruction, offset);
		return offset + 1;
	}
}

COLD_FUNCTION
void disassembleChunk(ObjFunction* function, C_STR name, uint32_t id) {
	Chunk* chunk = &function->chunk;

	DIS_PRINTF("== %s(%d) ==\n", name, id);

	uint32_t offset = 0;
	while (offset < chunk->count) {
		offset = disassembleInstruction(function, offset);
	}

	DIS_PRINTF("== %s(%d) end==\n", name, id);
}

#if DEBUG_TRACE_EXECUTION || DEBUG_PRINT_CODE
COLD_FUNCTION
void disassembleOpStack(OPStack* opStack) {
	DIS_PRINTF("== opStack begin ==\n");
	for (uint32_t i = 0; i < opStack->count; ++i) {
		uint8_t code = opStack->code[i];

		switch (code) {
		case OP_CONSTANT:         DIS_PRINTF("OP_CONSTANT\n"); break;
		case OP_CONST_NUMBER:     DIS_PRINTF("OP_CONST_NUMBER\n"); break;
		case OP_GET_LOCAL:        DIS_PRINTF("OP_GET_LOCAL\n"); break;
		case OP_SET_LOCAL:        DIS_PRINTF("OP_SET_LOCAL\n"); break;
		case OP_ADD:              DIS_PRINTF("OP_ADD\n"); break;
		case OP_SUBTRACT:         DIS_PRINTF("OP_SUBTRACT\n"); break;
		case OP_MULTIPLY:         DIS_PRINTF("OP_MULTIPLY\n"); break;
		case OP_DIVIDE:           DIS_PRINTF("OP_DIVIDE\n"); break;
		case OP_MODULUS:          DIS_PRINTF("OP_MODULUS\n"); break;
		case OP_NOT:              DIS_PRINTF("OP_NOT\n"); break;
		case OP_NEGATE:           DIS_PRINTF("OP_NEGATE\n"); break;
		case OP_FALSE:            DIS_PRINTF("OP_FALSE\n"); break;
		case OP_TRUE:             DIS_PRINTF("OP_TRUE\n"); break;
		case OP_NIL:              DIS_PRINTF("OP_NIL\n"); break;
		case OP_EQUAL:            DIS_PRINTF("OP_EQUAL\n"); break;
		case OP_GREATER:          DIS_PRINTF("OP_GREATER\n"); break;
		case OP_LESS:             DIS_PRINTF("OP_LESS\n"); break;
		case OP_NOT_EQUAL:        DIS_PRINTF("OP_NOT_EQUAL\n"); break;
		case OP_LESS_EQUAL:       DIS_PRINTF("OP_LESS_EQUAL\n"); break;
		case OP_GREATER_EQUAL:    DIS_PRINTF("OP_GREATER_EQUAL\n"); break;
		case OP_BITWISE:		  DIS_PRINTF("OP_BITWISE\n"); break;
		case OP_POP:			  DIS_PRINTF("OP_POP\n"); break;
		case OP_GET_PROPERTY:     DIS_PRINTF("OP_GET_PROPERTY\n"); break;
		case OP_SET_PROPERTY:     DIS_PRINTF("OP_SET_PROPERTY\n"); break;
		case OP_SET_PROPERTY_POP: DIS_PRINTF("OP_SET_PROPERTY_POP\n"); break;
		case OP_GET_PROP_LOCAL:      DIS_PRINTF("OP_GET_PROP_LOCAL\n"); break;
		case OP_SET_PROP_LOCAL:      DIS_PRINTF("OP_SET_PROP_LOCAL\n"); break;
		case OP_SET_PROP_LOCAL_POP:  DIS_PRINTF("OP_SET_PROP_LOCAL_POP\n"); break;
		case OP_INVOKE:           DIS_PRINTF("OP_INVOKE\n"); break;
		case OP_CALL:             DIS_PRINTF("OP_CALL\n"); break;
		case OP_JUMP:             DIS_PRINTF("OP_JUMP\n"); break;
		case OP_LOOP:             DIS_PRINTF("OP_LOOP\n"); break;
		case OP_JUMP_IF_FALSE:    DIS_PRINTF("OP_JUMP_IF_FALSE\n"); break;
		case OP_JUMP_IF_FALSE_POP:DIS_PRINTF("OP_JUMP_IF_FALSE_POP\n"); break;
		case OP_JUMP_IF_TRUE:     DIS_PRINTF("OP_JUMP_IF_TRUE\n"); break;
		case OP_POP_N:            DIS_PRINTF("OP_POP_N\n"); break;
		case OP_SET_LOCAL_POP:    DIS_PRINTF("OP_SET_LOCAL_POP\n"); break;
		case OP_MOVE_LOCAL:       DIS_PRINTF("OP_MOVE_LOCAL\n"); break;
		case OP_GET_GLOBAL:       DIS_PRINTF("OP_GET_GLOBAL\n"); break;
		case OP_SET_GLOBAL:       DIS_PRINTF("OP_SET_GLOBAL\n"); break;
		case OP_DEFINE_GLOBAL:    DIS_PRINTF("OP_DEFINE_GLOBAL\n"); break;
		case OP_ADD_LOCAL:        DIS_PRINTF("OP_ADD_LOCAL\n"); break;
		case OP_SUBTRACT_LOCAL:   DIS_PRINTF("OP_SUBTRACT_LOCAL\n"); break;
		case OP_MULTIPLY_LOCAL:   DIS_PRINTF("OP_MULTIPLY_LOCAL\n"); break;
		case OP_DIVIDE_LOCAL:     DIS_PRINTF("OP_DIVIDE_LOCAL\n"); break;
		case OP_MODULUS_LOCAL:    DIS_PRINTF("OP_MODULUS_LOCAL\n"); break;
		case OP_EQUAL_LOCAL:      DIS_PRINTF("OP_EQUAL_LOCAL\n"); break;
		case OP_ADD_CONST:        DIS_PRINTF("OP_ADD_CONST\n"); break;
		case OP_SUBTRACT_CONST:   DIS_PRINTF("OP_SUBTRACT_CONST\n"); break;
		case OP_MULTIPLY_CONST:   DIS_PRINTF("OP_MULTIPLY_CONST\n"); break;
		case OP_DIVIDE_CONST:     DIS_PRINTF("OP_DIVIDE_CONST\n"); break;
		case OP_MODULUS_CONST:    DIS_PRINTF("OP_MODULUS_CONST\n"); break;
		case OP_EQUAL_CONST:      DIS_PRINTF("OP_EQUAL_CONST\n"); break;
		case OP_EQUAL_CONST_NUMBER: DIS_PRINTF("OP_EQUAL_CONST_NUMBER\n"); break;
		case OP_LESS_CONST:       DIS_PRINTF("OP_LESS_CONST\n"); break;
		case OP_GREATER_CONST:    DIS_PRINTF("OP_GREATER_CONST\n"); break;
		case OP_LESS_EQUAL_CONST: DIS_PRINTF("OP_LESS_EQUAL_CONST\n"); break;
		case OP_GREATER_EQUAL_CONST: DIS_PRINTF("OP_GREATER_EQUAL_CONST\n"); break;
		case OP_JIF_LESS_LL:      DIS_PRINTF("OP_JIF_LESS_LL\n"); break;
		case OP_JIF_LESS_EQUAL_LL:DIS_PRINTF("OP_JIF_LESS_EQUAL_LL\n"); break;
		case OP_JIF_EQUAL_LL:     DIS_PRINTF("OP_JIF_EQUAL_LL\n"); break;
		case OP_JIF_NOT_EQUAL_LL: DIS_PRINTF("OP_JIF_NOT_EQUAL_LL\n"); break;
		case OP_JIF_LESS_LC_NUMBER:        DIS_PRINTF("OP_JIF_LESS_LC_NUMBER\n"); break;
		case OP_JIF_LESS_EQUAL_LC_NUMBER:  DIS_PRINTF("OP_JIF_LESS_EQUAL_LC_NUMBER\n"); break;
		case OP_JIF_GREATER_LC_NUMBER:     DIS_PRINTF("OP_JIF_GREATER_LC_NUMBER\n"); break;
		case OP_JIF_GREATER_EQUAL_LC_NUMBER: DIS_PRINTF("OP_JIF_GREATER_EQUAL_LC_NUMBER\n"); break;
		case OP_JIF_EQUAL_LC_NUMBER:       DIS_PRINTF("OP_JIF_EQUAL_LC_NUMBER\n"); break;
		case OP_JIF_NOT_EQUAL_LC_NUMBER:   DIS_PRINTF("OP_JIF_NOT_EQUAL_LC_NUMBER\n"); break;
		case OP_JIF_EQUAL_LC:     DIS_PRINTF("OP_JIF_EQUAL_LC\n"); break;
		case OP_JIF_NOT_EQUAL_LC: DIS_PRINTF("OP_JIF_NOT_EQUAL_LC\n"); break;
		case OP_SET_INDEX:        DIS_PRINTF("OP_SET_INDEX\n"); break;
		case OP_GET_INDEX:        DIS_PRINTF("OP_GET_INDEX\n"); break;
		case OP_GET_SUPER:        DIS_PRINTF("OP_GET_SUPER\n"); break;
		case OP_SUPER_INVOKE:     DIS_PRINTF("OP_SUPER_INVOKE\n"); break;
		case OP_METHOD:           DIS_PRINTF("OP_METHOD\n"); break;
		case OP_INHERIT:          DIS_PRINTF("OP_INHERIT\n"); break;
		case OP_CLASS:            DIS_PRINTF("OP_CLASS\n"); break;
		case OP_MODULE_BUILTIN:   DIS_PRINTF("OP_MODULE_BUILTIN\n"); break;
		case OP_CLOSURE:          DIS_PRINTF("OP_CLOSURE\n"); break;
		case OP_RETURN:           DIS_PRINTF("OP_RETURN\n"); break;
		case OP_PRINT:            DIS_PRINTF("OP_PRINT\n"); break;
		case OP_CEDE:             DIS_PRINTF("OP_CEDE\n"); break;
		case OP_SET_SUBSCRIPT:    DIS_PRINTF("OP_SET_SUBSCRIPT\n"); break;
		case OP_GET_SUBSCRIPT:    DIS_PRINTF("OP_GET_SUBSCRIPT\n"); break;
		case OP_GET_UPVALUE:      DIS_PRINTF("OP_GET_UPVALUE\n"); break;
		case OP_SET_UPVALUE:      DIS_PRINTF("OP_SET_UPVALUE\n"); break;
		case OP_CLOSE_UPVALUE:    DIS_PRINTF("OP_CLOSE_UPVALUE\n"); break;
		case OP_NEW_ARRAY:        DIS_PRINTF("OP_NEW_ARRAY\n"); break;
		case OP_NEW_OBJECT:       DIS_PRINTF("OP_NEW_OBJECT\n"); break;
		case OP_NEW_PROPERTY:     DIS_PRINTF("OP_NEW_PROPERTY\n"); break;
		case OP_INSTANCE_OF:      DIS_PRINTF("OP_INSTANCE_OF\n"); break;
		case OP_TYPE_OF:          DIS_PRINTF("OP_TYPE_OF\n"); break;
		case OP_IN:               DIS_PRINTF("OP_IN\n"); break;
		case OP_THROW:            DIS_PRINTF("OP_THROW\n"); break;
		case OP_IMPORT:           DIS_PRINTF("OP_IMPORT\n"); break;
		case OP_NOT_EQUAL_CONST_NUMBER: DIS_PRINTF("OP_NOT_EQUAL_CONST_NUMBER\n"); break;
		case OP_NOT_EQUAL_CONST:  DIS_PRINTF("OP_NOT_EQUAL_CONST\n"); break;
		case OP_ITERATE:          DIS_PRINTF("OP_ITERATE\n"); break;
		case OP_FREE_LOCAL:       DIS_PRINTF("OP_FREE_LOCAL\n"); break;
		case OP_NEW_ITERATOR:     DIS_PRINTF("OP_NEW_ITERATOR\n"); break;
		default: break;//unknown codes are silently ignored in the debug dump
		}
	}
	DIS_PRINTF("== opStack end ==\n");
}
#endif