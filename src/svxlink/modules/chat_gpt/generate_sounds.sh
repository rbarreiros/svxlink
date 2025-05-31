#!/bin/bash

# Script to generate sound files for ChatGPT module
# Run with: sudo ./generate_sounds.sh

SOUNDS_DIR="/usr/share/svxlink/sounds/ChatGPT"

echo "Creating ChatGPT module sound files..."

# Create directory
mkdir -p "$SOUNDS_DIR"
cd "$SOUNDS_DIR"

# Function to create sound file
create_sound() {
    local filename="$1"
    local text="$2"
    
    echo "Creating $filename.raw..."
    espeak -w "${filename}.wav" "$text" 2>/dev/null
    
    if [ -f "${filename}.wav" ]; then
        sox "${filename}.wav" -r 16000 -c 1 -t raw "${filename}.raw" 2>/dev/null
        rm "${filename}.wav"
        echo "  ✓ Created ${filename}.raw"
    else
        echo "  ✗ Failed to create ${filename}.raw"
    fi
}

# Create all sound files
create_sound "module_activated" "ChatGPT module activated"
create_sound "help" "ChatGPT module. Press 1 to start voice request, hash to exit"
create_sound "ready_to_record" "Ready to record. Press PTT and speak your question"
create_sound "recording_started" "Recording started"
create_sound "recording_stopped" "Recording stopped"
create_sound "processing_request" "Processing your request, please wait"
create_sound "ready_for_next" "Ready for next request"
create_sound "error_occurred" "An error occurred, please try again"
create_sound "operation_timeout" "Operation timed out"

echo "Sound file generation complete!"
echo "Files created in: $SOUNDS_DIR"

# Set appropriate permissions
chown -R svxlink:svxlink "$SOUNDS_DIR" 2>/dev/null || true
chmod -R 644 "$SOUNDS_DIR"/*.raw 2>/dev/null || true

echo "Permissions set for svxlink user"

# File: Add to src/svxlink/modules/CMakeLists.txt
# Add this section to the existing CMakeLists.txt file:

# ChatGPT module
find_package(PkgConfig)
if(PKG_CONFIG_FOUND)
  pkg_check_modules(JSONCPP jsoncpp)
endif()
find_package(CURL)

if(CURL_FOUND)
  set(MODULECHATGPT_SOURCES chatgpt/ModuleChatGPT.cpp)
  set(MODULECHATGPT_HEADERS chatgpt/ModuleChatGPT.h)
  add_library(ModuleChatGPT MODULE ${MODULECHATGPT_SOURCES})
  set_target_properties(ModuleChatGPT PROPERTIES PREFIX "")
  target_link_libraries(ModuleChatGPT ${LIBS} ${CURL_LIBRARIES})
  
  # Include directories for SVXLink headers
  target_include_directories(ModuleChatGPT PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src/async/core
    ${CMAKE_SOURCE_DIR}/src/async/audio
    ${CMAKE_SOURCE_DIR}/src/svxlink/svxlink
  )
  
  if(CURL_INCLUDE_DIRS)
    target_include_directories(ModuleChatGPT PRIVATE ${CURL_INCLUDE_DIRS})
  endif()
  
  if(JSONCPP_FOUND)
    target_link_libraries(ModuleChatGPT ${JSONCPP_LIBRARIES})
    target_compile_definitions(ModuleChatGPT PRIVATE HAVE_JSONCPP)
    if(JSONCPP_INCLUDE_DIRS)
      target_include_directories(ModuleChatGPT PRIVATE ${JSONCPP_INCLUDE_DIRS})
    endif()
  endif()
  
  target_compile_definitions(ModuleChatGPT PRIVATE HAVE_CURL)
  
  install(TARGETS ModuleChatGPT DESTINATION ${SVX_MODULE_INSTALL_DIR})
  install(FILES chatgpt/ModuleChatGPT.conf 
          DESTINATION ${SVX_SYSCONF_INSTALL_DIR}/svxlink.d)
else()
  message("-- ChatGPT module will not be built: libcurl not found")
endif()