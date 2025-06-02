/*
 * MIT License
 * Copyright (c) 2025 IMSDcrueoft (https://github.com/IMSDcrueoft)
 * See LICENSE file in the root directory for full license text.
*/
#pragma once
#include "chunk.h"
#if DEBUG_TRACE_EXECUTION || DEBUG_PRINT_CODE
uint32_t disassembleInstruction(Chunk* chunk, uint32_t offset);
void disassembleChunk(Chunk* chunk, C_STR name, uint32_t id);
void disassembleOpStack(OPStack* opStack);
#endif