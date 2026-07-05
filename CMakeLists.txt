cmake_minimum_required(VERSION 3.16)
project(UmbraNote VERSION 1.2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory(core)

option(ZERONOTE_BUILD_TESTS "Build UmbraNote crypto tests" ON)
if(ZERONOTE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(WIN32)
    enable_language(RC)
    add_subdirectory(platform/win32)
elseif(APPLE OR UNIX)
    add_subdirectory(platform/linux)
else()
    message(FATAL_ERROR "UmbraNote supports Windows, Linux, and macOS builds.")
endif()
