# -----------------------------------------------------------------------------
#  SourceGlo Pro - portable build.
#
#  The hand-written Makefile stays the primary macOS path; it is faster and it
#  knows about bundles, codesigning and the .pkg installer. This file exists so
#  the same source builds on Windows, where none of that applies.
#
#  A Windows VST3 is a PE DLL and has to be compiled by MSVC on Windows. It
#  cannot be produced from the macOS development machine at all, so the Windows
#  route is .github/workflows/windows.yml on a GitHub runner, which also gates
#  the build on pluginval - the Windows counterpart of the auval check.
#
#  macOS (to check this file still configures and the sources still compile
#  under the JUCE CMake API):
#    cmake -B build-cmake && cmake --build build-cmake --parallel 2
# -----------------------------------------------------------------------------

cmake_minimum_required(VERSION 3.22)
project(SourceGloPro VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Use a local JUCE when one is given, otherwise fetch it. The fetch path is
# what lets a GitHub runner - or any Windows box that has never seen this Mac -
# build without being told where anything is.
if(DEFINED JUCE_PATH AND EXISTS "${JUCE_PATH}/CMakeLists.txt")
    message(STATUS "SourceGlo Pro: using JUCE at ${JUCE_PATH}")
    add_subdirectory("${JUCE_PATH}" "${CMAKE_BINARY_DIR}/JUCE" EXCLUDE_FROM_ALL)
elseif(EXISTS "$ENV{HOME}/Documents/JUCE/CMakeLists.txt")
    message(STATUS "SourceGlo Pro: using JUCE at $ENV{HOME}/Documents/JUCE")
    add_subdirectory("$ENV{HOME}/Documents/JUCE" "${CMAKE_BINARY_DIR}/JUCE" EXCLUDE_FROM_ALL)
else()
    message(STATUS "SourceGlo Pro: fetching JUCE 9.0.0")
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        9.0.0
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

# Identity must match JucePluginDefines.h and the macOS Makefile exactly, or a
# session saved on one platform will not find the plugin on the other.
# AU is macOS-only; Windows gets VST3 and the standalone.
if(APPLE)
    set(SGP_FORMATS VST3 AU Standalone)
else()
    set(SGP_FORMATS VST3 Standalone)
endif()

juce_add_plugin(SourceGloPro
    PRODUCT_NAME                "SourceGlo Pro"
    COMPANY_NAME                "Diamond Loopz"
    BUNDLE_ID                   com.diamondloopz.sourceglopro
    PLUGIN_MANUFACTURER_CODE    DmLz
    PLUGIN_CODE                 SGPr
    FORMATS                     ${SGP_FORMATS}
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD     FALSE
    VST3_CATEGORIES             Fx Analyzer Tools
    AU_MAIN_TYPE                kAudioUnitType_Effect
    DESCRIPTION                 "Production Intelligence for Better Mixes")

file(GLOB_RECURSE SGP_SOURCES CONFIGURE_DEPENDS
     "${CMAKE_CURRENT_SOURCE_DIR}/Source/*.cpp")
target_sources(SourceGloPro PRIVATE ${SGP_SOURCES})

target_include_directories(SourceGloPro PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source")

target_compile_definitions(SourceGloPro PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_STRICT_REFCOUNTEDPOINTER=1)

target_link_libraries(SourceGloPro PRIVATE
    juce::juce_audio_utils
    juce::juce_audio_devices
    juce::juce_audio_formats
    juce::juce_audio_processors
    juce::juce_audio_basics
    juce::juce_dsp
    juce::juce_gui_basics
    juce::juce_graphics
    juce::juce_events
    juce::juce_data_structures
    juce::juce_core
  PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)

# Artwork has to land where Assets::assetsDirectory() actually looks, and that
# differs per format:
#
#   VST3 (both platforms)  <bundle>/Contents/Resources/Assets
#     A Windows VST3 is a folder bundle too - the DLL sits in
#     Contents/x86_64-win/ - so the same Contents/Resources lookup works.
#   Standalone .exe        an Assets folder beside the executable
#
# Copying to $<TARGET_FILE_DIR:SourceGloPro> would put it next to the shared
# code library, where nothing looks for it. On The Drum King that shipped a
# Windows VST3 with no artwork at all, so the destination is spelled out per
# target here rather than shared.
function(sgp_copy_assets target destination)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Assets")
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                    "${CMAKE_CURRENT_SOURCE_DIR}/Assets"
                    "${destination}/Assets"
            COMMENT "Copying Assets into ${target}")
    endif()
endfunction()

if(TARGET SourceGloPro_VST3)
    sgp_copy_assets(SourceGloPro_VST3
                    "$<TARGET_FILE_DIR:SourceGloPro_VST3>/../Resources")
endif()
if(TARGET SourceGloPro_AU)
    sgp_copy_assets(SourceGloPro_AU
                    "$<TARGET_FILE_DIR:SourceGloPro_AU>/../Resources")
endif()
if(TARGET SourceGloPro_Standalone)
    # macOS builds the standalone as an .app bundle, so its payload belongs in
    # Contents/Resources like the plugins. Only Windows produces a bare .exe
    # with the payload beside it.
    if(APPLE)
        sgp_copy_assets(SourceGloPro_Standalone
                        "$<TARGET_FILE_DIR:SourceGloPro_Standalone>/../Resources")
    else()
        sgp_copy_assets(SourceGloPro_Standalone
                        "$<TARGET_FILE_DIR:SourceGloPro_Standalone>")
    endif()
endif()
