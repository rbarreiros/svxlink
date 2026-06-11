###############################################################################
# CMakeLists.txt – parrotclient
###############################################################################

set(PARROTCLIENT_SRCS
  parrotclient.cpp
  ParrotClient.cpp
)

# popt (command-line parsing)
find_package(Popt REQUIRED)
set(LIBS ${LIBS} ${POPT_LIBRARIES})
include_directories(${POPT_INCLUDE_DIRS})
add_definitions(${POPT_DEFINITIONS})

# GSM codec (required by asyncaudio)
find_package(GSM REQUIRED)
set(LIBS ${LIBS} ${GSM_LIBRARY})
include_directories(${GSM_INCLUDE_DIR})

# dl - Linux only
if(${CMAKE_SYSTEM_NAME} MATCHES "Linux")
  find_package(DL REQUIRED)
  set(LIBS ${LIBS} ${DL_LIBRARIES})
  include_directories(${DL_INCLUDES})
endif()

# libcurl (needed by ReflectorClient for CA bundle download)
find_package(CURL REQUIRED)
if(CURL_FOUND)
  set(LIBS ${LIBS} ${CURL_LIBRARIES} ${CURL_LIBRARY})
  include_directories(${CURL_INCLUDE_DIRS})
else()
  message(FATAL_ERROR "Could not find the CURL library and development files.")
endif()

# jsoncpp (used by ReflectorClient)
pkg_check_modules(JSONCPP REQUIRED jsoncpp)
include_directories(${JSONCPP_INCLUDE_DIRS})
set(LIBS ${LIBS} ${JSONCPP_LIBRARIES})

# Project libraries
set(LIBS reflectorclient asynccpp asyncaudio asynccore svxmisc ${LIBS})

# Include paths
include_directories(
  ${CMAKE_CURRENT_SOURCE_DIR}/../reflector        # ReflectorMsg.h
  ${CMAKE_CURRENT_SOURCE_DIR}/../reflectorclient  # ReflectorClient.h
)

# Build
add_executable(parrotclient ${PARROTCLIENT_SRCS} ${VERSION_DEPENDS})
target_link_libraries(parrotclient ${LIBS})
set_target_properties(parrotclient PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${RUNTIME_OUTPUT_DIRECTORY}
)

# Generate config file with correct install paths substituted
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/parrotclient.conf.in
  ${CMAKE_CURRENT_BINARY_DIR}/parrotclient.conf
  @ONLY
)

# Install
install(TARGETS parrotclient DESTINATION ${BIN_INSTALL_DIR})
install_if_not_exists(${CMAKE_CURRENT_BINARY_DIR}/parrotclient.conf
  ${SVX_SYSCONF_INSTALL_DIR}
)
