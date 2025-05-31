# FindWhisperCpp.cmake - Find Whisper.cpp library
#
# This module defines:
#  WHISPERCPP_FOUND - True if Whisper.cpp is found
#  WHISPERCPP_INCLUDE_DIRS - Include directories for Whisper.cpp
#  WHISPERCPP_LIBRARIES - Libraries to link against
#  WHISPERCPP_VERSION - Version of Whisper.cpp

find_path(WHISPERCPP_INCLUDE_DIR
    NAMES whisper.h
    PATHS
        /usr/local/include
        /usr/include
        /opt/local/include
        ${CMAKE_INSTALL_PREFIX}/include
    PATH_SUFFIXES whisper
)

find_library(WHISPERCPP_LIBRARY
    NAMES whisper libwhisper
    PATHS
        /usr/local/lib
        /usr/lib
        /usr/lib64
        /usr/local/lib64
        /opt/local/lib
        ${CMAKE_INSTALL_PREFIX}/lib
        ${CMAKE_INSTALL_PREFIX}/lib64
)

# Try to determine version
if(WHISPERCPP_INCLUDE_DIR AND EXISTS "${WHISPERCPP_INCLUDE_DIR}/whisper.h")
    file(STRINGS "${WHISPERCPP_INCLUDE_DIR}/whisper.h" WHISPER_VERSION_LINES
         REGEX "#define WHISPER_VERSION")
    
    if(WHISPER_VERSION_LINES)
        string(REGEX REPLACE ".*WHISPER_VERSION \"([^\"]+)\".*" "\\1" 
               WHISPERCPP_VERSION "${WHISPER_VERSION_LINES}")
    endif()
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(WhisperCpp
    REQUIRED_VARS WHISPERCPP_LIBRARY WHISPERCPP_INCLUDE_DIR
    VERSION_VAR WHISPERCPP_VERSION
)

if(WHISPERCPP_FOUND)
    set(WHISPERCPP_LIBRARIES ${WHISPERCPP_LIBRARY})
    set(WHISPERCPP_INCLUDE_DIRS ${WHISPERCPP_INCLUDE_DIR})
    
    # Create imported target
    if(NOT TARGET WhisperCpp::whisper)
        add_library(WhisperCpp::whisper UNKNOWN IMPORTED)
        set_target_properties(WhisperCpp::whisper PROPERTIES
            IMPORTED_LOCATION "${WHISPERCPP_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${WHISPERCPP_INCLUDE_DIR}"
        )
    endif()
endif()

mark_as_advanced(WHISPERCPP_INCLUDE_DIR WHISPERCPP_LIBRARY)
