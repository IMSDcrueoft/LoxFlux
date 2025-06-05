/*
* MIT License
* Copyright (c) 2025 IMSDcrueoft (https://github.com/IMSDcrueoft)
* See LICENSE file in the root directory for full license text.
*/
//this file is very improtant to the performance!
#pragma once
// ==================== detect compiler ====================
#if defined(__clang__) && defined(_MSC_VER)
#define IS_CLANGCL 1     // ClangCL Windows
#elif defined(__clang__)
#define IS_CLANG 1       // Clang
#elif defined(__GNUC__)
#define IS_GCC 1         // GCC
#elif defined(_MSC_VER)
#define IS_MSVC 1        // MSVC
#endif

// ==================== hotCode ====================
#if IS_CLANGCL || IS_CLANG || IS_GCC
#define HOT_FUNCTION    __attribute__((hot))
#define COLD_FUNCTION   __attribute__((cold))
#define COMPUTE_GOTO 1
#elif IS_MSVC
#define HOT_FUNCTION    __pragma(optimize("t", on))
#define COLD_FUNCTION   __pragma(optimize("t", off))
#define COMPUTE_GOTO 0
#else
#define HOT_FUNCTION
#define COLD_FUNCTION
#define COMPUTE_GOTO 0
#endif

#undef IS_CLANGCL
#undef IS_CLANG
#undef IS_GCC
#undef IS_MSVC