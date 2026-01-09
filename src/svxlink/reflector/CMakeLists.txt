# Find the popt library
find_package(Popt REQUIRED)
set(LIBS ${LIBS} ${POPT_LIBRARIES})
include_directories(${POPT_INCLUDE_DIRS})
add_definitions(${POPT_DEFINITIONS})

# Find the GCrypt library
#find_package(GCrypt REQUIRED)
#set(LIBS ${LIBS} ${GCRYPT_LIBRARIES})
#include_directories(${GCRYPT_INCLUDE_DIRS})
#add_definitions(${GCRYPT_DEFINITIONS})

# Find jsoncpp library
#find_package(jsoncpp REQUIRED)
#get_target_property(JSONCPP_INC_PATH jsoncpp_lib INTERFACE_INCLUDE_DIRECTORIES)
#include_directories(${JSONCPP_INC_PATH})
#set(LIBS ${LIBS} jsoncpp_lib)
pkg_check_modules (JSONCPP REQUIRED jsoncpp)
include_directories(${JSONCPP_INCLUDE_DIRS})
set(LIBS ${LIBS} ${JSONCPP_LIBRARIES})

# Add project libraries
set(LIBS asynccpp asyncaudio asynccore svxmisc ${LIBS})

# Find libcurl libraries
pkg_check_modules(CURL REQUIRED libcurl)
include_directories(${CURL_INCLUDE_DIRS})
set(LIBS ${LIBS} ${CURL_LIBRARIES})

# Find Paho MQTT C++
find_library(PAHO_MQTT_CPP_LIB paho-mqttpp3 NAMES paho-mqttpp3 paho-mqttpp)
find_library(PAHO_MQTT_C_LIB paho-mqtt3as NAMES paho-mqtt3as paho-mqtt3a)

if(PAHO_MQTT_CPP_LIB AND PAHO_MQTT_C_LIB)
  add_definitions(-DHAVE_MQTT)
  set(LIBS ${LIBS} ${PAHO_MQTT_CPP_LIB} ${PAHO_MQTT_C_LIB})
  message(STATUS "Found Paho MQTT C++: ${PAHO_MQTT_CPP_LIB}")
else()
  message(STATUS "Paho MQTT C++ not found. MQTT support disabled.")
endif()

# Build the executable
add_executable(svxreflector
  svxreflector.cpp Reflector.cpp ReflectorClient.cpp TGHandler.cpp
  RemoteUserAuth.cpp
)
target_link_libraries(svxreflector ${LIBS} ${CURL_LIBRARIES})
target_compile_features(svxreflector PRIVATE cxx_std_17)
set_target_properties(svxreflector PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${RUNTIME_OUTPUT_DIRECTORY}
)

# Generate config file with correct paths
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/svxreflector.conf.in
  ${CMAKE_CURRENT_BINARY_DIR}/svxreflector.conf
  @ONLY
  )
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/ca-hook.py.in
  ${CMAKE_CURRENT_BINARY_DIR}/ca-hook.py
  @ONLY
  )

# Install targets
install(TARGETS svxreflector DESTINATION ${BIN_INSTALL_DIR})
install_if_not_exists(${CMAKE_CURRENT_BINARY_DIR}/svxreflector.conf
  ${SVX_SYSCONF_INSTALL_DIR}
  )
install_if_not_exists(ca-hook.yaml ${SVX_SYSCONF_INSTALL_DIR})
install(PROGRAMS svxreflector-status
  DESTINATION ${BIN_INSTALL_DIR}
  )
install(PROGRAMS ${CMAKE_CURRENT_BINARY_DIR}/ca-hook.py
  DESTINATION ${SVX_SHARE_INSTALL_DIR}
  )
