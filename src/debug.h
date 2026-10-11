/*
 * MIT License
 * Copyright (c) 2025-2026 IMSDcrueoft (https://github.com/IMSDcrueoft)
 * See LICENSE file in the root directory for full license text.
*/
#pragma once
#include "chunk.h"
#include "object.h"

uint32_t disassembleInstruction(ObjFunction* function, uint32_t offset);
void disassembleChunk(ObjFunction* function, C_STR name, uint32_t id);

//redirects all disassembly printing to the given stream (NULL = stdout);
//used by the runtime --print-bytecode flag to keep the script's stdout clean
void debug_setDisasmStream(FILE* stream);

#if DEBUG_TRACE_EXECUTION || DEBUG_PRINT_CODE
//optimizer-fusion dump: debug builds only, never part of --print-bytecode output
void disassembleOpStack(OPStack* opStack);
#endif