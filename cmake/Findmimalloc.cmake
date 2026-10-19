# Findmimalloc.cmake
#
# Find the mimalloc allocator library.
#
# The project ships prebuilt static libraries on Windows under
#   third-party/mimalloc/mimalloc-x64.lib        (Release, x64)
#   third-party/mimalloc/mimalloc-x64-debug.lib  (Debug,   x64)
#   third-party/mimalloc/mimalloc-x86.lib        (Release, Win32)
#   third-party/mimalloc/mimalloc-x86-debug.lib  (Debug,   Win32)
# and headers under third-party/mimalloc/include. The architecture is chosen
# from CMAKE_SIZEOF_VOID_P. On other platforms (or when the prebuilt library
# is not found) we fall back to a system-installed mimalloc; if that is not
# found either the target stays unset so that the caller can fall back to the
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
if(WIN32)
  if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(_mimalloc_lib_win_release "mimalloc-x64")
    set(_mimalloc_lib_win_debug   "mimalloc-x64-debug")
  else()
    set(_mimalloc_lib_win_release "mimalloc-x86")
    set(_mimalloc_lib_win_debug   "mimalloc-x86-debug")
  endif()

  find_library(mimalloc_LIBRARY_DEBUG
    NAMES ${_mimalloc_lib_win_debug}
    PATHS ${_mimalloc_search_paths} NO_DEFAULT_PATH)
  find_library(mimalloc_LIBRARY_RELEASE
    NAMES ${_mimalloc_lib_win_release}
    PATHS ${_mimalloc_search_paths} NO_DEFAULT_PATH)

  # keep IMPORTED_LOCATION non-empty: single-config generators use the
  # <CONFIG>-mapped location, but the base property must still be valid
  if(mimalloc_LIBRARY_RELEASE)
    set(mimalloc_LIBRARY "${mimalloc_LIBRARY_RELEASE}")
  endif()
else()
  find_library(mimalloc_LIBRARY NAMES mimalloc)
endif()

if(mimalloc_LIBRARY_RELEASE)
  set(_mimalloc_release_found TRUE)
endif()
if(mimalloc_LIBRARY_DEBUG)
  set(_mimalloc_debug_found TRUE)
endif()

include(FindPackageHandleStandardArgs)
if(WIN32)
  find_package_handle_standard_args(mimalloc
    REQUIRED_VARS mimalloc_INCLUDE_DIR _mimalloc_release_found)
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
unset(_mimalloc_lib_win_release)
unset(_mimalloc_lib_win_debug)
unset(_mimalloc_release_found)
unset(_mimalloc_debug_found)
