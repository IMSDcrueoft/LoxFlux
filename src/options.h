/*
 * MIT License
 * Copyright (c) 2025 IM&SD (https://github.com/IMSDcrueoft)
 * See LICENSE file in the root directory for full license text.
*/
#pragma once
// switch on this to use debug
#define DEBUG_MODE 1
// log the compiled codes
#define DEBUG_PRINT_CODE 1
// this is too slow and print too much.
#define DEBUG_TRACE_EXECUTION 0
// stress gc
#define DEBUG_STRESS_GC 0
// log gc info
#define DEBUG_LOG_GC 0

// switch on this to use log
#define LOG_MODE 1
// log compile time
#define LOG_COMPILE_TIMING 1
// log execute time
#define LOG_EXECUTE_TIMING 1
// use this to check memory allocate and leak
#define LOG_EACH_MALLOC_INFO 0
// use this to log gc info
#define LOG_GC_RESULT 0
// log memory info after execute
#define LOG_MALLOC_INFO 1
// kilo instructions per second
#define LOG_KIPS 1

#if !DEBUG_MODE
#undef DEBUG_PRINT_CODE
#undef DEBUG_TRACE_EXECUTION
#undef DEBUG_STRESS_GC
#undef DEBUG_LOG_GC
#endif

#if !LOG_MODE
#undef LOG_COMPILE_TIMING
#undef LOG_EXECUTE_TIMING
#undef LOG_EACH_MALLOC_INFO
#undef LOG_GC_RESULT
#undef LOG_MALLOC_INFO
#undef LOG_KIPS
#endif