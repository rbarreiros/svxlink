# Find required libraries
find_package(Popt REQUIRED)
include_directories(${POPT_INCLUDE_DIRS})
add_definitions(${POPT_DEFINITIONS})

pkg_check_modules(JSONCPP REQUIRED jsoncpp)
include_directories(${JSONCPP_INCLUDE_DIRS})

# OpenSSL is required for WebSocket SHA1 handshake
find_package(OpenSSL REQUIRED)
include_directories(${OPENSSL_INCLUDE_DIR})

# Build the executable
add_executable(svxwebbridge
  svxwebbridge.cpp
  WebBridge.cpp
  WsServer.cpp
)

target_include_directories(svxwebbridge
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src/svxlink/reflectorclient
    ${CMAKE_SOURCE_DIR}/src/svxlink/reflector
)

target_link_libraries(svxwebbridge
  reflectorclient
  asynccpp asyncaudio asynccore svxmisc
  ${POPT_LIBRARIES}
  ${JSONCPP_LIBRARIES}
  ${OPENSSL_LIBRARIES}
)

set_target_properties(svxwebbridge PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${RUNTIME_OUTPUT_DIRECTORY}
)

# Generate config file with paths substituted
configure_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/svxwebbridge.conf.in
  ${CMAKE_CURRENT_BINARY_DIR}/svxwebbridge.conf
  @ONLY
)

# Install targets
install(TARGETS svxwebbridge DESTINATION ${BIN_INSTALL_DIR})
install_if_not_exists(
  ${CMAKE_CURRENT_BINARY_DIR}/svxwebbridge.conf
  ${SVX_SYSCONF_INSTALL_DIR}
)
