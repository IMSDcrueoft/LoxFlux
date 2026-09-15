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
  # AdvancedVectorExtensions2 == SSE4.2
  add_compile_options(/arch:SSE4.2)
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
# Only required on Windows where a prebuilt static library is shipped.
# On other platforms, fall back to the standard library allocator unless a
# system mimalloc is found.
set(CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake)

# Default: enable mimalloc when it can be found. A user may opt out with
# -DLOXFLUX_USE_MIMALLOC=OFF.
option(LOXFLUX_USE_MIMALLOC "Use mimalloc allocator if available" ON)

find_package(mimalloc QUIET)

if(mimalloc_FOUND AND LOXFLUX_USE_MIMALLOC)
  message(STATUS "mimalloc: found -> using mimalloc allocator")
elseif(mimalloc_FOUND AND NOT LOXFLUX_USE_MIMALLOC)
  message(STATUS "mimalloc: found but disabled by LOXFLUX_USE_MIMALLOC=OFF -> using the standard library allocator")
else()
  if(WIN32)
    message(WARNING "mimalloc library not found; falling back to the standard library allocator")
  else()
    message(STATUS "mimalloc: not found -> using the standard library allocator")
  endif()
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
  src/numberTable.c
  src/object.c
  src/scanner.c
  src/stringTable.c
  src/table.c
  src/timer.c
  src/value.c
  src/vm.c
  src/xoshiro256.c
)

add_library(loxFluxCore STATIC ${LOXFLUX_CORE_SOURCES})
target_include_directories(loxFluxCore PUBLIC
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/third-party/xxHash
)
target_link_libraries(loxFluxCore PUBLIC xxhash)

# Always define LOXFLUX_USE_MIMALLOC (1/0) so allocator.h selects the same
# allocator the build system configured, regardless of the platform default.
target_compile_definitions(loxFluxCore PUBLIC
  LOXFLUX_USE_MIMALLOC=$<IF:$<BOOL:${LOXFLUX_USE_MIMALLOC}>,1,0>)
if(LOXFLUX_USE_MIMALLOC)
  target_link_libraries(loxFluxCore PUBLIC mimalloc::mimalloc)
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
