/*
 * MIT License
 * Copyright (c) 2025-2026 IMSDcrueoft (https://github.com/IMSDcrueoft)
 * See LICENSE file in the root directory for full license text.
*/
#include "chunk.h"

void chunk_init(Chunk* chunk) {
	chunk->count = 0u;
	chunk->capacity = 0u;
	chunk->code = NULL;

	lineArray_init(&chunk->lines);
}

void chunk_write(LfxVM* vm, Chunk* chunk, uint8_t byte, uint32_t line) {
	if (chunk->capacity < chunk->count + 1) {
		uint32_t oldCapacity = chunk->capacity;

		if (oldCapacity >= GROW_CAPACITY_MAX) {
			fprintf(stderr, "Chunk too large.\n");
			exit(1);
		}

		chunk->capacity = GROW_CAPACITY(oldCapacity);
		chunk->code = GROW_ARRAY_NO_GC(vm, uint8_t, chunk->code, oldCapacity, chunk->capacity);
	}

	lineArray_write(vm, &chunk->lines, line, chunk->count);

	chunk->code[chunk->count] = byte;
	chunk->count++;
}

void chunk_fallback(Chunk* chunk, uint32_t byteCount)
{
	if (chunk->count >= byteCount) {
		chunk->count -= byteCount;
		lineArray_fallback(&chunk->lines, chunk->count);
	}
	else {
		fprintf(stderr, "chunk fallback too many!\n");
	}
}

void chunk_free(LfxVM* vm, Chunk* chunk) {
	FREE_ARRAY_NO_GC(vm, uint8_t, chunk->code, chunk->capacity);
	lineArray_free(vm, &chunk->lines);
	chunk_init(chunk);
}

void opStack_init(OPStack* stack)
{
	stack->count = 0u;
	stack->capacity = 0u;
	stack->code = NULL;
}

void opStack_push(LfxVM* vm, OPStack* stack, uint8_t byte)
{
	if (stack->capacity < stack->count + 1) {
		uint32_t oldCapacity = stack->capacity;

		if (oldCapacity >= GROW_CAPACITY_MAX) {
			fprintf(stderr, "Op stack too large.\n");
			exit(1);
		}

		stack->capacity = GROW_CAPACITY(oldCapacity);
		stack->code = GROW_ARRAY_NO_GC(vm, uint8_t, stack->code, oldCapacity, stack->capacity);
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

void opStack_fallback(OPStack* stack, uint32_t byteCount)
{
	if (stack->count >= byteCount) {
		stack->count -= byteCount;
	}
	else {
		fprintf(stderr, "opStack fallback too many!\n");
	}
}

//logic clear
void opStack_clear(OPStack* stack)
{
	if (stack->count > 0) {
		stack->count = 0;
	}
}

void opStack_free(LfxVM* vm, OPStack* stack)
{
	FREE_ARRAY_NO_GC(vm, uint8_t, stack->code, stack->capacity);
	opStack_init(stack);
}