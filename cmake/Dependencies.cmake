include(FetchContent)

# Composer does not use JUCE's code-generating CMake helpers. Keeping the dependency in
# modules-only mode avoids a configure-time host-tool build and leaves module targets available.
set(JUCE_MODULES_ONLY ON CACHE BOOL "Composer uses JUCE module targets directly" FORCE)

# Release tag 8.0.13 resolves to commit 7c9d3783b127263d72bb65fe0a7e2dc8a02a7ac2.
set(COMPOSER_JUCE_VERSION "8.0.13" CACHE STRING "Pinned JUCE version")
set(COMPOSER_JUCE_ARCHIVE "" CACHE FILEPATH "Optional offline JUCE source archive")
set(_composer_juce_sha256 "97c3c5cf039d8ba45378397c3d6c1033c3fc85102c928054a77e8857031ecae3")

if(COMPOSER_JUCE_ARCHIVE)
    if(NOT EXISTS "${COMPOSER_JUCE_ARCHIVE}")
        message(FATAL_ERROR "DX-JUCE-004: COMPOSER_JUCE_ARCHIVE does not exist: ${COMPOSER_JUCE_ARCHIVE}")
    endif()
    set(_composer_juce_url "${COMPOSER_JUCE_ARCHIVE}")
else()
    set(_composer_juce_url
        "https://github.com/juce-framework/JUCE/archive/refs/tags/${COMPOSER_JUCE_VERSION}.zip")
endif()

FetchContent_Declare(JUCE
    URL "${_composer_juce_url}"
    URL_HASH "SHA256=${_composer_juce_sha256}"
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(JUCE)
