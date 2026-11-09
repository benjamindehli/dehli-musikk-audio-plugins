cmake_minimum_required(VERSION 3.22)

# Before project(), since project() creates both as empty cache entries and a
# set(... CACHE ...) after it is ignored. A universal binary, so one build runs
# on Apple Silicon and Intel Macs. CMAKE_HOST_APPLE, since project() is what
# defines APPLE.
if(CMAKE_HOST_APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Minimum macOS version")
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Build architectures")
endif()

project(DehliMusikkAudioPlugins VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Extracted files get the time they were extracted, so a new archive rebuilds
# what depends on it.
if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()

include(FetchContent)

# ------------------------------------------------------------------------------
# JUCE 9.0.5, fetched as the archive of the commit its tag points to and checked
# against that archive's hash. Point -DJUCE_PATH at a checkout to skip the
# download.
# ------------------------------------------------------------------------------
set(JUCE_PATH "" CACHE PATH "Path to an existing JUCE checkout")

if(JUCE_PATH)
    add_subdirectory("${JUCE_PATH}" JUCE)
else()
    FetchContent_Declare(JUCE
        URL      https://github.com/juce-framework/JUCE/archive/d60b7a78030661574a615b586d9325ba7a7fa7dd.tar.gz
        URL_HASH SHA256=8237b2976871982eb9107fd67b576d95bf02355d99540a0127c4e7c001ecd9f5)
    FetchContent_MakeAvailable(JUCE)
endif()

# ------------------------------------------------------------------------------
# dehli-musikk-plugin-core: the output stage and its machines, and the look and
# controls, shared with Overtonium. Pinned the same way as JUCE, and after it,
# since its GUI module is only there when JUCE is. To work on the library and
# the plugins together, point CMake at a checkout of it:
#
#   -DFETCHCONTENT_SOURCE_DIR_DM_PLUGIN_CORE=../dehli-musikk-plugin-core
# ------------------------------------------------------------------------------
FetchContent_Declare(dm_plugin_core
    URL      https://github.com/benjamindehli/dehli-musikk-plugin-core/archive/384735b56ab82c8453b75de107b2d62e1eb74326.tar.gz
    URL_HASH SHA256=9674199e0e405449c3c7d4378e1bb4b6c9d4f447a486d936a33d38abf5610cf6)
FetchContent_MakeAvailable(dm_plugin_core)

# ------------------------------------------------------------------------------
# Warnings as errors, on by default so a plain build here is the build CI does.
# Configure with -DDM_WARNINGS_AS_ERRORS=OFF for a compiler this project has
# not met. JUCE's modules compile into the targets that use them, so they get
# these options too, and a JUCE file that warns is silenced for that file and
# that check alone.
# ------------------------------------------------------------------------------
option(DM_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" ON)

function(dm_set_warnings_as_errors target)
    if(NOT DM_WARNINGS_AS_ERRORS)
        return()
    endif()

    if(MSVC)
        target_compile_options(${target} PRIVATE /WX)
    else()
        target_compile_options(${target} PRIVATE -Werror)
    endif()

    # A function defined with no declaration before it, which almost always
    # belongs in an anonymous namespace. JUCE asks Clang for this as
    # -Wmissing-prototypes, and GCC has it under this name.
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        target_compile_options(${target} PRIVATE
            $<$<COMPILE_LANGUAGE:CXX>:-Wmissing-declarations>)
    endif()
endfunction()

# JUCE 9.0.5's alert window code takes a reference through a temporary that
# GCC 13 and newer warn about, though it is sound. Silenced for that one JUCE
# file on GCC, and our own code keeps the check.
if(DM_WARNINGS_AS_ERRORS AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU"
   AND CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 13)
    set_source_files_properties(
        "${JUCE_MODULES_DIR}/juce_gui_basics/juce_gui_basics_2.cpp"
        PROPERTIES COMPILE_OPTIONS "-Wno-dangling-reference")
endif()

# The Ogg Vorbis library JUCE carries inside juce_audio_formats is C compiled
# as C++, and declares nothing before it defines it. Our own code keeps
# -Wmissing-declarations.
if(DM_WARNINGS_AS_ERRORS AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    set_source_files_properties(
        "${JUCE_MODULES_DIR}/juce_audio_formats/juce_audio_formats.cpp"
        PROPERTIES COMPILE_OPTIONS "-Wno-missing-declarations")
endif()

# ------------------------------------------------------------------------------
# The plugins
#
# Both are the same code around a different machine. Each gets its own name,
# bundle id and plugin code, so a host keeps them apart and a session saved
# with one never loads the other. The manufacturer code is Overtonium's, since
# it names the maker.
# ------------------------------------------------------------------------------
option(DM_INSTALL_AFTER_BUILD "Copy the built plugins into the user plugin folders" ON)

set(DM_FORMATS VST3 Standalone)

if(APPLE)
    list(APPEND DM_FORMATS AU)
endif()

if(UNIX AND NOT APPLE)
    list(APPEND DM_FORMATS LV2)
endif()

set(DM_SOURCES
    Source/Parameters.cpp
    Source/PluginEditor.cpp
    Source/PluginProcessor.cpp)

# What every target that compiles the plugin's code shares.
function(dm_configure target)
    target_sources(${target} PRIVATE ${DM_SOURCES})
    target_include_directories(${target} PRIVATE Source)

    target_compile_definitions(${target} PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_USE_WEBP=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_STRICT_REFCOUNTEDPOINTER=1
        DM_PLUGINS_VERSION="${PROJECT_VERSION}")

    target_link_libraries(${target}
        PRIVATE
            dm::plugin_core
            dm::dm_gui
            juce::juce_audio_utils
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)

    dm_set_warnings_as_errors(${target})
endfunction()

# Adds one plugin, named after its machine: Console or Cassette, the name of a
# dm::ClipType. The target takes the same name.
function(dm_add_machine machine code category description)
    set(target ${machine})
    string(TOLOWER "${machine}" lower)

    juce_add_plugin(${target}
        PRODUCT_NAME                "${machine}"
        COMPANY_NAME                "DehliMusikk"
        BUNDLE_ID                   "com.dehlimusikk.${lower}"
        PLUGIN_MANUFACTURER_CODE    Dhmk
        PLUGIN_CODE                 ${code}
        FORMATS                     ${DM_FORMATS}
        LV2URI                      "urn:dehlimusikk:${lower}"
        IS_SYNTH                    FALSE
        NEEDS_MIDI_INPUT            FALSE
        NEEDS_MIDI_OUTPUT           FALSE
        IS_MIDI_EFFECT              FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS FALSE
        COPY_PLUGIN_AFTER_BUILD     ${DM_INSTALL_AFTER_BUILD}
        VST3_CATEGORIES             Fx ${category}
        AU_MAIN_TYPE                kAudioUnitType_Effect
        DESCRIPTION                 "${description}"
        MICROPHONE_PERMISSION_ENABLED FALSE)

    target_sources(${target} PRIVATE Source/PluginEntry.cpp)
    target_compile_definitions(${target} PRIVATE DM_MACHINE=${machine})
    target_link_libraries(${target} PUBLIC juce::juce_recommended_lto_flags)

    dm_configure(${target})
endfunction()

dm_add_machine(Console Dcon Dynamics
    "A console's mix bus, measured from a real chain")
dm_add_machine(Cassette Dcas Distortion
    "A four-track cassette recorder with Dolby B, measured from a real deck")

# ------------------------------------------------------------------------------
# Tests
#
# One console program that builds the plugin's code once and tests both
# machines with no host and no display.
# ------------------------------------------------------------------------------
option(DM_BUILD_TESTS "Build the tests" ON)

if(DM_BUILD_TESTS)
    enable_testing()

    juce_add_console_app(dm_plugins_test PRODUCT_NAME "dm_plugins_test")
    target_sources(dm_plugins_test PRIVATE Tests/plugin_test.cpp)
    target_compile_definitions(dm_plugins_test PUBLIC
        JUCE_STANDALONE_APPLICATION=1)

    dm_configure(dm_plugins_test)

    add_test(NAME plugins COMMAND dm_plugins_test)
endif()
