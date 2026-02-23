cmake_minimum_required(VERSION 3.14)
project(sonopleth_adm_extract CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---------------------------------------------------------------------------
# libbw64 — header-only EBU BW64/RF64 container library
# Located at: thirdparty/libbw64  (git submodule)
# ---------------------------------------------------------------------------
set(LIBBW64_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../thirdparty/libbw64")

if(NOT EXISTS "${LIBBW64_DIR}/include/bw64/bw64.hpp")
    message(FATAL_ERROR
        "libbw64 headers not found at ${LIBBW64_DIR}/include/bw64/bw64.hpp\n"
        "Run:  git submodule update --init thirdparty/libbw64"
    )
endif()

# libbw64 is header-only — just add the include path
add_library(libbw64 INTERFACE)
target_include_directories(libbw64 INTERFACE "${LIBBW64_DIR}/include")

# ---------------------------------------------------------------------------
# sonopleth_adm_extract executable
# ---------------------------------------------------------------------------
add_executable(sonopleth_adm_extract main.cpp)
target_link_libraries(sonopleth_adm_extract PRIVATE libbw64)

# Ensure it lands in the build directory root (predictable path for Python)
set_target_properties(sonopleth_adm_extract PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)

# ---------------------------------------------------------------------------
# Install (optional — used if someone does `cmake --install`)
# ---------------------------------------------------------------------------
install(TARGETS sonopleth_adm_extract RUNTIME DESTINATION bin)
