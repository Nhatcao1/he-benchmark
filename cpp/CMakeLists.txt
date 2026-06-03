cmake_minimum_required(VERSION 3.20)

project(seal_openfhe_benchmark LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(HE_BENCHMARK_USE_SIBLING_SEAL "Build Microsoft SEAL from ../SEAL" ON)

# Header-only helpers shared by SEAL and future OpenFHE benchmark executables.
add_library(he_benchmark_common INTERFACE)
target_include_directories(he_benchmark_common INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/common"
)

if(HE_BENCHMARK_USE_SIBLING_SEAL)
    # Local development layout expects this benchmark repo beside a SEAL clone:
    # Viettel/SEAL and Viettel/seal-openfhe-benchmark.
    set(SEAL_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(SEAL_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(SEAL_BUILD_BENCH OFF CACHE BOOL "" FORCE)
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../SEAL" "${CMAKE_CURRENT_BINARY_DIR}/seal")
else()
    # CI or system installs can provide an already-built SEAL package instead.
    find_package(SEAL 4.1 REQUIRED)
endif()

# First exact BFV harness. It consumes he_corpus/exact/*.csv and prints one CSV
# result line per primitive operation.
add_executable(seal_bfv_exact seal/seal_bfv_exact.cpp)
target_link_libraries(seal_bfv_exact PRIVATE he_benchmark_common SEAL::seal)
