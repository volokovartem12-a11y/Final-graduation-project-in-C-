cmake_minimum_required(VERSION 3.16)
project(search_engine VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
find_package(Threads REQUIRED)

add_library(search_core
    src/ConverterJSON.cpp
    src/InvertedIndex.cpp
    src/SearchServer.cpp
    src/TextProcessing.cpp
)
target_include_directories(search_core PUBLIC include third_party)
target_link_libraries(search_core PUBLIC Threads::Threads)
if(MSVC)
    target_compile_options(search_core PRIVATE /W4 /utf-8)
else()
    target_compile_options(search_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(search_engine src/main.cpp)
target_link_libraries(search_engine PRIVATE search_core)
if(WIN32)
    target_link_libraries(search_engine PRIVATE shell32)
endif()
if(MSVC)
    target_compile_options(search_engine PRIVATE /W4 /utf-8)
else()
    target_compile_options(search_engine PRIVATE -Wall -Wextra -Wpedantic)
endif()

include(CTest)
if(BUILD_TESTING)
    include(FetchContent)
    if(POLICY CMP0135)
        cmake_policy(SET CMP0135 NEW)
    endif()
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(googletest
        URL "${CMAKE_CURRENT_SOURCE_DIR}/third_party/googletest-v1.14.0.zip"
        URL_HASH SHA256=1f357c27ca988c3f7c6b4bf68a9395005ac6761f034046e9dde0896e3aba00e4
    )
    FetchContent_MakeAvailable(googletest)
    add_executable(search_tests
        tests/converter_tests.cpp
        tests/index_tests.cpp
        tests/search_tests.cpp
    )
    target_link_libraries(search_tests PRIVATE search_core GTest::gtest_main)
    if(MSVC)
        target_compile_options(search_tests PRIVATE /utf-8)
    endif()
    include(GoogleTest)
    gtest_discover_tests(search_tests DISCOVERY_TIMEOUT 30)
    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(Python3_Interpreter_FOUND)
        add_test(NAME cli_integration
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/cli_smoke.py"
                    "$<TARGET_FILE:search_engine>")
        set_tests_properties(cli_integration PROPERTIES TIMEOUT 45)
    endif()
endif()
