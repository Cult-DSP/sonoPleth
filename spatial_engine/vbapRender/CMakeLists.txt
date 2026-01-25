cmake_minimum_required(VERSION 3.12)

# Workaround for allolib's old CMake version requirement
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

project(sonoPleth_vbap_render)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Disable unnecessary AlloLib features
set(ALLOLIB_BUILD_EXAMPLES OFF CACHE BOOL "Don't build AlloLib examples")
set(ALLOLIB_BUILD_TESTS OFF CACHE BOOL "Don't build AlloLib tests")
set(AL_VERBOSE_OUTPUT OFF CACHE BOOL "Disable verbose output")

# AlloLib is in thirdparty at project root (two levels up from spatial_engine/vbapRender)
add_subdirectory(
    ${CMAKE_CURRENT_SOURCE_DIR}/../../thirdparty/allolib
    ${CMAKE_CURRENT_BINARY_DIR}/allolib-build
)

# Source files are in spatial_engine/src (one level up from vbapRender)
# VBAPRenderer is in vbap_src subdirectory
add_executable(sonoPleth_vbap_render
    ../src/main.cpp
    ../src/vbap_src/VBAPRenderer.cpp
    ../src/JSONLoader.cpp
    ../src/LayoutLoader.cpp
    ../src/WavUtils.cpp
)

# Include AlloLib headers and vbap_src for VBAPRenderer.hpp
target_include_directories(sonoPleth_vbap_render PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../thirdparty/allolib/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/vbap_src
)

target_link_libraries(sonoPleth_vbap_render
    al
    Gamma
)
