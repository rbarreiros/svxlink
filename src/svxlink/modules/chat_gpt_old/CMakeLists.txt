set(MODNAME ChatGPT)

find_package(PkgConfig)
if(PKG_CONFIG_FOUND)
  pkg_check_modules(JSONCPP jsoncpp)
  # Use pkg-config for CURL as it's more reliable
  pkg_check_modules(CURL libcurl)
endif()

# Fallback to find_package if pkg-config didn't work
if(NOT CURL_FOUND)
  find_package(CURL)
endif()

if(CURL_FOUND OR CURL_LIBRARIES)
  message("-- Building ChatGPT module with CURL support")
  
  set(MODULECHATGPT_SOURCES ModuleChatGPT.cpp)
  set(MODULECHATGPT_HEADERS ModuleChatGPT.h)
  
  add_library(ModuleChatGPT MODULE ${MODULECHATGPT_SOURCES})
  set_target_properties(ModuleChatGPT PROPERTIES PREFIX "")
  
  # Link with SVXLink libraries
  target_link_libraries(ModuleChatGPT ${LIBS})
  
  # Link CURL - try different variable names
  if(CURL_LIBRARIES)
    target_link_libraries(ModuleChatGPT ${CURL_LIBRARIES})
    message("-- ChatGPT: Using CURL_LIBRARIES: ${CURL_LIBRARIES}")
  elseif(CURL_LIBRARY)
    target_link_libraries(ModuleChatGPT ${CURL_LIBRARY})
    message("-- ChatGPT: Using CURL_LIBRARY: ${CURL_LIBRARY}")
  else()
    # Fallback to direct linking
    target_link_libraries(ModuleChatGPT curl)
    message("-- ChatGPT: Using direct curl linking")
  endif()
  
  # Include directories for SVXLink headers
  target_include_directories(ModuleChatGPT PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src/async/core
    ${CMAKE_SOURCE_DIR}/src/async/audio
    ${CMAKE_SOURCE_DIR}/src/svxlink/svxlink
  )
  
  # Add CURL include directories
  if(CURL_INCLUDE_DIRS)
    target_include_directories(ModuleChatGPT PRIVATE ${CURL_INCLUDE_DIRS})
    message("-- ChatGPT: Using CURL_INCLUDE_DIRS: ${CURL_INCLUDE_DIRS}")
  elseif(CURL_INCLUDE_DIR)
    target_include_directories(ModuleChatGPT PRIVATE ${CURL_INCLUDE_DIR})
    message("-- ChatGPT: Using CURL_INCLUDE_DIR: ${CURL_INCLUDE_DIR}")
  endif()
  
  target_compile_definitions(ModuleChatGPT PRIVATE HAVE_CURL)
  
  # Add JSON support if available
  if(JSONCPP_FOUND)
    target_link_libraries(ModuleChatGPT ${JSONCPP_LIBRARIES})
    target_compile_definitions(ModuleChatGPT PRIVATE HAVE_JSONCPP)
    if(JSONCPP_INCLUDE_DIRS)
      target_include_directories(ModuleChatGPT PRIVATE ${JSONCPP_INCLUDE_DIRS})
    endif()
    message("-- ChatGPT module: JSON support enabled")
  else()
    message("-- ChatGPT module: JSON support disabled (jsoncpp not found)")
  endif()
  
  # Install targets
  install(TARGETS ModuleChatGPT DESTINATION ${SVX_MODULE_INSTALL_DIR})
  install(FILES ModuleChatGPT.conf 
          DESTINATION ${SVX_SYSCONF_INSTALL_DIR}/svxlink.d)
          
else()
  message("-- ChatGPT module will not be built: libcurl not found")
  message("-- Install libcurl development package to enable ChatGPT module")
endif()

# Debug output to see what CMake found
message("-- CURL Debug Info:")
message("-- CURL_FOUND: ${CURL_FOUND}")
message("-- CURL_LIBRARIES: ${CURL_LIBRARIES}")
message("-- CURL_LIBRARY: ${CURL_LIBRARY}")
message("-- CURL_INCLUDE_DIRS: ${CURL_INCLUDE_DIRS}")
message("-- CURL_INCLUDE_DIR: ${CURL_INCLUDE_DIR}")
message("-- CURL_VERSION_STRING: ${CURL_VERSION_STRING}")

# Alternative simple approach - uncomment if the above doesn't work:
# set(MODULECHATGPT_SOURCES chatgpt/ModuleChatGPT.cpp)
# add_library(ModuleChatGPT MODULE ${MODULECHATGPT_SOURCES})
# set_target_properties(ModuleChatGPT PROPERTIES PREFIX "")
# target_link_libraries(ModuleChatGPT ${LIBS} curl)
# target_compile_definitions(ModuleChatGPT PRIVATE HAVE_CURL)
# target_include_directories(ModuleChatGPT PRIVATE 
#   ${CMAKE_CURRENT_SOURCE_DIR}
#   ${CMAKE_SOURCE_DIR}/src/async/core
#   ${CMAKE_SOURCE_DIR}/src/async/audio
#   ${CMAKE_SOURCE_DIR}/src/svxlink/svxlink
# )
# install(TARGETS ModuleChatGPT DESTINATION ${SVX_MODULE_INSTALL_DIR})
