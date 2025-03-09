/*
 * MIT License
 * Copyright (c) 2025 IM&SD (https://github.com/IMSDcrueoft)
 * See LICENSE file in the root directory for full license text.
*/
#pragma once
// to use debug mode open this
#define DEBUG_MODE 1
#define DEBUG_TRACE_EXECUTION 1
#define DEBUG_PRINT_CODE 1

//to use the logs
#define LOG_MODE 1
#define LOG_COMPILE_TIMING 1
#define LOG_EXECUTE_TIMING 1
#define LOG_MALLOC_INFO 1
#define LOG_BIPMS 1

#if !DEBUG_MODE
#undef DEBUG_PRINT_CODE
#undef DEBUG_TRACE_EXECUTION
#elif !DEBUG_TRACE_EXECUTION
//print require trace
#undef DEBUG_PRINT_CODE
#endif

#if !LOG_MODE
#undef LOG_COMPILE_TIMING
#undef LOG_EXECUTE_TIMING
#undef LOG_MALLOC_INFO
#undef LOG_BIPMS
#endif