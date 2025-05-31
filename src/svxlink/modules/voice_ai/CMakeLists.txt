# Module name and library name
set(MODNAME ModuleVoiceAI)
set(LIBS ${LIBS} ${SIGC_LIBRARIES} ${GSM_LIBRARIES})

# Check for Whisper.cpp dependency
find_path(WHISPER_INCLUDE_DIR whisper.h 
    PATHS 
        /usr/local/include 
        /usr/include
        ${CMAKE_INSTALL_PREFIX}/include
    DOC "Whisper.cpp include directory"
)

find_library(WHISPER_LIBRARY 
    NAMES whisper libwhisper
    PATHS 
        /usr/local/lib 
        /usr/lib
        /usr/lib64
        /usr/local/lib64
        ${CMAKE_INSTALL_PREFIX}/lib
        ${CMAKE_INSTALL_PREFIX}/lib64
    DOC "Whisper.cpp library"
)

# Check if dependencies are found
if(NOT WHISPER_INCLUDE_DIR OR NOT WHISPER_LIBRARY)
    message(WARNING "Whisper.cpp not found. ModuleVoiceAI will not be built.")
    message(STATUS "  Whisper include dir: ${WHISPER_INCLUDE_DIR}")
    message(STATUS "  Whisper library: ${WHISPER_LIBRARY}")
    message(STATUS "  Install whisper.cpp to build this module")
    return()
endif()

# Optional: Find Piper executable
find_program(PIPER_EXECUTABLE piper
    PATHS 
        /usr/local/bin
        /usr/bin
        ~/.local/bin
    DOC "Piper TTS executable"
)

if(NOT PIPER_EXECUTABLE)
    message(WARNING "Piper TTS executable not found. Limited functionality.")
    message(STATUS "  Install: pip install piper-tts")
endif()

# Add compilation definitions
add_definitions(-DHAVE_WHISPER_CPP)
if(PIPER_EXECUTABLE)
    add_definitions(-DHAVE_PIPER_TTS)
    add_definitions(-DPIPER_EXECUTABLE="${PIPER_EXECUTABLE}")
endif()

# Include directories
include_directories(${WHISPER_INCLUDE_DIR})

# Source files
set(LIBSRC
    ModuleVoiceAI.cpp
    WhisperEngine.cpp
    PiperEngine.cpp
    IntentionProcessor.cpp
    AudioBuffer.cpp
)

# Add the module library
add_library(${MODNAME} SHARED ${LIBSRC})

# Set library properties
set_target_properties(${MODNAME} PROPERTIES PREFIX "")

# Set version if available, otherwise use defaults
#if(DEFINED SVXLINK_VERSION AND NOT "${SVXLINK_VERSION}" STREQUAL "")
#    set_target_properties(${MODNAME} PROPERTIES
#        VERSION ${SVXLINK_VERSION}
#        SOVERSION 1
#    )
#else()
#    set_target_properties(${MODNAME} PROPERTIES
#        VERSION 1.0.0
#        SOVERSION 1
#    )
#endif()

string(TOUPPER MODULE_${MODNAME} VERNAME)

# Link libraries
target_link_libraries(${MODNAME} 
    ${LIBS} 
    ${WHISPER_LIBRARY}
    pthread
)

# Set C++ standard to C++11 (SVXLink compatible)
set_target_properties(${MODNAME} PROPERTIES
    CXX_STANDARD 11
    CXX_STANDARD_REQUIRED ON
)

# Compiler-specific options
if(CMAKE_COMPILER_IS_GNUCXX)
    target_compile_options(${MODNAME} PRIVATE -Wall -Wextra)
endif()

# Install the module
install(TARGETS ${MODNAME} DESTINATION ${SVX_MODULE_INSTALL_DIR})

# Install configuration template if it exists
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/ModuleVoiceAI.conf.template")
    install(FILES ModuleVoiceAI.conf.template 
        DESTINATION ${SVX_SYSCONF_INSTALL_DIR}/svxlink.d
        RENAME ModuleVoiceAI.conf
    )
endif()

# Print build information
message(STATUS "Building ModuleVoiceAI:")
message(STATUS "  Whisper include: ${WHISPER_INCLUDE_DIR}")
message(STATUS "  Whisper library: ${WHISPER_LIBRARY}")
message(STATUS "  C++ Standard: C++11")
message(STATUS "  Install directory: ${SVX_MODULE_INSTALL_DIR}")
