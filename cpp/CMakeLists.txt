cmake_minimum_required(VERSION 3.20)

project(seal_openfhe_benchmark LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(HE_BENCHMARK_USE_SIBLING_SEAL "Build Microsoft SEAL from ../SEAL" ON)
option(HE_BENCHMARK_BUILD_OPENFHE "Build OpenFHE benchmark targets" ON)
option(HE_BENCHMARK_LINK_OPENFHE_STATIC "Link OpenFHE static libraries" OFF)

# Header-only helpers shared by SEAL and OpenFHE benchmark executables.
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

add_executable(seal_bgv_exact seal/seal_bgv_exact.cpp)
target_link_libraries(seal_bgv_exact PRIVATE he_benchmark_common SEAL::seal)

add_executable(seal_ckks seal/seal_ckks.cpp)
target_link_libraries(seal_ckks PRIVATE he_benchmark_common SEAL::seal)

add_executable(seal_bfv_depth seal/seal_exact_depth.cpp)
target_link_libraries(seal_bfv_depth PRIVATE he_benchmark_common SEAL::seal)

add_executable(seal_bgv_depth seal/seal_exact_depth.cpp)
target_compile_definitions(seal_bgv_depth PRIVATE HE_BENCHMARK_SEAL_DEPTH_BGV)
target_link_libraries(seal_bgv_depth PRIVATE he_benchmark_common SEAL::seal)

add_executable(seal_ckks_depth seal/seal_ckks_depth.cpp)
target_link_libraries(seal_ckks_depth PRIVATE he_benchmark_common SEAL::seal)

add_executable(seal_bfv_rotation seal/seal_bfv_rotation.cpp)
target_link_libraries(seal_bfv_rotation PRIVATE he_benchmark_common SEAL::seal)

add_executable(seal_bgv_rotation seal/seal_bfv_rotation.cpp)
target_compile_definitions(seal_bgv_rotation PRIVATE HE_BENCHMARK_SEAL_ROTATION_BGV)
target_link_libraries(seal_bgv_rotation PRIVATE he_benchmark_common SEAL::seal)

add_executable(seal_ckks_rotation seal/seal_ckks_rotation.cpp)
target_link_libraries(seal_ckks_rotation PRIVATE he_benchmark_common SEAL::seal)

add_executable(seal_bfv_serialization seal/seal_bfv_serialization.cpp)
target_link_libraries(seal_bfv_serialization PRIVATE he_benchmark_common SEAL::seal)

add_executable(seal_bgv_serialization seal/seal_bfv_serialization.cpp)
target_compile_definitions(seal_bgv_serialization PRIVATE HE_BENCHMARK_SEAL_SERIALIZATION_BGV)
target_link_libraries(seal_bgv_serialization PRIVATE he_benchmark_common SEAL::seal)

add_executable(seal_ckks_serialization seal/seal_ckks_serialization.cpp)
target_link_libraries(seal_ckks_serialization PRIVATE he_benchmark_common SEAL::seal)

add_executable(seal_bfv_dot seal/seal_exact_dot.cpp)
target_link_libraries(seal_bfv_dot PRIVATE he_benchmark_common SEAL::seal)

add_executable(seal_bgv_dot seal/seal_exact_dot.cpp)
target_compile_definitions(seal_bgv_dot PRIVATE HE_BENCHMARK_SEAL_DOT_BGV)
target_link_libraries(seal_bgv_dot PRIVATE he_benchmark_common SEAL::seal)

add_executable(seal_ckks_dot seal/seal_ckks_dot.cpp)
target_link_libraries(seal_ckks_dot PRIVATE he_benchmark_common SEAL::seal)

add_executable(seal_bfv_e2e seal/seal_exact_e2e.cpp)
target_link_libraries(seal_bfv_e2e PRIVATE he_benchmark_common SEAL::seal)

add_executable(seal_bgv_e2e seal/seal_exact_e2e.cpp)
target_compile_definitions(seal_bgv_e2e PRIVATE HE_BENCHMARK_SEAL_E2E_BGV)
target_link_libraries(seal_bgv_e2e PRIVATE he_benchmark_common SEAL::seal)

add_executable(seal_ckks_e2e seal/seal_ckks_e2e.cpp)
target_link_libraries(seal_ckks_e2e PRIVATE he_benchmark_common SEAL::seal)

add_executable(seal_bfv_memory seal/seal_exact_memory.cpp)
target_link_libraries(seal_bfv_memory PRIVATE he_benchmark_common SEAL::seal)

add_executable(seal_bgv_memory seal/seal_exact_memory.cpp)
target_compile_definitions(seal_bgv_memory PRIVATE HE_BENCHMARK_SEAL_MEMORY_BGV)
target_link_libraries(seal_bgv_memory PRIVATE he_benchmark_common SEAL::seal)

add_executable(seal_ckks_memory seal/seal_ckks_memory.cpp)
target_link_libraries(seal_ckks_memory PRIVATE he_benchmark_common SEAL::seal)

add_executable(seal_lowlevel_ntt seal/seal_lowlevel.cpp)
target_link_libraries(seal_lowlevel_ntt PRIVATE he_benchmark_common SEAL::seal)

add_executable(seal_lowlevel_poly seal/seal_lowlevel.cpp)
target_compile_definitions(seal_lowlevel_poly PRIVATE HE_BENCHMARK_SEAL_LOWLEVEL_POLY)
target_link_libraries(seal_lowlevel_poly PRIVATE he_benchmark_common SEAL::seal)

add_executable(seal_bfv_keyswitch seal/seal_keyswitch.cpp)
target_link_libraries(seal_bfv_keyswitch PRIVATE he_benchmark_common SEAL::seal)

add_executable(seal_bgv_keyswitch seal/seal_keyswitch.cpp)
target_compile_definitions(seal_bgv_keyswitch PRIVATE HE_BENCHMARK_SEAL_KEYSWITCH_BGV)
target_link_libraries(seal_bgv_keyswitch PRIVATE he_benchmark_common SEAL::seal)

add_executable(seal_ckks_keyswitch seal/seal_keyswitch.cpp)
target_compile_definitions(seal_ckks_keyswitch PRIVATE HE_BENCHMARK_SEAL_KEYSWITCH_CKKS)
target_link_libraries(seal_ckks_keyswitch PRIVATE he_benchmark_common SEAL::seal)

add_executable(seal_bfv_matrix seal/seal_exact_matrix.cpp)
target_link_libraries(seal_bfv_matrix PRIVATE he_benchmark_common SEAL::seal)

add_executable(seal_bgv_matrix seal/seal_exact_matrix.cpp)
target_compile_definitions(seal_bgv_matrix PRIVATE HE_BENCHMARK_SEAL_MATRIX_BGV)
target_link_libraries(seal_bgv_matrix PRIVATE he_benchmark_common SEAL::seal)

add_executable(seal_ckks_matrix seal/seal_ckks_matrix.cpp)
target_link_libraries(seal_ckks_matrix PRIVATE he_benchmark_common SEAL::seal)

add_executable(seal_bfv_throughput seal/seal_bfv_throughput.cpp)
target_link_libraries(seal_bfv_throughput PRIVATE he_benchmark_common SEAL::seal)

add_executable(seal_ckks_throughput seal/seal_ckks_throughput.cpp)
target_link_libraries(seal_ckks_throughput PRIVATE he_benchmark_common SEAL::seal)

foreach(resource_mode IN ITEMS heap footprint corpus_memory thread_memory cpu)
    string(TOUPPER "${resource_mode}" resource_mode_upper)
    add_executable(seal_bfv_${resource_mode} seal/seal_resource.cpp)
    target_compile_definitions(seal_bfv_${resource_mode} PRIVATE HE_BENCHMARK_RESOURCE_${resource_mode_upper})
    target_link_libraries(seal_bfv_${resource_mode} PRIVATE he_benchmark_common SEAL::seal)

    add_executable(seal_bgv_${resource_mode} seal/seal_resource.cpp)
    target_compile_definitions(seal_bgv_${resource_mode} PRIVATE
        HE_BENCHMARK_SEAL_RESOURCE_BGV
        HE_BENCHMARK_RESOURCE_${resource_mode_upper}
    )
    target_link_libraries(seal_bgv_${resource_mode} PRIVATE he_benchmark_common SEAL::seal)
endforeach()

if(HE_BENCHMARK_BUILD_OPENFHE)
    # Fresh builds expect OpenFHE by default. Pass
    # -DHE_BENCHMARK_BUILD_OPENFHE=OFF for SEAL-only local development.
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
        # Static linking is useful on servers where runtime library paths are
        # harder to control.
        set(HE_BENCHMARK_OPENFHE_LIBRARIES ${OpenFHE_STATIC_LIBRARIES})
    else()
        set(HE_BENCHMARK_OPENFHE_LIBRARIES ${OpenFHE_SHARED_LIBRARIES})
    endif()
    target_link_libraries(openfhe_bfv_exact PRIVATE
        he_benchmark_common
        ${HE_BENCHMARK_OPENFHE_LIBRARIES}
    )

    add_executable(openfhe_bgv_exact openfhe/openfhe_bgv_exact.cpp)
    target_include_directories(openfhe_bgv_exact PRIVATE
        ${OPENMP_INCLUDES}
        ${OpenFHE_INCLUDE}
        ${OpenFHE_INCLUDE}/third-party/include
        ${OpenFHE_INCLUDE}/core
        ${OpenFHE_INCLUDE}/pke
        ${OpenFHE_INCLUDE}/binfhe
    )
    target_compile_options(openfhe_bgv_exact PRIVATE ${OpenFHE_CXX_FLAGS_LIST})
    target_link_options(openfhe_bgv_exact PRIVATE ${OpenFHE_EXE_LINKER_FLAGS_LIST})
    if(UNIX AND NOT APPLE)
        target_link_options(openfhe_bgv_exact PRIVATE -Wl,--no-as-needed)
    endif()
    target_link_directories(openfhe_bgv_exact PRIVATE
        ${OpenFHE_LIBDIR}
        ${OPENMP_LIBRARIES}
    )
    target_link_libraries(openfhe_bgv_exact PRIVATE
        he_benchmark_common
        ${HE_BENCHMARK_OPENFHE_LIBRARIES}
    )

    add_executable(openfhe_ckks openfhe/openfhe_ckks.cpp)
    target_include_directories(openfhe_ckks PRIVATE
        ${OPENMP_INCLUDES}
        ${OpenFHE_INCLUDE}
        ${OpenFHE_INCLUDE}/third-party/include
        ${OpenFHE_INCLUDE}/core
        ${OpenFHE_INCLUDE}/pke
        ${OpenFHE_INCLUDE}/binfhe
    )
    target_compile_options(openfhe_ckks PRIVATE ${OpenFHE_CXX_FLAGS_LIST})
    target_link_options(openfhe_ckks PRIVATE ${OpenFHE_EXE_LINKER_FLAGS_LIST})
    if(UNIX AND NOT APPLE)
        target_link_options(openfhe_ckks PRIVATE -Wl,--no-as-needed)
    endif()
    target_link_directories(openfhe_ckks PRIVATE
        ${OpenFHE_LIBDIR}
        ${OPENMP_LIBRARIES}
    )
    target_link_libraries(openfhe_ckks PRIVATE
        he_benchmark_common
        ${HE_BENCHMARK_OPENFHE_LIBRARIES}
    )

    add_executable(openfhe_bfv_depth openfhe/openfhe_exact_depth.cpp)
    target_include_directories(openfhe_bfv_depth PRIVATE
        ${OPENMP_INCLUDES}
        ${OpenFHE_INCLUDE}
        ${OpenFHE_INCLUDE}/third-party/include
        ${OpenFHE_INCLUDE}/core
        ${OpenFHE_INCLUDE}/pke
        ${OpenFHE_INCLUDE}/binfhe
    )
    target_compile_options(openfhe_bfv_depth PRIVATE ${OpenFHE_CXX_FLAGS_LIST})
    target_link_options(openfhe_bfv_depth PRIVATE ${OpenFHE_EXE_LINKER_FLAGS_LIST})
    if(UNIX AND NOT APPLE)
        target_link_options(openfhe_bfv_depth PRIVATE -Wl,--no-as-needed)
    endif()
    target_link_directories(openfhe_bfv_depth PRIVATE
        ${OpenFHE_LIBDIR}
        ${OPENMP_LIBRARIES}
    )
    target_link_libraries(openfhe_bfv_depth PRIVATE
        he_benchmark_common
        ${HE_BENCHMARK_OPENFHE_LIBRARIES}
    )

    add_executable(openfhe_bgv_depth openfhe/openfhe_exact_depth.cpp)
    target_compile_definitions(openfhe_bgv_depth PRIVATE HE_BENCHMARK_OPENFHE_DEPTH_BGV)
    target_include_directories(openfhe_bgv_depth PRIVATE
        ${OPENMP_INCLUDES}
        ${OpenFHE_INCLUDE}
        ${OpenFHE_INCLUDE}/third-party/include
        ${OpenFHE_INCLUDE}/core
        ${OpenFHE_INCLUDE}/pke
        ${OpenFHE_INCLUDE}/binfhe
    )
    target_compile_options(openfhe_bgv_depth PRIVATE ${OpenFHE_CXX_FLAGS_LIST})
    target_link_options(openfhe_bgv_depth PRIVATE ${OpenFHE_EXE_LINKER_FLAGS_LIST})
    if(UNIX AND NOT APPLE)
        target_link_options(openfhe_bgv_depth PRIVATE -Wl,--no-as-needed)
    endif()
    target_link_directories(openfhe_bgv_depth PRIVATE
        ${OpenFHE_LIBDIR}
        ${OPENMP_LIBRARIES}
    )
    target_link_libraries(openfhe_bgv_depth PRIVATE
        he_benchmark_common
        ${HE_BENCHMARK_OPENFHE_LIBRARIES}
    )

    add_executable(openfhe_ckks_depth openfhe/openfhe_ckks_depth.cpp)
    target_include_directories(openfhe_ckks_depth PRIVATE
        ${OPENMP_INCLUDES}
        ${OpenFHE_INCLUDE}
        ${OpenFHE_INCLUDE}/third-party/include
        ${OpenFHE_INCLUDE}/core
        ${OpenFHE_INCLUDE}/pke
        ${OpenFHE_INCLUDE}/binfhe
    )
    target_compile_options(openfhe_ckks_depth PRIVATE ${OpenFHE_CXX_FLAGS_LIST})
    target_link_options(openfhe_ckks_depth PRIVATE ${OpenFHE_EXE_LINKER_FLAGS_LIST})
    if(UNIX AND NOT APPLE)
        target_link_options(openfhe_ckks_depth PRIVATE -Wl,--no-as-needed)
    endif()
    target_link_directories(openfhe_ckks_depth PRIVATE
        ${OpenFHE_LIBDIR}
        ${OPENMP_LIBRARIES}
    )
    target_link_libraries(openfhe_ckks_depth PRIVATE
        he_benchmark_common
        ${HE_BENCHMARK_OPENFHE_LIBRARIES}
    )

    add_executable(openfhe_bfv_dot openfhe/openfhe_exact_dot.cpp)
    target_include_directories(openfhe_bfv_dot PRIVATE
        ${OPENMP_INCLUDES}
        ${OpenFHE_INCLUDE}
        ${OpenFHE_INCLUDE}/third-party/include
        ${OpenFHE_INCLUDE}/core
        ${OpenFHE_INCLUDE}/pke
        ${OpenFHE_INCLUDE}/binfhe
    )
    target_compile_options(openfhe_bfv_dot PRIVATE ${OpenFHE_CXX_FLAGS_LIST})
    target_link_options(openfhe_bfv_dot PRIVATE ${OpenFHE_EXE_LINKER_FLAGS_LIST})
    if(UNIX AND NOT APPLE)
        target_link_options(openfhe_bfv_dot PRIVATE -Wl,--no-as-needed)
    endif()
    target_link_directories(openfhe_bfv_dot PRIVATE
        ${OpenFHE_LIBDIR}
        ${OPENMP_LIBRARIES}
    )
    target_link_libraries(openfhe_bfv_dot PRIVATE
        he_benchmark_common
        ${HE_BENCHMARK_OPENFHE_LIBRARIES}
    )

    add_executable(openfhe_bgv_dot openfhe/openfhe_exact_dot.cpp)
    target_compile_definitions(openfhe_bgv_dot PRIVATE HE_BENCHMARK_OPENFHE_DOT_BGV)
    target_include_directories(openfhe_bgv_dot PRIVATE
        ${OPENMP_INCLUDES}
        ${OpenFHE_INCLUDE}
        ${OpenFHE_INCLUDE}/third-party/include
        ${OpenFHE_INCLUDE}/core
        ${OpenFHE_INCLUDE}/pke
        ${OpenFHE_INCLUDE}/binfhe
    )
    target_compile_options(openfhe_bgv_dot PRIVATE ${OpenFHE_CXX_FLAGS_LIST})
    target_link_options(openfhe_bgv_dot PRIVATE ${OpenFHE_EXE_LINKER_FLAGS_LIST})
    if(UNIX AND NOT APPLE)
        target_link_options(openfhe_bgv_dot PRIVATE -Wl,--no-as-needed)
    endif()
    target_link_directories(openfhe_bgv_dot PRIVATE
        ${OpenFHE_LIBDIR}
        ${OPENMP_LIBRARIES}
    )
    target_link_libraries(openfhe_bgv_dot PRIVATE
        he_benchmark_common
        ${HE_BENCHMARK_OPENFHE_LIBRARIES}
    )

    add_executable(openfhe_ckks_dot openfhe/openfhe_ckks_dot.cpp)
    target_include_directories(openfhe_ckks_dot PRIVATE
        ${OPENMP_INCLUDES}
        ${OpenFHE_INCLUDE}
        ${OpenFHE_INCLUDE}/third-party/include
        ${OpenFHE_INCLUDE}/core
        ${OpenFHE_INCLUDE}/pke
        ${OpenFHE_INCLUDE}/binfhe
    )
    target_compile_options(openfhe_ckks_dot PRIVATE ${OpenFHE_CXX_FLAGS_LIST})
    target_link_options(openfhe_ckks_dot PRIVATE ${OpenFHE_EXE_LINKER_FLAGS_LIST})
    if(UNIX AND NOT APPLE)
        target_link_options(openfhe_ckks_dot PRIVATE -Wl,--no-as-needed)
    endif()
    target_link_directories(openfhe_ckks_dot PRIVATE
        ${OpenFHE_LIBDIR}
        ${OPENMP_LIBRARIES}
    )
    target_link_libraries(openfhe_ckks_dot PRIVATE
        he_benchmark_common
        ${HE_BENCHMARK_OPENFHE_LIBRARIES}
    )

    add_executable(openfhe_bfv_memory openfhe/openfhe_exact_memory.cpp)
    target_include_directories(openfhe_bfv_memory PRIVATE
        ${OPENMP_INCLUDES}
        ${OpenFHE_INCLUDE}
        ${OpenFHE_INCLUDE}/third-party/include
        ${OpenFHE_INCLUDE}/core
        ${OpenFHE_INCLUDE}/pke
        ${OpenFHE_INCLUDE}/binfhe
    )
    target_compile_options(openfhe_bfv_memory PRIVATE ${OpenFHE_CXX_FLAGS_LIST})
    target_link_options(openfhe_bfv_memory PRIVATE ${OpenFHE_EXE_LINKER_FLAGS_LIST})
    if(UNIX AND NOT APPLE)
        target_link_options(openfhe_bfv_memory PRIVATE -Wl,--no-as-needed)
    endif()
    target_link_directories(openfhe_bfv_memory PRIVATE
        ${OpenFHE_LIBDIR}
        ${OPENMP_LIBRARIES}
    )
    target_link_libraries(openfhe_bfv_memory PRIVATE
        he_benchmark_common
        ${HE_BENCHMARK_OPENFHE_LIBRARIES}
    )

    add_executable(openfhe_bgv_memory openfhe/openfhe_exact_memory.cpp)
    target_compile_definitions(openfhe_bgv_memory PRIVATE HE_BENCHMARK_OPENFHE_MEMORY_BGV)
    target_include_directories(openfhe_bgv_memory PRIVATE
        ${OPENMP_INCLUDES}
        ${OpenFHE_INCLUDE}
        ${OpenFHE_INCLUDE}/third-party/include
        ${OpenFHE_INCLUDE}/core
        ${OpenFHE_INCLUDE}/pke
        ${OpenFHE_INCLUDE}/binfhe
    )
    target_compile_options(openfhe_bgv_memory PRIVATE ${OpenFHE_CXX_FLAGS_LIST})
    target_link_options(openfhe_bgv_memory PRIVATE ${OpenFHE_EXE_LINKER_FLAGS_LIST})
    if(UNIX AND NOT APPLE)
        target_link_options(openfhe_bgv_memory PRIVATE -Wl,--no-as-needed)
    endif()
    target_link_directories(openfhe_bgv_memory PRIVATE
        ${OpenFHE_LIBDIR}
        ${OPENMP_LIBRARIES}
    )
    target_link_libraries(openfhe_bgv_memory PRIVATE
        he_benchmark_common
        ${HE_BENCHMARK_OPENFHE_LIBRARIES}
    )

    add_executable(openfhe_ckks_memory openfhe/openfhe_ckks_memory.cpp)
    target_include_directories(openfhe_ckks_memory PRIVATE
        ${OPENMP_INCLUDES}
        ${OpenFHE_INCLUDE}
        ${OpenFHE_INCLUDE}/third-party/include
        ${OpenFHE_INCLUDE}/core
        ${OpenFHE_INCLUDE}/pke
        ${OpenFHE_INCLUDE}/binfhe
    )
    target_compile_options(openfhe_ckks_memory PRIVATE ${OpenFHE_CXX_FLAGS_LIST})
    target_link_options(openfhe_ckks_memory PRIVATE ${OpenFHE_EXE_LINKER_FLAGS_LIST})
    if(UNIX AND NOT APPLE)
        target_link_options(openfhe_ckks_memory PRIVATE -Wl,--no-as-needed)
    endif()
    target_link_directories(openfhe_ckks_memory PRIVATE
        ${OpenFHE_LIBDIR}
        ${OPENMP_LIBRARIES}
    )
    target_link_libraries(openfhe_ckks_memory PRIVATE
        he_benchmark_common
        ${HE_BENCHMARK_OPENFHE_LIBRARIES}
    )

    add_executable(openfhe_bfv_rotation openfhe/openfhe_bfv_rotation.cpp)
    target_include_directories(openfhe_bfv_rotation PRIVATE
        ${OPENMP_INCLUDES}
        ${OpenFHE_INCLUDE}
        ${OpenFHE_INCLUDE}/third-party/include
        ${OpenFHE_INCLUDE}/core
        ${OpenFHE_INCLUDE}/pke
        ${OpenFHE_INCLUDE}/binfhe
    )
    target_compile_options(openfhe_bfv_rotation PRIVATE ${OpenFHE_CXX_FLAGS_LIST})
    target_link_options(openfhe_bfv_rotation PRIVATE ${OpenFHE_EXE_LINKER_FLAGS_LIST})
    if(UNIX AND NOT APPLE)
        target_link_options(openfhe_bfv_rotation PRIVATE -Wl,--no-as-needed)
    endif()
    target_link_directories(openfhe_bfv_rotation PRIVATE
        ${OpenFHE_LIBDIR}
        ${OPENMP_LIBRARIES}
    )
    target_link_libraries(openfhe_bfv_rotation PRIVATE
        he_benchmark_common
        ${HE_BENCHMARK_OPENFHE_LIBRARIES}
    )

    add_executable(openfhe_bgv_rotation openfhe/openfhe_bfv_rotation.cpp)
    target_compile_definitions(openfhe_bgv_rotation PRIVATE HE_BENCHMARK_OPENFHE_ROTATION_BGV)
    target_include_directories(openfhe_bgv_rotation PRIVATE
        ${OPENMP_INCLUDES}
        ${OpenFHE_INCLUDE}
        ${OpenFHE_INCLUDE}/third-party/include
        ${OpenFHE_INCLUDE}/core
        ${OpenFHE_INCLUDE}/pke
        ${OpenFHE_INCLUDE}/binfhe
    )
    target_compile_options(openfhe_bgv_rotation PRIVATE ${OpenFHE_CXX_FLAGS_LIST})
    target_link_options(openfhe_bgv_rotation PRIVATE ${OpenFHE_EXE_LINKER_FLAGS_LIST})
    if(UNIX AND NOT APPLE)
        target_link_options(openfhe_bgv_rotation PRIVATE -Wl,--no-as-needed)
    endif()
    target_link_directories(openfhe_bgv_rotation PRIVATE
        ${OpenFHE_LIBDIR}
        ${OPENMP_LIBRARIES}
    )
    target_link_libraries(openfhe_bgv_rotation PRIVATE
        he_benchmark_common
        ${HE_BENCHMARK_OPENFHE_LIBRARIES}
    )

    add_executable(openfhe_bfv_serialization openfhe/openfhe_bfv_serialization.cpp)
    target_include_directories(openfhe_bfv_serialization PRIVATE
        ${OPENMP_INCLUDES}
        ${OpenFHE_INCLUDE}
        ${OpenFHE_INCLUDE}/third-party/include
        ${OpenFHE_INCLUDE}/core
        ${OpenFHE_INCLUDE}/pke
        ${OpenFHE_INCLUDE}/binfhe
    )
    target_compile_options(openfhe_bfv_serialization PRIVATE ${OpenFHE_CXX_FLAGS_LIST})
    target_link_options(openfhe_bfv_serialization PRIVATE ${OpenFHE_EXE_LINKER_FLAGS_LIST})
    if(UNIX AND NOT APPLE)
        target_link_options(openfhe_bfv_serialization PRIVATE -Wl,--no-as-needed)
    endif()
    target_link_directories(openfhe_bfv_serialization PRIVATE
        ${OpenFHE_LIBDIR}
        ${OPENMP_LIBRARIES}
    )
    target_link_libraries(openfhe_bfv_serialization PRIVATE
        he_benchmark_common
        ${HE_BENCHMARK_OPENFHE_LIBRARIES}
    )

    add_executable(openfhe_bgv_serialization openfhe/openfhe_bfv_serialization.cpp)
    target_compile_definitions(openfhe_bgv_serialization PRIVATE HE_BENCHMARK_OPENFHE_SERIALIZATION_BGV)
    target_include_directories(openfhe_bgv_serialization PRIVATE
        ${OPENMP_INCLUDES}
        ${OpenFHE_INCLUDE}
        ${OpenFHE_INCLUDE}/third-party/include
        ${OpenFHE_INCLUDE}/core
        ${OpenFHE_INCLUDE}/pke
        ${OpenFHE_INCLUDE}/binfhe
    )
    target_compile_options(openfhe_bgv_serialization PRIVATE ${OpenFHE_CXX_FLAGS_LIST})
    target_link_options(openfhe_bgv_serialization PRIVATE ${OpenFHE_EXE_LINKER_FLAGS_LIST})
    if(UNIX AND NOT APPLE)
        target_link_options(openfhe_bgv_serialization PRIVATE -Wl,--no-as-needed)
    endif()
    target_link_directories(openfhe_bgv_serialization PRIVATE
        ${OpenFHE_LIBDIR}
        ${OPENMP_LIBRARIES}
    )
    target_link_libraries(openfhe_bgv_serialization PRIVATE
        he_benchmark_common
        ${HE_BENCHMARK_OPENFHE_LIBRARIES}
    )

    add_executable(openfhe_ckks_serialization openfhe/openfhe_ckks_serialization.cpp)
    target_include_directories(openfhe_ckks_serialization PRIVATE
        ${OPENMP_INCLUDES}
        ${OpenFHE_INCLUDE}
        ${OpenFHE_INCLUDE}/third-party/include
        ${OpenFHE_INCLUDE}/core
        ${OpenFHE_INCLUDE}/pke
        ${OpenFHE_INCLUDE}/binfhe
    )
    target_compile_options(openfhe_ckks_serialization PRIVATE ${OpenFHE_CXX_FLAGS_LIST})
    target_link_options(openfhe_ckks_serialization PRIVATE ${OpenFHE_EXE_LINKER_FLAGS_LIST})
    if(UNIX AND NOT APPLE)
        target_link_options(openfhe_ckks_serialization PRIVATE -Wl,--no-as-needed)
    endif()
    target_link_directories(openfhe_ckks_serialization PRIVATE
        ${OpenFHE_LIBDIR}
        ${OPENMP_LIBRARIES}
    )
    target_link_libraries(openfhe_ckks_serialization PRIVATE
        he_benchmark_common
        ${HE_BENCHMARK_OPENFHE_LIBRARIES}
    )

    function(add_openfhe_benchmark target_name source_file)
        add_executable(${target_name} ${source_file})
        if(ARGN)
            target_compile_definitions(${target_name} PRIVATE ${ARGN})
        endif()
        target_include_directories(${target_name} PRIVATE
            ${OPENMP_INCLUDES}
            ${OpenFHE_INCLUDE}
            ${OpenFHE_INCLUDE}/third-party/include
            ${OpenFHE_INCLUDE}/core
            ${OpenFHE_INCLUDE}/pke
            ${OpenFHE_INCLUDE}/binfhe
        )
        target_compile_options(${target_name} PRIVATE ${OpenFHE_CXX_FLAGS_LIST})
        target_link_options(${target_name} PRIVATE ${OpenFHE_EXE_LINKER_FLAGS_LIST})
        if(UNIX AND NOT APPLE)
            target_link_options(${target_name} PRIVATE -Wl,--no-as-needed)
        endif()
        target_link_directories(${target_name} PRIVATE
            ${OpenFHE_LIBDIR}
            ${OPENMP_LIBRARIES}
        )
        target_link_libraries(${target_name} PRIVATE
            he_benchmark_common
            ${HE_BENCHMARK_OPENFHE_LIBRARIES}
        )
    endfunction()

    add_openfhe_benchmark(openfhe_lowlevel_ntt openfhe/openfhe_lowlevel.cpp)
    add_openfhe_benchmark(openfhe_lowlevel_poly openfhe/openfhe_lowlevel.cpp HE_BENCHMARK_OPENFHE_LOWLEVEL_POLY)

    add_openfhe_benchmark(openfhe_bfv_keyswitch openfhe/openfhe_keyswitch.cpp)
    add_openfhe_benchmark(openfhe_bgv_keyswitch openfhe/openfhe_keyswitch.cpp HE_BENCHMARK_OPENFHE_KEYSWITCH_BGV)
    add_openfhe_benchmark(openfhe_ckks_keyswitch openfhe/openfhe_keyswitch.cpp HE_BENCHMARK_OPENFHE_KEYSWITCH_CKKS)
    add_openfhe_benchmark(openfhe_bfv_matrix openfhe/openfhe_exact_matrix.cpp)
    add_openfhe_benchmark(openfhe_bgv_matrix openfhe/openfhe_exact_matrix.cpp HE_BENCHMARK_OPENFHE_MATRIX_BGV)
    add_openfhe_benchmark(openfhe_ckks_matrix openfhe/openfhe_ckks_matrix.cpp)
    add_openfhe_benchmark(openfhe_ckks_rotation openfhe/openfhe_ckks_rotation.cpp)
    add_openfhe_benchmark(openfhe_bfv_e2e openfhe/openfhe_exact_e2e.cpp)
    add_openfhe_benchmark(openfhe_bgv_e2e openfhe/openfhe_exact_e2e.cpp HE_BENCHMARK_OPENFHE_E2E_BGV)
    add_openfhe_benchmark(openfhe_ckks_e2e openfhe/openfhe_ckks_e2e.cpp)
    add_openfhe_benchmark(openfhe_bfv_throughput openfhe/openfhe_bfv_throughput.cpp)
    add_openfhe_benchmark(openfhe_ckks_throughput openfhe/openfhe_ckks_throughput.cpp)

    foreach(resource_mode IN ITEMS heap footprint corpus_memory thread_memory cpu)
        string(TOUPPER "${resource_mode}" resource_mode_upper)
        add_openfhe_benchmark(
            openfhe_bfv_${resource_mode}
            openfhe/openfhe_resource.cpp
            HE_BENCHMARK_RESOURCE_${resource_mode_upper}
        )
        add_openfhe_benchmark(
            openfhe_bgv_${resource_mode}
            openfhe/openfhe_resource.cpp
            HE_BENCHMARK_OPENFHE_RESOURCE_BGV
            HE_BENCHMARK_RESOURCE_${resource_mode_upper}
        )
    endforeach()
endif()
