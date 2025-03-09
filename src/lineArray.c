/*
 * MIT License
 * Copyright (c) 2025 IM&SD (https://github.com/IMSDcrueoft)
 * See LICENSE file in the root directory for full license text.
*/
#include "lineArray.h"
void lineArray_init(LineArray* array) {
	array->ranges = NULL;
	array->capacity = 0u;
	array->count = 0u;
}

void lineArray_write(LineArray* array, uint32_t line, uint32_t offset) {
	uint32_t count = array->count;

	if (count + 1 > array->capacity) {
		uint32_t oldCapacity = array->capacity;
		array->capacity = GROW_CAPACITY(oldCapacity);
		array->ranges = GROW_ARRAY(RangeLine, array->ranges, oldCapacity, array->capacity);

		//init
		if (oldCapacity == 0) {
			array->ranges[count].line = line;
			array->ranges[count].offset = offset;
		}
		else {
			memset(array->ranges + oldCapacity, 0, sizeof(RangeLine) * (array->capacity - oldCapacity));
		}
	}

	if (array->ranges[count].line != line) {
		count = ++array->count;
		array->ranges[count].line = line;
		array->ranges[count].offset = offset;
	}
	else {
		array->ranges[count].offset = offset;
	}
}

void lineArray_free(LineArray* array) {
	FREE_ARRAY(RangeLine, array->ranges, array->capacity);
	lineArray_init(array);
}

//getline info bin search
uint32_t getLine(LineArray* array, uint32_t offset) {
	uint32_t low = 0, high = array->count, mid;

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