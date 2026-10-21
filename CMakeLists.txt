cmake_minimum_required(VERSION 3.20)
project(miniredis LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Default build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

add_compile_options(-Wall -Wextra -Wpedantic)

# Sanitizers
option(ENABLE_ASAN "Enable AddressSanitizer" OFF)
option(ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer" OFF)

if(ENABLE_ASAN)
    add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address)
endif()

if(ENABLE_UBSAN)
    add_compile_options(-fsanitize=undefined)
    add_link_options(-fsanitize=undefined)
endif()

# Core library sources
set(CORE_SOURCES
    src/util/logger.cpp
    src/util/config.cpp
    src/store/store.cpp
    src/protocol/resp_parser.cpp
    src/protocol/resp_encoder.cpp
    src/commands/command_table.cpp
    src/net/server.cpp
)

add_library(miniredis_core STATIC ${CORE_SOURCES})
target_include_directories(miniredis_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

# Server executable
add_executable(miniredis_server src/main.cpp)
target_link_libraries(miniredis_server PRIVATE miniredis_core)

# GoogleTest
include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.14.0
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

enable_testing()

add_executable(unit_tests
    tests/test_hash_table.cpp
    tests/test_parser.cpp
    tests/test_store.cpp
)
target_link_libraries(unit_tests PRIVATE miniredis_core gtest_main)
include(GoogleTest)
gtest_discover_tests(unit_tests)
