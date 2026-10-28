/*
 * MIT License
 * Copyright (c) 2025-2026 IMSDcrueoft (https://github.com/IMSDcrueoft)
 * See LICENSE file in the root directory for full license text.
*/
#pragma once
#include "common.h"

int loxFluxMain(int argc, C_STR argv[]);
void runFile(LfxVM* vm, C_STR path);
void repl(LfxVM* vm);