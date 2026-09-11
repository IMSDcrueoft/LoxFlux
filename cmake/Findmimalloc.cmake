# Findmimalloc.cmake
#
# Find the mimalloc allocator library.
#
# The project ships a prebuilt static library on Windows under
#   third-party/mimalloc/mimalloc-static.lib      (Release)
#   third-party/mimalloc/mimalloc-static-debug.lib (Debug)
# and headers under third-party/mimalloc/include.
# On other platforms we first look for a system-installed mimalloc, and if not
# found the target stays unset so that the caller can fall back to the
# standard library allocator.
#
# Imports the target `mimalloc::mimalloc` when found and sets:
#   mimalloc_FOUND
#
# Optional variables:
#   LOXFLUX_MIMALLOC_DIR  - root of a local mimalloc distribution

if(TARGET mimalloc::mimalloc)
  set(mimalloc_FOUND TRUE)
  return()
endif()

set(_mimalloc_search_paths)
if(LOXFLUX_MIMALLOC_DIR)
  list(APPEND _mimalloc_search_paths
    "${LOXFLUX_MIMALLOC_DIR}"
    "${LOXFLUX_MIMALLOC_DIR}/lib"
  )
endif()

# Project-local prebuilt library (Windows)
list(APPEND _mimalloc_search_paths
  "${CMAKE_CURRENT_SOURCE_DIR}/third-party/mimalloc"
)

set(_mimalloc_inc_dirs)
if(LOXFLUX_MIMALLOC_DIR)
  list(APPEND _mimalloc_inc_dirs "${LOXFLUX_MIMALLOC_DIR}/include")
endif()
list(APPEND _mimalloc_inc_dirs
  "${CMAKE_CURRENT_SOURCE_DIR}/third-party/mimalloc/include"
)

find_path(mimalloc_INCLUDE_DIR mimalloc.h PATHS ${_mimalloc_inc_dirs} NO_DEFAULT_PATH)
find_path(mimalloc_INCLUDE_DIR_SYSTEM mimalloc.h)

set(_mimalloc_lib_var mimalloc_LIBRARY)
set(_mimalloc_components Release Debug)
set(_mimalloc_lib_win_release "mimalloc-static")
set(_mimalloc_lib_win_debug   "mimalloc-static-debug")

if(WIN32)
  # Prefer the prebuilt static libraries next to the headers, per build type.
  find_library(mimalloc_LIBRARY_DEBUG
    NAMES ${_mimalloc_lib_win_debug}
    PATHS ${_mimalloc_search_paths} NO_DEFAULT_PATH)
  find_library(mimalloc_LIBRARY_RELEASE
    NAMES ${_mimalloc_lib_win_release}
    PATHS ${_mimalloc_search_paths} NO_DEFAULT_PATH)

  if(mimalloc_LIBRARY_RELEASE)
    set(_mimalloc_release_found TRUE)
  endif()
  if(mimalloc_LIBRARY_DEBUG)
    set(_mimalloc_debug_found TRUE)
  endif()
else()
  find_library(mimalloc_LIBRARY NAMES mimalloc)
endif()

include(FindPackageHandleStandardArgs)
if(WIN32)
  find_package_handle_standard_args(mimalloc
    REQUIRED_VARS mimalloc_INCLUDE_DIR _mimalloc_release_found _mimalloc_debug_found)
else()
  find_package_handle_standard_args(mimalloc
    REQUIRED_VARS mimalloc_INCLUDE_DIR mimalloc_LIBRARY)
endif()

if(mimalloc_FOUND)
  add_library(mimalloc::mimalloc UNKNOWN IMPORTED)
  set_target_properties(mimalloc::mimalloc PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${mimalloc_INCLUDE_DIR}"
    IMPORTED_LOCATION "${mimalloc_LIBRARY}")
  if(WIN32 AND mimalloc_LIBRARY_DEBUG AND mimalloc_LIBRARY_RELEASE)
    set_target_properties(mimalloc::mimalloc PROPERTIES
      IMPORTED_CONFIGURATIONS "Debug;Release"
      IMPORTED_LOCATION_DEBUG   "${mimalloc_LIBRARY_DEBUG}"
      IMPORTED_LOCATION_RELEASE "${mimalloc_LIBRARY_RELEASE}"
      MAP_IMPORTED_CONFIG_RELWITHDEBINFO Release
      MAP_IMPORTED_CONFIG_MINSIZEREL Release)
  endif()
  set(mimalloc_FOUND TRUE)
endif()

unset(_mimalloc_search_paths)
unset(_mimalloc_inc_dirs)
unset(_mimalloc_lib_var)
unset(_mimalloc_components)
unset(_mimalloc_lib_win_release)
unset(_mimalloc_lib_win_debug)
unset(_mimalloc_release_found)
unset(_mimalloc_debug_found)
