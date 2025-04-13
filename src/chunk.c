/*
 * MIT License
 * Copyright (c) 2025 IM&SD (https://github.com/IMSDcrueoft)
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

COLD_FUNCTION
void chunk_free(Chunk* chunk) {
	FREE_ARRAY_NO_GC(uint8_t, chunk->code, chunk->capacity);
	lineArray_free(&chunk->lines);
	chuck_init(chunk);
}

//beginError:where error begins
void chunk_free_errorCode(Chunk* chunk, uint32_t beginError) {
	if (chunk == NULL || beginError > chunk->count) {
		return;
	}
	else {
		if (beginError > 0) --beginError;

		chunk->count = beginError;
	}
}