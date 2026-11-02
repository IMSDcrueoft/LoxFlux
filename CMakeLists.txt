cmake_minimum_required(VERSION 3.21)

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
option(LOXFLUX_ENABLE_LTO "Enable link-time optimization in release builds" ON)
set(LOXFLUX_LTO_MODE "thin" CACHE STRING
  "Clang LTO mode: thin or full (GCC uses full -flto=auto where supported)")
set_property(CACHE LOXFLUX_LTO_MODE PROPERTY STRINGS thin full)
string(TOLOWER "${LOXFLUX_LTO_MODE}" _loxflux_lto_mode)
if(NOT _loxflux_lto_mode MATCHES "^(thin|full)$")
  message(FATAL_ERROR "LOXFLUX_LTO_MODE must be 'thin' or 'full' (got '${LOXFLUX_LTO_MODE}')")
endif()
set(LOXFLUX_LTO_MODE "${_loxflux_lto_mode}" CACHE STRING
  "Clang LTO mode: thin or full (GCC uses full -flto=auto where supported)" FORCE)

set(LOXFLUX_STACK_PROTECTOR "STRONG" CACHE STRING
  "Stack protector: STRONG (default), ON (-fstack-protector) or OFF")
set_property(CACHE LOXFLUX_STACK_PROTECTOR PROPERTY STRINGS OFF ON STRONG)
string(TOUPPER "${LOXFLUX_STACK_PROTECTOR}" _loxflux_stack_protector)
if(NOT _loxflux_stack_protector MATCHES "^(OFF|ON|STRONG)$")
  message(FATAL_ERROR "LOXFLUX_STACK_PROTECTOR must be OFF, ON, or STRONG (got '${LOXFLUX_STACK_PROTECTOR}')")
endif()
set(LOXFLUX_STACK_PROTECTOR "${_loxflux_stack_protector}" CACHE STRING
  "Stack protector: STRONG (default), ON (-fstack-protector) or OFF" FORCE)

# Compiler flag selection ---------------------------------------------------
# Mimics the flags used in loxFlux.vcxproj so CMake builds behave like the
# Visual Studio workflow where possible.
if(MSVC)
  # Exceptions are disabled project-wide in the original MSBuild config.
  # C and C++ are handled separately so no conflicting /EH flags remain.
  add_compile_options($<$<COMPILE_LANGUAGE:C>:/EHs-c->)
  add_compile_options($<$<COMPILE_LANGUAGE:CXX>:/EH->)
else()
  add_compile_options(-fno-exceptions)
endif()

# Stack protector / stack checking ------------------------------------------
# Apple Clang defaults to -fstack-protector/-fstack-check; GCC to neither.
# Make the choice explicit so builds are comparable. -fno-stack-check is always
# added because the two compilers' stack checks are not equivalent hardening.
if(MSVC)
  if(LOXFLUX_STACK_PROTECTOR STREQUAL "OFF")
    add_compile_options(/GS-)
  else()
    add_compile_options(/GS)
  endif()
else()
  if(LOXFLUX_STACK_PROTECTOR STREQUAL "OFF")
    add_compile_options(-fno-stack-protector)
  elseif(LOXFLUX_STACK_PROTECTOR STREQUAL "STRONG")
    add_compile_options(-fstack-protector-strong)
  else()
    add_compile_options(-fstack-protector)
  endif()
  if(CMAKE_C_COMPILER_ID MATCHES "Clang")
    add_compile_options(-fno-stack-check)
  endif()
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
target_include_directories(xxhash INTERFACE ${PROJECT_SOURCE_DIR}/third-party/xxHash)

# Link-Time Optimization -----------------------------------------------------
# IPO variables must be set before targets are created (CMake initializes the
# target property at creation). Set them after mimalloc so the vendored
# allocator stays non-LTO: no measurable gain and interposition risk with
# MI_OVERRIDE.
set(LOXFLUX_LTO_EFFECTIVE OFF)
if(LOXFLUX_ENABLE_LTO)
  if(APPLE AND CMAKE_C_COMPILER_ID STREQUAL "GNU")
    # Apple ld cannot load GCC's LTO plugin; it would copy __GNU_LTO sections
    # into the output without optimizing.
    message(STATUS "LTO: effective OFF (GCC on macOS; Apple ld cannot process GCC LTO objects)")
  else()
    if(LOXFLUX_LTO_MODE STREQUAL "full" AND CMAKE_C_COMPILER_ID MATCHES "Clang")
      # Force full LTO so Clang matches GCC's -flto=auto.
      set(CMAKE_C_COMPILE_OPTIONS_IPO "-flto")
      set(CMAKE_C_LINK_OPTIONS_IPO "-flto")
      set(CMAKE_CXX_COMPILE_OPTIONS_IPO "-flto")
      set(CMAKE_CXX_LINK_OPTIONS_IPO "-flto")
    endif()
    include(CheckIPOSupported)
    check_ipo_supported(RESULT _loxflux_ipo_supported OUTPUT loxflux_ipo_output
      LANGUAGES C)
    if(_loxflux_ipo_supported)
      set(LOXFLUX_LTO_EFFECTIVE ON)
      set(_loxflux_lto_mode "${LOXFLUX_LTO_MODE}")
      if(NOT CMAKE_C_COMPILER_ID MATCHES "Clang")
        set(_loxflux_lto_mode "full")
      endif()
      message(STATUS "LTO: effective ON for Release/RelWithDebInfo (mode: ${_loxflux_lto_mode}; mimalloc excluded)")
    else()
      message(STATUS "LTO: effective OFF (toolchain does not support IPO: ${loxflux_ipo_output})")
    endif()
  endif()
else()
  message(STATUS "LTO: effective OFF (LOXFLUX_ENABLE_LTO=OFF)")
endif()

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

# IPO for our own targets; mimalloc was created above and stays non-LTO.
if(LOXFLUX_LTO_EFFECTIVE)
  set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
  set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO ON)
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
  ${PROJECT_SOURCE_DIR}/src
  ${PROJECT_SOURCE_DIR}/third-party/xxHash
  ${PROJECT_SOURCE_DIR}/third-party/memAllocator
)
target_link_libraries(loxFluxCore PUBLIC xxhash)

# Keep allocator.h's macro in sync with the allocator actually linked.
target_compile_definitions(loxFluxCore PUBLIC
  LOXFLUX_USE_MIMALLOC=$<IF:$<BOOL:${LOXFLUX_ALLOC_MIMALLOC}>,1,0>)
if(LOXFLUX_ALLOC_MIMALLOC)
  # With MI_BUILD_SHARED=OFF only mimalloc-static exists.
  if(TARGET mimalloc)
    target_link_libraries(loxFluxCore PUBLIC mimalloc)
  else()
    target_link_libraries(loxFluxCore PUBLIC mimalloc-static)
  endif()
endif()

# Executable ----------------------------------------------------------------
add_executable(loxFlux main.c)
target_link_libraries(loxFlux PRIVATE loxFluxCore)

# Release symbol stripping ---------------------------------------------------
# Keep function names out of the shipped binary: MSVC gets /DEBUG:NONE (no
# PDB, no COFF symbol table); GNU strip uses --strip-all; Apple's strip does
# not know --strip-all and uses -x (drop local symbols) instead.
if(MSVC)
  target_link_options(loxFlux PRIVATE $<$<CONFIG:Release>:/DEBUG:NONE>)
elseif(APPLE)
  add_custom_command(TARGET loxFlux POST_BUILD
    COMMAND $<$<CONFIG:Release>:${CMAKE_STRIP}>
            $<$<CONFIG:Release>:-x>
            $<$<CONFIG:Release>:$<TARGET_FILE:loxFlux>>
    COMMENT "$<$<CONFIG:Release>:Stripping release symbols>")
else()
  add_custom_command(TARGET loxFlux POST_BUILD
    COMMAND $<$<CONFIG:Release>:${CMAKE_STRIP}>
            $<$<CONFIG:Release>:--strip-all>
            $<$<CONFIG:Release>:$<TARGET_FILE:loxFlux>>
    COMMENT "$<$<CONFIG:Release>:Stripping release binary>")
endif()
