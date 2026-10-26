cmake_minimum_required(VERSION 3.16)

# ---------------------------------------------------------------------------
# loxFlux - a Lox dialect interpreter (VM)
# ---------------------------------------------------------------------------
project(loxFlux
  VERSION 0.1.0
  DESCRIPTION "loxFlux - a fast Lox dialect interpreter"
  LANGUAGES C CXX)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_CXX_EXTENSIONS OFF)

# Options -------------------------------------------------------------------
option(LOXFLUX_BUILD_SHARED "Build shared library" OFF)

# Compiler flag selection ---------------------------------------------------
# Mimics the flags used in loxFlux.vcxproj so CMake builds behave like the
# Visual Studio workflow where possible.
if(MSVC)
  # Exceptions are disabled project-wide in the original MSBuild config.
  # C and C++ are handled separately so no conflicting /EH flags remain.
  add_compile_options($<$<COMPILE_LANGUAGE:C>:/EHs-c->)
  add_compile_options($<$<COMPILE_LANGUAGE:CXX>:/EH->)
  # AdvancedVectorExtensions2 == /arch:AVX2 (matches loxFlux.vcxproj; MSVC
  # does not know an /arch:SSE4.2 value - it was silently ignored with D9002)
  add_compile_options(/arch:AVX2)
else()
  add_compile_options(-fno-exceptions)
endif()

# Warnings (keep parity with WarningLevel Level3)
if(MSVC)
  add_compile_options(/W3)
elseif(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
  add_compile_options(-Wall -Wextra)
endif()

# Debug / release build type ------------------------------------------------
# For single-config generators (Makefiles/Ninja) default to Release unless a
# build type is already set (e.g. by CMakePresets). Multi-config generators
# (Visual Studio, Ninja Multi-Config) manage this via CMAKE_CONFIGURATION_TYPES.
if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# xxHash is header-only (XXH_INLINE_ALL) -------------------------------------
add_library(xxhash INTERFACE)
target_include_directories(xxhash INTERFACE ${CMAKE_SOURCE_DIR}/third-party/xxHash)

# mimalloc -------------------------------------------------------------------
# Vendored from source (third-party/mimalloc) and built through its own
# CMakeLists; Debug/Release output naming is handled by upstream. Opt out
# with -DLOXFLUX_USE_MIMALLOC=OFF to use the standard library allocator.
option(LOXFLUX_USE_MIMALLOC "Build and use the vendored mimalloc allocator" ON)

set(LOXFLUX_ALLOC_MIMALLOC OFF)
if(LOXFLUX_USE_MIMALLOC)
  set(MI_BUILD_SHARED OFF)
  set(MI_BUILD_OBJECT OFF)
  set(MI_BUILD_TESTS OFF)
  add_subdirectory(third-party/mimalloc)
  set(LOXFLUX_ALLOC_MIMALLOC ON)
  message(STATUS "mimalloc: building vendored source -> using mimalloc allocator")
else()
  message(STATUS "mimalloc disabled by LOXFLUX_USE_MIMALLOC=OFF -> using the standard library allocator")
endif()

# Core library --------------------------------------------------------------
set(LOXFLUX_CORE_SOURCES
  src/file.c
  src/hash.c
  src/chunk.c
  src/compiler.c
  src/debug.c
  src/entrance.c
  src/gc.c
  src/lineArray.c
  src/memory.c
  src/nativeArray.c
  src/nativeCtor.c
  src/nativeGlobal.c
  src/nativeMath.c
  src/nativeObject.c
  src/nativeString.c
  src/nativeSystem.c
  src/nativeTime.c
  src/binaryTable.c
  src/object.c
  src/scanner.c
  src/stringTable.c
  src/table.c
  src/timer.c
  src/value.c
  src/vm.c
  src/xoshiro256.c
  third-party/memAllocator/arena_bump.c
  third-party/memAllocator/palloc_os.c
  third-party/memAllocator/bits.c
)

add_library(loxFluxCore STATIC ${LOXFLUX_CORE_SOURCES})
target_include_directories(loxFluxCore PUBLIC
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/third-party/xxHash
  ${CMAKE_SOURCE_DIR}/third-party/memAllocator
)
target_link_libraries(loxFluxCore PUBLIC xxhash)

# Always define LOXFLUX_USE_MIMALLOC (1/0) so allocator.h selects the same
# allocator the build system configured, regardless of the platform default.
# Based on LOXFLUX_ALLOC_MIMALLOC (mimalloc actually found and enabled), not
# the user option alone, so the macro matches what gets linked.
target_compile_definitions(loxFluxCore PUBLIC
  LOXFLUX_USE_MIMALLOC=$<IF:$<BOOL:${LOXFLUX_ALLOC_MIMALLOC}>,1,0>)
if(LOXFLUX_ALLOC_MIMALLOC)
  target_link_libraries(loxFluxCore PUBLIC mimalloc)
endif()

# Link-Time Optimization -----------------------------------------------------
# Enabled for Release/RelWithDebInfo only; MSVC uses /LTCG, Clang/GCC use -flto.
option(LOXFLUX_ENABLE_LTO "Enable link-time optimization in release builds" ON)
if(LOXFLUX_ENABLE_LTO)
  include(CheckIPOSupported)
  check_ipo_supported(RESULT loxflux_ipo_supported OUTPUT loxflux_ipo_output)
  if(loxflux_ipo_supported)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO ON)
    message(STATUS "LTO: enabled for Release/RelWithDebInfo")
  else()
    message(STATUS "LTO: not supported by this toolchain -> disabled (${loxflux_ipo_output})")
  endif()
endif()

# Executable ----------------------------------------------------------------
add_executable(loxFlux main.c)
target_link_libraries(loxFlux PRIVATE loxFluxCore)

# Release symbol stripping ---------------------------------------------------
# Keep function names out of the shipped binary: MSVC gets /DEBUG:NONE (no
# PDB, no COFF symbol table), GCC/Clang get a post-build `strip --strip-all`.
if(MSVC)
  target_link_options(loxFlux PRIVATE $<$<CONFIG:Release>:/DEBUG:NONE>)
else()
  add_custom_command(TARGET loxFlux POST_BUILD
    COMMAND $<$<CONFIG:Release>:${CMAKE_STRIP}> --strip-all $<TARGET_FILE:loxFlux>
    COMMENT "Stripping release binary")
endif()
