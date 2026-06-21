find_program(COMPOSER_POWERSHELL NAMES powershell pwsh REQUIRED)

function(composer_add_test_binary target)
    add_executable(${target} ${ARGN})
    target_include_directories(${target} PRIVATE ${PROJECT_SOURCE_DIR} ${PROJECT_SOURCE_DIR}/src)
    target_link_libraries(${target} PRIVATE composer_audio_runtime)
    composer_enable_warnings(${target})
endfunction()

composer_add_test_binary(composer_unit_tests
    UnitMain.cpp
    unit/ContractTests.cpp
    unit/MidiSongTests.cpp)
add_test(NAME unit.contracts COMMAND composer_unit_tests)
set_tests_properties(unit.contracts PROPERTIES LABELS "unit;quick")

composer_add_test_binary(composer_integration_tests
    IntegrationMain.cpp
    integration/AudioFoundationTests.cpp
    integration/MidiSongFixtureTests.cpp
    integration/PlaybackEngineTests.cpp)
add_test(NAME integration.audio_foundation COMMAND composer_integration_tests)
set_tests_properties(integration.audio_foundation PROPERTIES LABELS "integration;audio;release")

if(MSVC)
    add_executable(composer_juce_unit_tests juce/JuceUnitMain.cpp)
    target_include_directories(composer_juce_unit_tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
    target_link_libraries(composer_juce_unit_tests PRIVATE
        composer_audio_runtime
        juce::juce_core)
    target_compile_definitions(composer_juce_unit_tests PRIVATE JUCE_USE_CURL=0)
    composer_enable_warnings(composer_juce_unit_tests)
    add_test(NAME unit.juce_runner COMMAND composer_juce_unit_tests)
    set_tests_properties(unit.juce_runner PROPERTIES LABELS "unit;quick;release")
endif()

add_test(NAME smoke.canonical_fixture
    COMMAND composer_fixture --fixture canonical-verse)
set_tests_properties(smoke.canonical_fixture PROPERTIES LABELS "integration;smoke;release")

add_test(NAME contract.realtime_source
    COMMAND ${COMPOSER_POWERSHELL} -NoProfile -ExecutionPolicy Bypass
        -File ${PROJECT_SOURCE_DIR}/tests/contracts/VerifyRealtimeSource.ps1
        -SourceRoot ${PROJECT_SOURCE_DIR})
set_tests_properties(contract.realtime_source PROPERTIES LABELS "contract;audio;release")

add_test(NAME contract.error_registry
    COMMAND ${COMPOSER_POWERSHELL} -NoProfile -ExecutionPolicy Bypass
        -File ${PROJECT_SOURCE_DIR}/tests/contracts/VerifyErrorRegistry.ps1
        -SourceRoot ${PROJECT_SOURCE_DIR})
set_tests_properties(contract.error_registry PROPERTIES LABELS "contract;quick;release")

add_test(NAME contract.license_inventory
    COMMAND ${COMPOSER_POWERSHELL} -NoProfile -ExecutionPolicy Bypass
        -File ${PROJECT_SOURCE_DIR}/tests/contracts/VerifyLicenseInventory.ps1
        -SourceRoot ${PROJECT_SOURCE_DIR})
set_tests_properties(contract.license_inventory PROPERTIES LABELS "contract;quick;release")
