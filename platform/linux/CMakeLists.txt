find_package(PkgConfig REQUIRED)
pkg_check_modules(GTK4 REQUIRED gtk4)

add_executable(UmbraNote
    main.cpp
)

target_include_directories(UmbraNote PRIVATE ${GTK4_INCLUDE_DIRS})
target_link_libraries(UmbraNote PRIVATE zeronote_core ${GTK4_LIBRARIES})
target_compile_options(UmbraNote PRIVATE ${GTK4_CFLAGS_OTHER})