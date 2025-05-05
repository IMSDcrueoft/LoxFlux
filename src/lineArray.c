/*
 * MIT License
 * Copyright (c) 2025 IMSDcrueoft (https://github.com/IMSDcrueoft)
 * See LICENSE file in the root directory for full license text.
*/
#include "lineArray.h"
#include "memory.h"

void lineArray_init(LineArray* array) {
	array->ranges = NULL;
	array->capacity = 0u;
	array->index = 0u;
}

void lineArray_write(LineArray* array, uint32_t line, uint32_t offset) {
	uint32_t index = array->index;

	if ((index + 1) >= array->capacity) {
		uint32_t oldCapacity = array->capacity;
		array->capacity = GROW_CAPACITY(oldCapacity);
		array->ranges = GROW_ARRAY_NO_GC(RangeLine, array->ranges, oldCapacity, array->capacity);

		//init
		if (oldCapacity == 0) {
			array->ranges[index].line = line;
			array->ranges[index].offset = offset;
		}
		else {
			memset(array->ranges + oldCapacity, 0, sizeof(RangeLine) * (array->capacity - oldCapacity));
		}
	}

	if (array->ranges[index].line != line) {
		index = ++array->index;
		array->ranges[index].line = line;
		array->ranges[index].offset = offset;
	}
	else {
		array->ranges[index].offset = offset;
	}
}

void lineArray_fallback(LineArray* array, uint32_t targetOffset)
{
	if (array->capacity != 0) {
		uint32_t index = array->index;

		if (targetOffset < array->ranges[index].offset) {
			while ((index > 0) && (array->ranges[index - 1].offset > targetOffset)) {
				--index;
			}

			array->index = index;
			array->ranges[index].offset = targetOffset;
		}
	}
	else {
		fprintf(stderr, "Trying to fallback uninitialized lineArray.");
	}
}

void lineArray_free(LineArray* array) {
	FREE_ARRAY_NO_GC(RangeLine, array->ranges, array->capacity);
	lineArray_init(array);
}

//getline info bin search
uint32_t getLine(LineArray* array, uint32_t offset) {
	uint32_t low = 0, high = array->index, mid;

	while (low < high) {
		mid = low + ((high - low) >> 1);

		if (offset > array->ranges[mid].offset) {
			low = mid + 1;
		}
		else if (offset < array->ranges[mid].offset) {
			high = mid; //findLast offset < ranges[]->offset
		}
		else {
			return array->ranges[mid].line;
		}
	}

	return (offset <= array->ranges[low].offset) ? array->ranges[low].line : -1;
}