cmake_minimum_required(VERSION 3.22)

project(Composer VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

option(COMPOSER_BUILD_APP "Build the JUCE desktop application shell" ON)
option(COMPOSER_BUILD_TESTS "Build Composer tests" ON)

# MSVC reads source as the system code page (949/EUC-KR on Korean Windows), which trips
# C4819 on the UTF-8 third-party sources bundled by JUCE/HarfBuzz. Force UTF-8 for both
# source and execution charsets. Added before dependencies so JUCE's own targets inherit it.
if(MSVC)
    add_compile_options(/utf-8)
endif()

include(cmake/ToolchainPins.cmake)
include(cmake/Dependencies.cmake)
include(cmake/CompilerWarnings.cmake)

# Pure, dependency-free domain layer: types, tempo map, chord theory, project model. Header-only
# types plus the chord implementation. No JUCE, no filesystem, no device APIs.
add_library(composer_domain STATIC
    src/domain/Chord.cpp)
target_include_directories(composer_domain PUBLIC src)
composer_enable_warnings(composer_domain)

# Command/undo layer: the only legal write path to the project model.
add_library(composer_commands STATIC
    src/commands/ProjectEditor.cpp)
target_include_directories(composer_commands PUBLIC src)
target_link_libraries(composer_commands PUBLIC composer_domain)
composer_enable_warnings(composer_commands)

# Persistence: deterministic project.json serialize/parse. Pure, no JUCE.
add_library(composer_persistence STATIC
    src/persistence/ProjectSerializer.cpp)
target_include_directories(composer_persistence PUBLIC src)
target_link_libraries(composer_persistence PUBLIC composer_domain)
composer_enable_warnings(composer_persistence)

add_library(composer_audio_contracts INTERFACE)
target_include_directories(composer_audio_contracts INTERFACE src)
target_link_libraries(composer_audio_contracts INTERFACE composer_domain)

add_library(composer_audio_runtime STATIC
    src/audio/runtime/CanonicalVerse.cpp
    src/audio/runtime/ClickRenderer.cpp
    src/audio/runtime/FakeAudioDevice.cpp
    src/audio/runtime/ProjectCompiler.cpp
    src/audio/runtime/ProjectRenderer.cpp)
target_include_directories(composer_audio_runtime PUBLIC src)
target_link_libraries(composer_audio_runtime PUBLIC
    composer_audio_contracts
    composer_commands
    composer_persistence)
composer_enable_warnings(composer_audio_runtime)

add_executable(composer_fixture src/app/FixtureMain.cpp)
target_link_libraries(composer_fixture PRIVATE composer_audio_runtime)
composer_enable_warnings(composer_fixture)

if(COMPOSER_BUILD_APP AND MSVC)
    add_executable(composer_app WIN32
        src/app/Main.cpp
        src/app/MainComponent.cpp
        src/app/MainComponent.h)
    target_link_libraries(composer_app PRIVATE
        composer_audio_runtime
        juce::juce_audio_utils
        juce::juce_gui_extra)
    target_compile_definitions(composer_app PRIVATE
        JUCE_ASIO=0
        JUCE_STANDALONE_APPLICATION=1
        JUCE_USE_DIRECT2D=0
        JUCE_USE_CURL=0
        JUCE_WEB_BROWSER=0)
    composer_enable_warnings(composer_app)
elseif(COMPOSER_BUILD_APP)
    message(WARNING
        "JUCE 8 does not support MinGW. The Composer JUCE app target requires MSVC; "
        "foundation libraries, fixture, and release-gate tests remain enabled.")
endif()

if(COMPOSER_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
