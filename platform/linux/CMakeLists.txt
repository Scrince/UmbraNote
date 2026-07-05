find_package(PkgConfig REQUIRED)
pkg_check_modules(GTK3 REQUIRED gtk+-3.0)

add_executable(UmbraNote
    main.cpp
)

target_include_directories(UmbraNote PRIVATE ${GTK3_INCLUDE_DIRS})
target_link_directories(UmbraNote PRIVATE ${GTK3_LIBRARY_DIRS})
target_link_libraries(UmbraNote PRIVATE zeronote_core ${GTK3_LIBRARIES})
target_compile_options(UmbraNote PRIVATE ${GTK3_CFLAGS_OTHER})
