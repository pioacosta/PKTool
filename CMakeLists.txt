cmake_minimum_required(VERSION 3.16)
project(PKTool LANGUAGES CXX)

# Host build of the portable core, its tests and the command-line tool.
# The Nintendo DS build uses the Makefile (devkitARM) instead.

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

option(PKTOOL_SANITIZE "Build with address and undefined-behaviour sanitizers" OFF)
option(PKTOOL_FUZZ "Build the fuzzers (tests/fuzz): instrument the core for coverage and build pktool_fuzz, plus libFuzzer executables with clang" OFF)

file(GLOB_RECURSE PKTOOL_CORE_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/core/*.cpp)
add_library(pktool_core STATIC ${PKTOOL_CORE_SOURCES})
target_include_directories(pktool_core PUBLIC ${CMAKE_SOURCE_DIR})
# Same restrictions as on the DS, so host builds catch DS build problems early.
target_compile_options(pktool_core PUBLIC -fno-exceptions -fno-rtti -Wall -Wextra -Werror)

if(PKTOOL_SANITIZE)
    # Stop at the first undefined behaviour instead of printing it, so CI and
    # the fuzzers fail on it.
    target_compile_options(pktool_core PUBLIC -fsanitize=address,undefined -fno-sanitize-recover=undefined
                           -fno-omit-frame-pointer)
    target_link_options(pktool_core PUBLIC -fsanitize=address,undefined)
endif()

# Fuzz targets, the input generators and the mutation engine. Always built:
# the normal test-suite runs them as a deterministic smoke test.
add_library(pktool_fuzz_lib STATIC
    ${CMAKE_SOURCE_DIR}/tests/fuzz/fuzz_targets.cpp
    ${CMAKE_SOURCE_DIR}/tests/fuzz/fuzz_gen.cpp)
target_link_libraries(pktool_fuzz_lib PUBLIC pktool_core pktool_ui)
target_compile_definitions(pktool_fuzz_lib PUBLIC PKTOOL_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

if(PKTOOL_FUZZ)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        target_compile_options(pktool_core PUBLIC -fsanitize=fuzzer-no-link)
    else()
        # No libFuzzer: the core reports edges to tests/fuzz/fuzz_cov.cpp and
        # pktool_fuzz (fuzz_driver.cpp) does the coverage-guided mutation.
        add_library(pktool_fuzz_cov STATIC ${CMAKE_SOURCE_DIR}/tests/fuzz/fuzz_cov.cpp)
        target_compile_options(pktool_core PUBLIC -fsanitize-coverage=trace-pc)
        target_link_libraries(pktool_core PUBLIC pktool_fuzz_cov)
    endif()
endif()

# The DS user interface (everything in ds/ except the libnds glue in
# ds/main.cpp) is portable, so it is built and tested on the host too.
file(GLOB_RECURSE PKTOOL_UI_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/ds/app/*.cpp ${CMAKE_SOURCE_DIR}/ds/gfx/*.cpp)
add_library(pktool_ui STATIC ${PKTOOL_UI_SOURCES})
target_link_libraries(pktool_ui PUBLIC pktool_core)

file(GLOB PKTOOL_TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/tests/*.cpp)
add_executable(pktool_tests ${PKTOOL_TEST_SOURCES})
target_link_libraries(pktool_tests PRIVATE pktool_fuzz_lib pktool_core pktool_ui)
target_compile_definitions(pktool_tests PRIVATE PKTOOL_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

add_executable(pktool_preview ${CMAKE_SOURCE_DIR}/tools/preview/main.cpp)
target_link_libraries(pktool_preview PRIVATE pktool_ui)
target_compile_definitions(pktool_preview PRIVATE PKTOOL_ASSET_DIR="${CMAKE_SOURCE_DIR}/assets/nitrofs")

add_executable(pktool_cli ${CMAKE_SOURCE_DIR}/tools/cli/main.cpp)
target_link_libraries(pktool_cli PRIVATE pktool_core)

if(PKTOOL_FUZZ)
    add_executable(pktool_fuzz ${CMAKE_SOURCE_DIR}/tests/fuzz/fuzz_driver.cpp ${CMAKE_SOURCE_DIR}/tests/fuzz/fuzz_stubs.cpp)
    target_link_libraries(pktool_fuzz PRIVATE pktool_fuzz_lib)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        foreach(target save_load save_probe entity text showdown bank settings legality checks card_file language_pack cjk_font)
            add_executable(fuzz_${target} ${CMAKE_SOURCE_DIR}/tests/fuzz/fuzz_libfuzzer.cpp ${CMAKE_SOURCE_DIR}/tests/fuzz/fuzz_stubs.cpp)
            target_compile_definitions(fuzz_${target} PRIVATE PKTOOL_FUZZ_TARGET="${target}")
            target_link_libraries(fuzz_${target} PRIVATE pktool_fuzz_lib)
            target_link_options(fuzz_${target} PRIVATE -fsanitize=fuzzer)
        endforeach()
    endif()
endif()

enable_testing()
add_test(NAME core COMMAND pktool_tests)
