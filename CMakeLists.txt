cmake_minimum_required(VERSION 3.22)
# Versionsnummer steht in der Datei VERSION (wird bei jeder Aenderung hochgezaehlt)
file(STRINGS VERSION GALE_VERSION LIMIT_COUNT 1)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS VERSION)
project(Gale VERSION ${GALE_VERSION})

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Laufzeitbibliothek fest einbinden: das Plugin braucht dann kein VC++-Redistributable
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

option(AUDIO_DSP_ONLY "Build portable audio regression tests without JUCE" OFF)
include(CTest)
if(BUILD_TESTING)
    add_executable(AudioDSPTests Tests/DSPTests.cpp)
    target_include_directories(AudioDSPTests PRIVATE Source)
    add_test(NAME AudioDSP COMMAND AudioDSPTests)
endif()
add_executable(RenderDemo EXCLUDE_FROM_ALL tools/render_demo.cpp)
if(AUDIO_DSP_ONLY)
    return()
endif()

set(GALE_JUCE_PATH "" CACHE PATH "Lokaler JUCE-Ordner (leer = automatisch laden)")
set(GALE_FORMATS "VST3" CACHE STRING "Plugin-Formate")

if(GALE_JUCE_PATH)
    add_subdirectory(${GALE_JUCE_PATH} JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.9
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

juce_add_binary_data(GaleAssets SOURCES Assets/logo.png)

# Effekt mit Stereo-Ein-/Ausgang, ohne MIDI: der Wind wird zum Eingang addiert, GALE laeuft auf FX-Kanal oder Bus
juce_add_plugin(Gale
    COMPANY_NAME "Stromschlag Audio"
    PLUGIN_MANUFACTURER_CODE Strm
    PLUGIN_CODE Gale
    FORMATS ${GALE_FORMATS}
    PRODUCT_NAME "Gale"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    VST3_CATEGORIES Fx
    COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(Gale PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_compile_definitions(Gale PUBLIC
    PLUGIN_VERSION_TEXT="${GALE_VERSION}"
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

if(MSVC)
    target_compile_options(Gale PRIVATE /utf-8)
endif()

target_link_libraries(Gale
    PRIVATE
        GaleAssets
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

if(BUILD_TESTING)
    juce_add_console_app(AudioProcessorTests PRODUCT_NAME "Audio processor regression tests")
    target_sources(AudioProcessorTests PRIVATE Tests/ProcessorTests.cpp)
    target_include_directories(AudioProcessorTests PRIVATE Source $<TARGET_PROPERTY:Gale,INCLUDE_DIRECTORIES>)
    target_compile_definitions(AudioProcessorTests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(AudioProcessorTests PRIVATE Gale)
    add_test(NAME AudioProcessor COMMAND AudioProcessorTests)
endif()

# Screenshot-Werkzeug fuer die README (nicht Teil des Standard-Builds): cmake --build build --target GaleScreenshot
juce_add_console_app(GaleScreenshot PRODUCT_NAME "Gale screenshot" EXCLUDE_FROM_ALL)
target_sources(GaleScreenshot PRIVATE tools/screenshot.cpp)
target_include_directories(GaleScreenshot PRIVATE Source $<TARGET_PROPERTY:Gale,INCLUDE_DIRECTORIES>)
target_compile_definitions(GaleScreenshot PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 PLUGIN_VERSION_TEXT="${GALE_VERSION}")
target_link_libraries(GaleScreenshot PRIVATE Gale)
