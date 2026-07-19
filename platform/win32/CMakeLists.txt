add_executable(UmbraNote WIN32
    main.cpp
    notepad.cpp
    resource.rc
)

target_include_directories(UmbraNote PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(UmbraNote PRIVATE zeronote_core comctl32 comdlg32 dwmapi shell32 shcore)

if(MSVC)
    target_compile_options(UmbraNote PRIVATE /W4 /utf-8)
    target_compile_definitions(UmbraNote PRIVATE _WIN32_WINNT=0x0A00 WINVER=0x0A00)
    target_sources(UmbraNote PRIVATE app.manifest)
endif()
