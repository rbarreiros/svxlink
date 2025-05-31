set(MODNAME ChatGPT)

find_package(PkgConfig)
if(PKG_CONFIG_FOUND)
  pkg_check_modules(JSONCPP jsoncpp)
endif()

find_package(CURL)

if(CURL_FOUND)
  message("-- Building ChatGPT module")

  string(TOUPPER MODULE_${MODNAME} VERNAME)
  
  add_library(Module${MODNAME} MODULE Module${MODNAME}.cpp)
  set_target_properties(Module${MODNAME} PROPERTIES PREFIX "")
  set_property(TARGET Module${MODNAME} PROPERTY NO_SONAME 1)  

  # Link with SVXLink libraries
  target_link_libraries(Module${MODNAME} ${LIBS} ${CURL_LIBRARIES})
   
  # Add CURL support
  if(CURL_INCLUDE_DIRS)
    target_include_directories(Module${MODNAME} PRIVATE ${CURL_INCLUDE_DIRS})
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
  install(FILES Module${MODNAME}.conf 
          DESTINATION ${SVX_SYSCONF_INSTALL_DIR}/svxlink.d)
                    
else()
  message("-- ChatGPT module will not be built: libcurl not found")
  message("-- Install libcurl development package to enable ChatGPT module")
endif()
