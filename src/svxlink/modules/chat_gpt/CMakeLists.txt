# The name of the module without the Module prefix
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

  #string(TOUPPER MODULE_${MODNAME} VERNAME)

  # Build the plugin
  add_library(Module${MODNAME} MODULE Module${MODNAME}.cpp)
  set_target_properties(Module${MODNAME} PROPERTIES PREFIX "")
  set_property(TARGET Module${MODNAME} PROPERTY NO_SONAME 1)
  target_link_libraries(Module${MODNAME} ${LIBS})

  # Link CURL - try different variable names
  if(CURL_LIBRARIES)
    target_link_libraries(Module${MODNAME} ${CURL_LIBRARIES})
    message("-- ChatGPT: Using CURL_LIBRARIES: ${CURL_LIBRARIES}")
  elseif(CURL_LIBRARY)
    target_link_libraries(Module${MODNAME} ${CURL_LIBRARY})
    message("-- ChatGPT: Using CURL_LIBRARY: ${CURL_LIBRARY}")
  else()
    # Fallback to direct linking
    target_link_libraries(Module${MODNAME} curl)
    message("-- ChatGPT: Using direct curl linking")
  endif()

  # Add CURL include directories
  if(CURL_INCLUDE_DIRS)
    target_include_directories(Module${MODNAME} PRIVATE ${CURL_INCLUDE_DIRS})
    message("-- ChatGPT: Using CURL_INCLUDE_DIRS: ${CURL_INCLUDE_DIRS}")
  elseif(CURL_INCLUDE_DIR)
    target_include_directories(Module${MODNAME} PRIVATE ${CURL_INCLUDE_DIR})
    message("-- ChatGPT: Using CURL_INCLUDE_DIR: ${CURL_INCLUDE_DIR}")
  endif()
  
  target_compile_definitions(Module${MODNAME} PRIVATE HAVE_CURL)

  # Add JSON support if available
  if(JSONCPP_FOUND)
    target_link_libraries(Module${MODNAME} ${JSONCPP_LIBRARIES})
    target_compile_definitions(Module${MODNAME} PRIVATE HAVE_JSONCPP)
    if(JSONCPP_INCLUDE_DIRS)
      target_include_directories(Module${MODNAME} PRIVATE ${JSONCPP_INCLUDE_DIRS})
    endif()
    message("-- ChatGPT module: JSON support enabled")
  else()
    message("-- ChatGPT module: JSON support disabled (jsoncpp not found)")
  endif()

  # Install targets
  install(TARGETS Module${MODNAME} DESTINATION ${SVX_MODULE_INSTALL_DIR})

  #install(FILES ${MODNAME}.tcl DESTINATION ${SVX_SHARE_INSTALL_DIR}/events.d)
  install_if_not_exists(Module${MODNAME}.conf
    ${SVX_SYSCONF_INSTALL_DIR}/svxlink.d
  )
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

