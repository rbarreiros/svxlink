# Find jsoncpp library
pkg_check_modules(JSONCPP REQUIRED jsoncpp)
include_directories(${JSONCPP_INCLUDE_DIRS})

# The common reflector client library
add_library(reflectorclient STATIC
  ReflectorClient.cpp
)
target_include_directories(reflectorclient
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${JSONCPP_INCLUDE_DIRS}
)
target_link_libraries(reflectorclient
  asynccpp asyncaudio asynccore svxmisc
  ${JSONCPP_LIBRARIES}
)
