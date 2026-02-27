# C++ source files needed to build svxplayer
set(SVXPLAYER_SRCS
  svxplayer.cpp
  SvxPlayer.cpp
  Scheduler.cpp
  ../svxlink/MsgHandler.cpp
  )

# Find the popt library
find_package(Popt REQUIRED)
set(LIBS ${LIBS} ${POPT_LIBRARIES})
include_directories(${POPT_INCLUDE_DIRS})
add_definitions(${POPT_DEFINITIONS})

# Find the GSM codec library
find_package(GSM REQUIRED)
set(LIBS ${LIBS} ${GSM_LIBRARY})
include_directories(${GSM_INCLUDE_DIR})

# Find the dl library - only for Linux, not required for FreeBSD
if(${CMAKE_SYSTEM_NAME} MATCHES "Linux")
  find_package(DL REQUIRED)
  set(LIBS ${LIBS} ${DL_LIBRARIES})
  include_directories(${DL_INCLUDES})
endif()

# Find libcurl libraries
find_package(CURL REQUIRED)
if(CURL_FOUND)
  set(LIBS ${LIBS} ${CURL_LIBRARIES})
  set(LIBS ${LIBS} ${CURL_LIBRARY})
  include_directories(${CURL_INCLUDE_DIRS})
else()
  message(FATAL_ERROR "Could not find the CURL library and development files.")
endif()

# Find jsoncpp library
pkg_check_modules(JSONCPP REQUIRED jsoncpp)
include_directories(${JSONCPP_INCLUDE_DIRS})
set(LIBS ${LIBS} ${JSONCPP_LIBRARIES})

# Add project libraries (no trx or locationinfo needed)
set(LIBS asynccpp asyncaudio asynccore svxmisc ${LIBS})

# Include paths for MsgHandler and reflector messages
include_directories(
  ${CMAKE_CURRENT_SOURCE_DIR}/../svxlink
  ${CMAKE_CURRENT_SOURCE_DIR}/../reflector
  )

# Build the executable
add_executable(svxplayer ${SVXPLAYER_SRCS} ${VERSION_DEPENDS})
target_link_libraries(svxplayer ${LIBS})
set_target_properties(svxplayer PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${RUNTIME_OUTPUT_DIRECTORY}
)

# Generate config file with correct paths
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/svxplayer.conf.in
  ${CMAKE_CURRENT_BINARY_DIR}/svxplayer.conf
  @ONLY
  )

# Install targets
install(TARGETS svxplayer DESTINATION ${BIN_INSTALL_DIR})
install_if_not_exists(${CMAKE_CURRENT_BINARY_DIR}/svxplayer.conf
  ${SVX_SYSCONF_INSTALL_DIR}
  )
