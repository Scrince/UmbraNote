include(FetchContent)
FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.5.4
)
set(CATCH_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(CATCH_INSTALL_DOCS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(Catch2)

add_executable(crypto_v3_test crypto_v3_test.cpp)

target_link_libraries(crypto_v3_test PRIVATE zeronote_core Catch2::Catch2WithMain)

target_compile_features(crypto_v3_test PRIVATE cxx_std_17)

add_test(NAME crypto_v3_test COMMAND crypto_v3_test
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_executable(generate_fixtures generate_fixtures.cpp)
target_link_libraries(generate_fixtures PRIVATE zeronote_core)
target_compile_features(generate_fixtures PRIVATE cxx_std_17)

