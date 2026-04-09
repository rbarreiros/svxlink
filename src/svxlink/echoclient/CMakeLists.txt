###############################################################################
# CMakeLists.txt – echoclient
###############################################################################

set(ECHOCLIENT_SRCS
  echoclient.cpp
  EchoClient.cpp
)

# popt (command-line parsing)
find_package(Popt REQUIRED)
set(LIBS ${LIBS} ${POPT_LIBRARIES})
include_directories(${POPT_INCLUDE_DIRS})
add_definitions(${POPT_DEFINITIONS})

# GSM codec (required by echolib)
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

# jsoncpp (used by ReflectorClient and EchoClient)
pkg_check_modules(JSONCPP REQUIRED jsoncpp)
include_directories(${JSONCPP_INCLUDE_DIRS})
set(LIBS ${LIBS} ${JSONCPP_LIBRARIES})

# Project libraries
set(LIBS echolib reflectorclient asynccpp asyncaudio asynccore svxmisc ${LIBS})

# Include paths
include_directories(
  ${CMAKE_CURRENT_SOURCE_DIR}/../reflector        # ReflectorMsg.h
  ${CMAKE_CURRENT_SOURCE_DIR}/../reflectorclient  # ReflectorClient.h
  ${CMAKE_SOURCE_DIR}/src/echolib                 # EchoLink*.h
)

# Build
add_executable(echoclient ${ECHOCLIENT_SRCS} ${VERSION_DEPENDS})
target_link_libraries(echoclient ${LIBS})
set_target_properties(echoclient PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${RUNTIME_OUTPUT_DIRECTORY}
)

# Generate config file with correct install paths substituted
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/echoclient.conf.in
  ${CMAKE_CURRENT_BINARY_DIR}/echoclient.conf
  @ONLY
)

# Install
install(TARGETS echoclient DESTINATION ${BIN_INSTALL_DIR})
install_if_not_exists(${CMAKE_CURRENT_BINARY_DIR}/echoclient.conf
  ${SVX_SYSCONF_INSTALL_DIR}
)
