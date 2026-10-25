cmake_minimum_required(VERSION 3.25)

project(MiniTranslatorNative VERSION 1.0.0 LANGUAGES CXX RC)

set(MINITRANS_UPDATE_REPOSITORY_URL
    "https://github.com/Freyja04/MiniTranslator"
    CACHE STRING "Immutable GitHub repository trusted by the native updater")
set(MINITRANS_UPDATE_MANIFEST_URL
    "https://github.com/Freyja04/MiniTranslator/releases/latest/download/update-manifest.json"
    CACHE STRING "Immutable GitHub Release manifest URL trusted by the native updater")
set(MINITRANS_UPDATE_CHANNEL "stable" CACHE STRING "Native update channel")

if(NOT WIN32)
    message(FATAL_ERROR "The native MiniTranslator client only supports Windows.")
endif()

if(NOT MSVC)
    message(FATAL_ERROR "The native MiniTranslator client currently requires MSVC.")
endif()

add_executable(MiniTranslator WIN32
    src/app.cpp
    src/app.hpp
    src/app_update.cpp
    src/block_editor.cpp
    src/block_editor.hpp
    src/capture.cpp
    src/capture.hpp
    src/config.cpp
    src/config.hpp
    src/core_self_test.cpp
    src/core_self_test.hpp
    src/crypto.cpp
    src/crypto.hpp
    src/dpapi.cpp
    src/dpapi.hpp
    src/image_codec.cpp
    src/image_codec.hpp
    src/http.cpp
    src/http.hpp
    src/hotkey.cpp
    src/hotkey.hpp
    src/language.cpp
    src/language.hpp
    src/language_catalog.hpp
    src/layout.cpp
    src/layout.hpp
    src/main.cpp
    src/ocr.cpp
    src/ocr.hpp
    src/pipeline.cpp
    src/pipeline.hpp
    src/provider_key_dialog.cpp
    src/provider_key_dialog.hpp
    src/resource.h
    src/result_renderer.cpp
    src/result_renderer.hpp
    src/result_window.cpp
    src/result_window.hpp
    src/result_window_control_bar.cpp
    src/result_window_internal.hpp
    src/result_window_render.cpp
    src/MiniTranslator.rc
    src/selection_overlay.cpp
    src/selection_overlay.hpp
    src/settings_catalog.hpp
    src/settings_ai_dialog.cpp
    src/settings_ai_dialog.hpp
    src/settings_ai_list.cpp
    src/settings_ai_list.hpp
    src/settings_ocr.cpp
    src/settings_ocr.hpp
    src/settings_profiles.cpp
    src/settings_profiles.hpp
    src/settings_provider.hpp
    src/settings_window.cpp
    src/settings_window.hpp
    src/settings_window_internal.cpp
    src/settings_window_internal.hpp
    src/settings_window_layout.cpp
    src/settings_window_pages.cpp
    src/settings_window_paint.cpp
    src/settings_window_self_test.cpp
    src/settings_window_state.cpp
    src/text_translation_session.cpp
    src/text_translation_session.hpp
    src/text_translation_window.cpp
    src/text_translation_window.hpp
    src/theme.cpp
    src/theme.hpp
    src/tray_menu.cpp
    src/tray_menu.hpp
    src/types.hpp
    src/ui_controls.cpp
    src/ui_controls.hpp
    src/ui_icons.hpp
    src/ui_draw.hpp
    src/updater.cpp
    src/updater.hpp
    src/translator.cpp
    src/translator.hpp
    src/util.cpp
    src/util.hpp
    src/version.hpp
)

target_compile_features(MiniTranslator PRIVATE cxx_std_20)

set_target_properties(MiniTranslator PROPERTIES
    OUTPUT_NAME "MiniTranslator"
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE
)

target_compile_definitions(MiniTranslator PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    STRICT
    WINVER=0x0A00
    _WIN32_WINNT=0x0A00
    "MINITRANS_VERSION_WIDE=L\"${PROJECT_VERSION}\""
    "MINITRANS_UPDATE_REPOSITORY_URL=L\"${MINITRANS_UPDATE_REPOSITORY_URL}\""
    "MINITRANS_UPDATE_MANIFEST_URL=L\"${MINITRANS_UPDATE_MANIFEST_URL}\""
    "MINITRANS_UPDATE_CHANNEL=L\"${MINITRANS_UPDATE_CHANNEL}\""
)

target_include_directories(MiniTranslator PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/.."
)

target_compile_options(MiniTranslator PRIVATE
    /W4
    /permissive-
    /EHsc
    /utf-8
    $<$<CONFIG:Release>:/O2>
)

target_link_options(MiniTranslator PRIVATE
    $<$<CONFIG:Debug>:/DEBUG:FULL>
    $<$<CONFIG:Release>:/LTCG>
    $<$<CONFIG:Release>:/OPT:REF>
    $<$<CONFIG:Release>:/OPT:ICF>
)

target_link_libraries(MiniTranslator PRIVATE
    user32
    shell32
    comctl32
    gdi32
    d2d1
    windowscodecs
    winhttp
    crypt32
    bcrypt
    ole32
    runtimeobject
    windowsapp
    shcore
    dwmapi
    msimg32
    uxtheme
)
