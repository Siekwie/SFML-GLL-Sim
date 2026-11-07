# libmodbus 3.1.11 (https://github.com/stephane/libmodbus, LGPL-2.1+), vendored
# for Windows builds of GLLSimulator. Builds the shared library `modbus`
# (modbus.dll) from src/; config.h is upstream's src/win32/config.h.win32.
cmake_minimum_required(VERSION 3.16)
project(libmodbus VERSION 3.1.11 LANGUAGES C)

set(LIBMODBUS_VERSION_MAJOR ${PROJECT_VERSION_MAJOR})
set(LIBMODBUS_VERSION_MINOR ${PROJECT_VERSION_MINOR})
set(LIBMODBUS_VERSION_MICRO ${PROJECT_VERSION_PATCH})
set(LIBMODBUS_VERSION ${PROJECT_VERSION})
configure_file(src/modbus-version.h.in ${CMAKE_CURRENT_BINARY_DIR}/modbus-version.h @ONLY)

add_library(modbus SHARED
    src/modbus.c
    src/modbus-data.c
    src/modbus-rtu.c
    src/modbus-tcp.c
)
target_include_directories(modbus
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_BINARY_DIR}
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_definitions(modbus PRIVATE DLLBUILD HAVE_CONFIG_H _CRT_SECURE_NO_WARNINGS)
if(WIN32)
    target_link_libraries(modbus PRIVATE ws2_32)
endif()
