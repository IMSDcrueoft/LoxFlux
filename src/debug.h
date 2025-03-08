#pragma once
#include "chunk.h"

#if DEBUG_TRACE_EXECUTION
uint32_t disassembleInstruction(Chunk* chunk, uint32_t offset);
#endif

#if DEBUG_PRINT_CODE
void disassembleChunk(Chunk* chunk, C_STR name);
#endif