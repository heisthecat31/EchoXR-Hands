# EchoXR Hands built with CMake: the hand tracking plugin (EchoXRHands.dll), its OpenXR API
# layer (layer/XrApiLayer_EchoXR_Hands.dll, which reads your fingers in the game's own OpenXR
# session) and the settings window (EchoXRSettings.exe), into out/. MSVC on Windows, or
# clang-cl from macOS/Linux:
#
#   cmake -S . -B build -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/clang-cl-xwin.cmake
#   cmake --build build
#
# Detours (MIT) is built from its v4.0.1 release and the OpenXR headers come from the Khronos
# OpenXR SDK 1.1.63 (Apache-2.0, as EchoXR pins it), both checked by hash.

cmake_minimum_required(VERSION 3.20)
# Release unless asked (before project(), which would settle on Debug).
if(NOT DEFINED CMAKE_BUILD_TYPE OR CMAKE_BUILD_TYPE STREQUAL "")
    set(CMAKE_BUILD_TYPE Release CACHE STRING "")
endif()
project(EchoXRHands LANGUAGES C CXX RC)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(OUT ${CMAKE_CURRENT_SOURCE_DIR}/out)
file(MAKE_DIRECTORY ${OUT})

# The resource scripts name their files from the repo root with backslashes (build.bat
# runs rc there); llvm-rc off Windows wants forward slashes, so it gets a copy.
function(rc_copy name src)
    file(READ ${CMAKE_CURRENT_SOURCE_DIR}/${src} text)
    string(REPLACE "\\\\" "/" text "${text}")
    string(REGEX REPLACE "\"(logo/|settings/|bridge/|EchoXRHands\\.txt)" "\"${CMAKE_CURRENT_SOURCE_DIR}/\\1" text "${text}")
    file(WRITE ${CMAKE_BINARY_DIR}/${name} "${text}")
endfunction()
rc_copy(settings.rc settings/settings.rc)

include(FetchContent)
FetchContent_Declare(detours
    URL https://github.com/microsoft/Detours/archive/refs/tags/v4.0.1.tar.gz
    URL_HASH SHA256=75df7f1f084acaf83c3b96a90328e93823c2506a97363d50556f1ee700c81f68
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SOURCE_SUBDIR no-cmake-project
)
FetchContent_Declare(openxr_sdk
    URL https://github.com/KhronosGroup/OpenXR-SDK/archive/f2448a8797c85814aa892efc1ab8707900fbcc78.tar.gz
    URL_HASH SHA256=1681e0f6c9633c73117c2fc376d1fe71cbd6fdd2aeb896ed5854ff022aa82eaa
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SOURCE_SUBDIR no-cmake-project
)
FetchContent_MakeAvailable(detours openxr_sdk)
set(D ${detours_SOURCE_DIR}/src)
add_library(detours STATIC
    ${D}/detours.cpp ${D}/modules.cpp ${D}/disasm.cpp ${D}/image.cpp ${D}/creatwth.cpp
    ${D}/disolx86.cpp ${D}/disolx64.cpp ${D}/disolia64.cpp ${D}/disolarm.cpp
    ${D}/disolarm64.cpp)
target_include_directories(detours PUBLIC ${D})
target_compile_definitions(detours PRIVATE WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0601)
target_compile_options(detours PRIVATE /W0)
set_target_properties(detours PROPERTIES MSVC_RUNTIME_LIBRARY MultiThreadedDLL)

# The plugin: /MD /EHa, as build.bat.
add_library(EchoXRHands SHARED plugin/handtracking.cpp plugin/htv_net.cpp)
target_include_directories(EchoXRHands PRIVATE third_party plugin)
target_link_libraries(EchoXRHands PRIVATE detours ws2_32 winhttp user32)
target_compile_options(EchoXRHands PRIVATE /EHa /W3)
set_target_properties(EchoXRHands PROPERTIES
    MSVC_RUNTIME_LIBRARY MultiThreadedDLL
    RUNTIME_OUTPUT_DIRECTORY ${OUT} LIBRARY_OUTPUT_DIRECTORY ${OUT} ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

# The OpenXR API layer: static CRT, so it needs nothing beside the game.
add_library(XrApiLayer_EchoXR_Hands SHARED layer/layer.cpp)
target_include_directories(XrApiLayer_EchoXR_Hands PRIVATE ${openxr_sdk_SOURCE_DIR}/include common plugin)
target_compile_definitions(XrApiLayer_EchoXR_Hands PRIVATE _CRT_SECURE_NO_WARNINGS)
target_link_libraries(XrApiLayer_EchoXR_Hands PRIVATE ws2_32 user32)
target_compile_options(XrApiLayer_EchoXR_Hands PRIVATE /EHsc /W3)
set_target_properties(XrApiLayer_EchoXR_Hands PROPERTIES
    MSVC_RUNTIME_LIBRARY MultiThreaded
    RUNTIME_OUTPUT_DIRECTORY ${OUT}/layer LIBRARY_OUTPUT_DIRECTORY ${OUT}/layer ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
file(MAKE_DIRECTORY ${OUT}/layer)
configure_file(layer/XrApiLayer_EchoXR_Hands.json ${OUT}/layer/XrApiLayer_EchoXR_Hands.json COPYONLY)

# The curl math's tests (a Windows console program; the header is plain C++, so
# `c++ -std=c++17 -Icommon tests/hand_curls_test.cpp` runs them on any desktop too).
add_executable(hand_curls_test tests/hand_curls_test.cpp)
target_include_directories(hand_curls_test PRIVATE common)
set_target_properties(hand_curls_test PROPERTIES MSVC_RUNTIME_LIBRARY MultiThreaded)

# The settings window: static CRT, Unicode, GUI.
add_executable(EchoXRSettings WIN32 settings/settings.cpp ${CMAKE_BINARY_DIR}/settings.rc)
set_property(SOURCE ${CMAKE_BINARY_DIR}/settings.rc APPEND PROPERTY INCLUDE_DIRECTORIES ${CMAKE_CURRENT_SOURCE_DIR}/settings)
target_include_directories(EchoXRSettings PRIVATE common)
target_compile_definitions(EchoXRSettings PRIVATE UNICODE _UNICODE)
target_compile_options(EchoXRSettings PRIVATE /EHsc /W3 /utf-8)
target_link_libraries(EchoXRSettings PRIVATE user32 gdi32 gdiplus dwmapi shell32 ole32 ws2_32)
set_target_properties(EchoXRSettings PROPERTIES
    MSVC_RUNTIME_LIBRARY MultiThreaded RUNTIME_OUTPUT_DIRECTORY ${OUT})
# Its manifest is in settings.rc already.
target_link_options(EchoXRSettings PRIVATE /MANIFEST:NO)

configure_file(EchoXRHands.txt ${OUT}/ COPYONLY)
