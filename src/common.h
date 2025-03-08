#pragma once
#include <stddef.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdarg.h>
//check the type
#include "check.h"

typedef char* STR;
typedef const char* C_STR;
typedef void* Unknown_ptr;
typedef float float32_t;
typedef double float64_t;

#define UINT10_MAX 0x3ff
#define UINT10_COUNT 0x400
#define UINT18_MAX 0x3ffff
#define UINT24_MAX 0xffffff
#define UINT24_COUNT 0x1000000

// to use debug mode open this
#define DEBUG_MODE 0
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

#define min_2(x,y) (((x) < (y)) ? (x): (y))
#define max_2(x,y) (((x) > (y)) ? (x): (y))