set(LIBNAME autopatch)

# ***
pkg_check_modules(DBUS dbus-1)
include_directories(${DBUS_INCLUDE_DIRS})
set(LIBS ${LIBS} ${DBUS_LIBRARIES})
set(LIBSRC PatchCord.cpp BrandMeisterBridge.cpp)
set(EXPINC PatchCord.h BrandMeisterBridge.h)
# ***

# Copy exported include files to the global include directory
foreach(incfile ${EXPINC})
  expinc(${incfile})
endforeach(incfile)

# Build a static library
add_library(${LIBNAME} STATIC ${LIBSRC})
set_target_properties(${LIBNAME} PROPERTIES OUTPUT_NAME ${LIBNAME})
target_link_libraries(${LIBNAME} ${LIBS})

# Install targets
install(TARGETS ${LIBNAME} DESTINATION ${LIB_INSTALL_DIR})
install(FILES ${EXPINC} DESTINATION ${SVX_INCLUDE_INSTALL_DIR})
