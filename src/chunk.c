/*
 * MIT License
 * Copyright (c) 2025 IMSDcrueoft (https://github.com/IMSDcrueoft)
 * See LICENSE file in the root directory for full license text.
*/
#include "chunk.h"

void chuck_init(Chunk* chunk) {
	chunk->count = 0u;
	chunk->capacity = 0u;
	chunk->code = NULL;

	lineArray_init(&chunk->lines);
}

void chunk_write(Chunk* chunk, uint8_t byte, uint32_t line) {
	if (chunk->capacity < chunk->count + 1) {
		uint32_t oldCapacity = chunk->capacity;

		chunk->capacity = GROW_CAPACITY(oldCapacity);
		chunk->code = GROW_ARRAY_NO_GC(uint8_t, chunk->code, oldCapacity, chunk->capacity);
	}

	lineArray_write(&chunk->lines, line, chunk->count);

	chunk->code[chunk->count] = byte;
	chunk->count++;
}

void chunk_fallback(Chunk* chunk, uint32_t byteCount)
{
	if (chunk->count > byteCount) {
		chunk->count -= byteCount;
		lineArray_fallback(&chunk->lines, chunk->count);
	}
}

COLD_FUNCTION
void chunk_free(Chunk* chunk) {
	FREE_ARRAY_NO_GC(uint8_t, chunk->code, chunk->capacity);
	lineArray_free(&chunk->lines);
	chuck_init(chunk);
}

//beginError:where error begins
COLD_FUNCTION
void chunk_free_errorCode(Chunk* chunk, uint32_t beginError) {
	if (chunk == NULL || beginError > chunk->count) {
		return;
	}
	else {
		if (beginError > 0) --beginError;

		chunk->count = beginError;
	}
}

void opStack_init(OPStack* stack)
{
	stack->count = 0u;
	stack->capacity = 0u;
	stack->code = NULL;
}

void opStack_push(OPStack* stack, uint8_t byte)
{
	if (stack->capacity < stack->count + 1) {
		uint32_t oldCapacity = stack->capacity;

		stack->capacity = GROW_CAPACITY(oldCapacity);
		stack->code = GROW_ARRAY_NO_GC(uint8_t, stack->code, oldCapacity, stack->capacity);
	}

	stack->code[stack->count] = byte;
	stack->count++;
}

uint8_t opStack_peek(OPStack* stack, uint8_t offset)
{
	if (stack->count > offset) {
		return stack->code[stack->count - offset - 1];
	}
	else {
		return UINT8_MAX;
	}
}

void opStack_pop(OPStack* stack)
{
	if (stack->count > 0) {
		--stack->count;
	}
}

//logic clear
void opStack_clear(OPStack* stack)
{
	stack->count = 0;
}

void opStack_free(OPStack* stack)
{
	FREE_ARRAY_NO_GC(uint8_t, stack->code, stack->capacity);
	opStack_init(stack);
}