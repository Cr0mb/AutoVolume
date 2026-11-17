cmake_minimum_required(VERSION 3.21)
project(AutoVolume VERSION 1.0.0 LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# static CRT -> a single self-contained AutoVolume.exe
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

add_compile_definitions(UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
if(MSVC)
    add_compile_options(/W3 /utf-8 /permissive- /MP)
endif()

# ---- Dear ImGui (Win32 + DirectX 11 backends)
set(IMGUI_DIR ${CMAKE_SOURCE_DIR}/third_party/imgui)
add_library(imgui STATIC
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_win32.cpp
    ${IMGUI_DIR}/backends/imgui_impl_dx11.cpp)
target_include_directories(imgui PUBLIC ${IMGUI_DIR} ${IMGUI_DIR}/backends)
target_compile_definitions(imgui PUBLIC IMGUI_DISABLE_OBSOLETE_FUNCTIONS)

# ---- engine (audio capture, loudness, volume control, settings)
add_library(avcore STATIC src/engine.cpp src/settings.cpp src/log.cpp)
target_include_directories(avcore PUBLIC src third_party/json)
target_link_libraries(avcore PUBLIC ole32 uuid)

# ---- application
add_executable(AutoVolume WIN32
    src/main.cpp src/ui.cpp src/platform.cpp
    res/autovolume.rc res/app.manifest)
target_include_directories(AutoVolume PRIVATE res)
target_link_libraries(AutoVolume PRIVATE avcore imgui d3d11 dxgi dwmapi shell32 ole32 uuid)
set_target_properties(AutoVolume PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin/$<0:>")

# ---- tests
enable_testing()
add_executable(autovolume_tests tests/tests.cpp)
target_link_libraries(autovolume_tests PRIVATE avcore)
add_test(NAME unit COMMAND autovolume_tests)
