#.rst:
# FindRnnoise
# -----------
# Find the RNNoise noise suppression library and include directory
#
#  Rnnoise_FOUND         - Set to true if the rnnoise library is found
#  Rnnoise_INCLUDE_DIRS  - The directory where rnnoise.h can be found
#  Rnnoise_LIBRARIES     - Libraries to link with to use rnnoise
#  Rnnoise_VERSION       - Full version string (if available via pkg-config)

# Use pkg-config (rnnoise installs rnnoise.pc) to get hints about where the
# header and library are. Fall back to the standard search paths otherwise.
find_package(PkgConfig QUIET)
if(PKG_CONFIG_FOUND)
  pkg_check_modules(PC_Rnnoise QUIET rnnoise)
endif()

find_path(Rnnoise_INCLUDE_DIR
  NAMES rnnoise.h
  HINTS ${PC_Rnnoise_INCLUDE_DIRS}
  DOC "RNNoise include directory"
)

find_library(Rnnoise_LIBRARY
  NAMES rnnoise
  HINTS ${PC_Rnnoise_LIBRARY_DIRS}
  DOC "RNNoise library path"
)

if(PC_Rnnoise_VERSION)
  set(Rnnoise_VERSION ${PC_Rnnoise_VERSION})
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Rnnoise
  REQUIRED_VARS Rnnoise_LIBRARY Rnnoise_INCLUDE_DIR
  VERSION_VAR Rnnoise_VERSION
)

if(Rnnoise_FOUND)
  set(Rnnoise_LIBRARIES ${Rnnoise_LIBRARY})
  set(Rnnoise_INCLUDE_DIRS ${Rnnoise_INCLUDE_DIR})
  set(Rnnoise_DEFINITIONS ${PC_Rnnoise_CFLAGS_OTHER})
endif()

mark_as_advanced(Rnnoise_INCLUDE_DIR Rnnoise_LIBRARY)
