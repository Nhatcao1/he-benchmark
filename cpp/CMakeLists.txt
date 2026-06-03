cmake_minimum_required(VERSION 3.20)

project(seal_openfhe_benchmark LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(HE_BENCHMARK_USE_SIBLING_SEAL "Build Microsoft SEAL from ../SEAL" ON)
option(HE_BENCHMARK_BUILD_OPENFHE "Build OpenFHE benchmark targets" OFF)
option(HE_BENCHMARK_LINK_OPENFHE_STATIC "Link OpenFHE static libraries" OFF)

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

if(HE_BENCHMARK_BUILD_OPENFHE)
    find_package(OpenFHE CONFIG REQUIRED)
    separate_arguments(OpenFHE_CXX_FLAGS_LIST NATIVE_COMMAND "${OpenFHE_CXX_FLAGS}")
    separate_arguments(OpenFHE_EXE_LINKER_FLAGS_LIST NATIVE_COMMAND "${OpenFHE_EXE_LINKER_FLAGS}")

    add_executable(openfhe_bfv_exact openfhe/openfhe_bfv_exact.cpp)
    target_include_directories(openfhe_bfv_exact PRIVATE
        ${OPENMP_INCLUDES}
        ${OpenFHE_INCLUDE}
        ${OpenFHE_INCLUDE}/third-party/include
        ${OpenFHE_INCLUDE}/core
        ${OpenFHE_INCLUDE}/pke
        ${OpenFHE_INCLUDE}/binfhe
    )
    target_compile_options(openfhe_bfv_exact PRIVATE ${OpenFHE_CXX_FLAGS_LIST})
    target_link_options(openfhe_bfv_exact PRIVATE ${OpenFHE_EXE_LINKER_FLAGS_LIST})
    if(UNIX AND NOT APPLE)
        target_link_options(openfhe_bfv_exact PRIVATE -Wl,--no-as-needed)
    endif()
    target_link_directories(openfhe_bfv_exact PRIVATE
        ${OpenFHE_LIBDIR}
        ${OPENMP_LIBRARIES}
    )
    if(HE_BENCHMARK_LINK_OPENFHE_STATIC)
        set(HE_BENCHMARK_OPENFHE_LIBRARIES ${OpenFHE_STATIC_LIBRARIES})
    else()
        set(HE_BENCHMARK_OPENFHE_LIBRARIES ${OpenFHE_SHARED_LIBRARIES})
    endif()
    target_link_libraries(openfhe_bfv_exact PRIVATE
        he_benchmark_common
        ${HE_BENCHMARK_OPENFHE_LIBRARIES}
    )
endif()
