#pragma once
#include "common.h"
#include "memory.h"

typedef struct {
	uint32_t line;      //the code line
	uint32_t offset;    //the chunk offset end
} RangeLine;

typedef struct {
	uint32_t count;    //limit to 4GB
	uint32_t capacity; //limit to 4GB
	RangeLine* ranges;
} LineArray;

void lineArray_init(LineArray* array);
void lineArray_write(LineArray* array, uint32_t line, uint32_t offset);
void lineArray_free(LineArray* array);

//getLine info by bin search
uint32_t getLine(LineArray* lines, uint32_t offset);