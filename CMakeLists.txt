cmake_minimum_required(VERSION 3.24)

project(QTEVisionDebugger VERSION 0.1.0 LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "QTEVisionDebugger requires Windows 10 or later.")
endif()

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "Configure this project with an x64 generator.")
endif()

add_executable(QTEVisionDebugger WIN32
    src/main.cpp
    src/capture/DxgiCapture.cpp
    src/config/Config.cpp
    src/debug/DebugWindow.cpp
    src/vision/ImageIO.cpp
    src/vision/QteDetector.cpp
)

target_compile_features(QTEVisionDebugger PRIVATE cxx_std_20)
target_compile_definitions(QTEVisionDebugger PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    $<$<CONFIG:Debug>:QTE_DEBUG_BUILD>
)
target_compile_options(QTEVisionDebugger PRIVATE /W4 /permissive- /EHsc /Zc:__cplusplus /utf-8)
target_link_libraries(QTEVisionDebugger PRIVATE d3d11 dxgi windowscodecs ole32)

configure_file(config.ini config.ini COPYONLY)

set_target_properties(QTEVisionDebugger PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/$<CONFIG>"
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
)

add_custom_command(TARGET QTEVisionDebugger POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/config.ini"
        "$<TARGET_FILE_DIR:QTEVisionDebugger>/config.ini"
    COMMENT "Copying runtime configuration"
)
