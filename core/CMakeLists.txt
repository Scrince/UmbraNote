add_library(zeronote_core STATIC
    text_codec.cpp
    crypto.cpp
    crypto_v3.cpp
    secure_memory.cpp
    pdf_export.cpp
)

target_include_directories(zeronote_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(zeronote_core PUBLIC cxx_std_17)

find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(LIBSODIUM QUIET libsodium)
endif()

if(LIBSODIUM_FOUND)
    target_include_directories(zeronote_core PUBLIC ${LIBSODIUM_INCLUDE_DIRS})
    target_link_libraries(zeronote_core PUBLIC ${LIBSODIUM_LIBRARIES})
    target_compile_options(zeronote_core PUBLIC ${LIBSODIUM_CFLAGS_OTHER})
else()
    include(FetchContent)
    FetchContent_Declare(
        libsodium_cmake
        GIT_REPOSITORY https://github.com/robinlinden/libsodium-cmake.git
        GIT_TAG 2257eb3f9539c281d662b0a606ecf37d8e590ca0
    )
    set(SODIUM_DISABLE_TESTS ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(libsodium_cmake)
    if(TARGET sodium)
        target_link_libraries(zeronote_core PUBLIC sodium)
    else()
        message(FATAL_ERROR "libsodium CMake target not found. Install libsodium or ensure Git is available.")
    endif()
endif()

if(WIN32)
    target_link_libraries(zeronote_core PUBLIC bcrypt)
else()
    find_package(OpenSSL REQUIRED)
    target_link_libraries(zeronote_core PUBLIC OpenSSL::Crypto)
endif()