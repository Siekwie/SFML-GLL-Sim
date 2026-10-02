cmake_minimum_required(VERSION 3.16)

# Set compiler based on platform before project declaration
if(WIN32)
    # Windows: Use clang++12
    set(CMAKE_CXX_COMPILER "clang++")
    set(CMAKE_C_COMPILER "clang")
elseif(UNIX AND NOT APPLE)
    # Linux: Use g++
    set(CMAKE_CXX_COMPILER "g++")
    set(CMAKE_C_COMPILER "gcc")
endif()

project(Gates LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(GLL_BUILD_TESTS "Build the headless gll_tests executable" OFF)

# Put outputs under build/<config> so DLLs can sit beside the exe
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>")

# Language core: syntax layer, compiler, editing operations.
# No SFML / Modbus dependency, so it can be unit tested headless.
add_library(gll_core STATIC
  src/Gll.cpp
  src/Parser.cpp
  src/Edits.cpp
)
target_include_directories(gll_core PUBLIC src)

if(GLL_BUILD_TESTS)
    add_executable(gll_tests
      tests/test_main.cpp
      tests/test_edits.cpp
      tests/legacy/LegacyParser.cpp
    )
    target_link_libraries(gll_tests PRIVATE gll_core)
    target_include_directories(gll_tests PRIVATE tests/legacy)
    target_compile_definitions(gll_tests PRIVATE GLL_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
endif()

# Platform detection and SFML configuration
if(WIN32)
    # Windows: Use MSVC-ready SFML 3 drop (extracted from SFML-3.0.2-windows-vc17-64-bit.zip)
    set(SFML_ROOT "${CMAKE_SOURCE_DIR}/../downloads/SFML-vc17/SFML-3.0.2")
    set(SFML_DIR "${SFML_ROOT}/lib/cmake/SFML")
    message(STATUS "Using SFML from Windows MSVC build: ${SFML_ROOT}")
elseif(UNIX AND NOT APPLE)
    # Linux: Use system-installed SFML 3 (installed via 'sudo make install' after building from source)
    message(STATUS "Using system-installed SFML on Linux")
else()
    message(FATAL_ERROR "Unsupported platform. This project supports Windows and Linux only.")
endif()

find_package(SFML 3 REQUIRED COMPONENTS Graphics Window System)

# Handle libmodbus differently per platform
if(WIN32)
    # Windows: Build from vendored source
    add_subdirectory(vendored/libmodbus)
    set(MODBUS_LIBRARIES modbus)
elseif(UNIX AND NOT APPLE)
    # Linux: Use system-installed libmodbus
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(MODBUS REQUIRED libmodbus)
    message(STATUS "Found system libmodbus: ${MODBUS_VERSION}")
endif()

add_executable(GLLSimulator
  src/main.cpp
  src/App.cpp
  src/Document.cpp
  src/GraphView.cpp
  src/Canvas.cpp
  src/Panels.cpp
  src/Widgets.cpp
  src/Graph.cpp
  src/Sim.cpp
  src/ModbusManager.cpp
)
target_link_libraries(GLLSimulator PRIVATE gll_core)

if(WIN32)
    target_link_libraries(GLLSimulator PRIVATE SFML::Graphics SFML::Window SFML::System ${MODBUS_LIBRARIES})
elseif(UNIX AND NOT APPLE)
    target_link_libraries(GLLSimulator PRIVATE SFML::Graphics SFML::Window SFML::System ${MODBUS_LIBRARIES})
    target_include_directories(GLLSimulator PRIVATE ${MODBUS_INCLUDE_DIRS})
    target_compile_options(GLLSimulator PRIVATE ${MODBUS_CFLAGS_OTHER})
endif()

# Platform-specific post-build steps
if(WIN32)
    # Copy runtime DLLs next to the built executable so it can run from the build dir
    file(GLOB SFML_DLLS "${SFML_ROOT}/bin/*.dll")
    if(SFML_DLLS)
        add_custom_command(
            TARGET GLLSimulator POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:GLLSimulator>"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${SFML_DLLS} "$<TARGET_FILE:modbus>" "$<TARGET_FILE_DIR:GLLSimulator>"
            COMMENT "Copying DLLs to output dir"
        )
    endif()
endif()
