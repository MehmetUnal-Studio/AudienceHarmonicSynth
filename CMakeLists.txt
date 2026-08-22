cmake_minimum_required(VERSION 3.22)

# This must be initialised before project(). Otherwise CMake creates an empty
# cache entry during compiler detection and the native SDK version silently
# becomes the plug-in's minimum supported macOS version.
if(NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET OR CMAKE_OSX_DEPLOYMENT_TARGET STREQUAL "")
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "macOS deployment target" FORCE)
endif()

project(AudienceHarmonicSynth VERSION 2.3.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(AUDIENCE_HARMONIC_SYNTH_VERSION "${PROJECT_VERSION}")
set(AUDIENCE_HARMONIC_SYNTH_FORMATS VST3 Standalone)
set(AUDIENCE_HARMONIC_SYNTH_NEEDS_MIDI_INPUT TRUE)
set(AUDIENCE_HARMONIC_SYNTH_NEEDS_MIDI_OUTPUT TRUE)
option(AUDIENCE_SYNTH_AUTO_INSTALL "Auto-install plug-ins to the user VST3 folder after build (macOS dev convenience)" ON)

if(ANDROID)
    set(AUDIENCE_HARMONIC_SYNTH_VERSION "${PROJECT_VERSION}")
    set(AUDIENCE_HARMONIC_SYNTH_FORMATS Standalone)
    set(AUDIENCE_HARMONIC_SYNTH_NEEDS_MIDI_INPUT FALSE)
    set(AUDIENCE_HARMONIC_SYNTH_NEEDS_MIDI_OUTPUT FALSE)
endif()

# Fetch JUCE (pin a stable release)
include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.4
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(AudienceHarmonicSynth
    COMPANY_NAME                "MehmetUnal"
    BUNDLE_ID                   "com.mehmetunal.spektrasynth"
    VERSION                     "${AUDIENCE_HARMONIC_SYNTH_VERSION}"
    IS_SYNTH                    TRUE
    NEEDS_MIDI_INPUT            ${AUDIENCE_HARMONIC_SYNTH_NEEDS_MIDI_INPUT}
    NEEDS_MIDI_OUTPUT           ${AUDIENCE_HARMONIC_SYNTH_NEEDS_MIDI_OUTPUT}
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    # The explicit install step below removes legacy same-CID bundle names
    # before copying the one canonical Cosmic Microwave bundle.
    COPY_PLUGIN_AFTER_BUILD     FALSE
    PLUGIN_MANUFACTURER_CODE    Mhmt
    PLUGIN_CODE                 Ahss
    FORMATS                     ${AUDIENCE_HARMONIC_SYNTH_FORMATS}
    PRODUCT_NAME                "Cosmic Microwave"
)

juce_generate_juce_header(AudienceHarmonicSynth)

if(ANDROID)
    set_target_properties(AudienceHarmonicSynth_Standalone PROPERTIES
        OUTPUT_NAME juce_jni
        C_VISIBILITY_PRESET default
        CXX_VISIBILITY_PRESET default
        VISIBILITY_INLINES_HIDDEN FALSE
    )
endif()

target_sources(AudienceHarmonicSynth PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginStateMigration.cpp
    Source/MpeMidiOutput.cpp
    Source/PluginEditor.cpp
    Source/AtomicScaleMap.cpp
    Source/AtomicScaleCatalog.cpp
    Source/CrowdTimeField.cpp
    Source/MidiAudienceModel.cpp
    Source/MidiPitchMap.cpp
    Source/OscBridge.cpp
    Source/OscFingerRouter.cpp
    Source/Simulator.cpp
)

juce_add_plugin(AudienceHarmonicMidi
    COMPANY_NAME                "MehmetUnal"
    BUNDLE_ID                   "com.mehmetunal.spektrasynthmidi"
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            TRUE
    NEEDS_MIDI_OUTPUT           TRUE
    IS_MIDI_EFFECT              TRUE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD     ${AUDIENCE_SYNTH_AUTO_INSTALL}
    PLUGIN_MANUFACTURER_CODE    Mhmt
    PLUGIN_CODE                 Ahmd
    FORMATS                     VST3 Standalone
    PRODUCT_NAME                "Cosmic Microwave MIDI"
)

juce_generate_juce_header(AudienceHarmonicMidi)

target_sources(AudienceHarmonicMidi PRIVATE
    Source/MidiProcessor.cpp
    Source/MidiGeneratorEditor.cpp
    Source/MidiEngine.cpp
    Source/MidiScaleModule.cpp
    Source/OscBridge.cpp
    Source/Simulator.cpp
)

juce_add_plugin(AudienceMidiGenerator
    COMPANY_NAME                "MehmetUnal"
    BUNDLE_ID                   "com.mehmetunal.spektrasynthmidigenerator"
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            TRUE
    NEEDS_MIDI_OUTPUT           TRUE
    IS_MIDI_EFFECT              TRUE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD     ${AUDIENCE_SYNTH_AUTO_INSTALL}
    PLUGIN_MANUFACTURER_CODE    Mhmt
    PLUGIN_CODE                 Amgn
    FORMATS                     VST3
    PRODUCT_NAME                "Cosmic Microwave MIDI Generator"
)

juce_generate_juce_header(AudienceMidiGenerator)

target_sources(AudienceMidiGenerator PRIVATE
    Source/MidiProcessor.cpp
    Source/MidiGeneratorEditor.cpp
    Source/MidiEngine.cpp
    Source/MidiScaleModule.cpp
    Source/OscBridge.cpp
    Source/Simulator.cpp
)

juce_add_gui_app(AudienceMidiDevice
    COMPANY_NAME                "MehmetUnal"
    BUNDLE_ID                   "com.mehmetunal.spektrasynthmididevice"
    PRODUCT_NAME                "Cosmic Microwave MIDI Device"
)

juce_generate_juce_header(AudienceMidiDevice)

target_sources(AudienceMidiDevice PRIVATE
    Source/AudienceMidiDeviceApp.cpp
    Source/MidiEngine.cpp
    Source/OscBridge.cpp
    Source/Simulator.cpp
)

target_compile_definitions(AudienceHarmonicSynth PRIVATE
    COSMIC_MIDI_DIAGNOSTICS=$<CONFIG:Debug>
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)

target_compile_definitions(AudienceHarmonicMidi PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)

target_compile_definitions(AudienceMidiGenerator PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)

target_compile_definitions(AudienceMidiDevice PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
)

target_link_libraries(AudienceHarmonicSynth PRIVATE
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_audio_devices
    juce::juce_audio_basics
    juce::juce_gui_basics
    juce::juce_gui_extra
    juce::juce_osc
    juce::juce_core
    juce::juce_data_structures
    juce::juce_events
    juce::juce_graphics
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)

target_link_libraries(AudienceHarmonicMidi PRIVATE
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_audio_devices
    juce::juce_audio_basics
    juce::juce_gui_basics
    juce::juce_gui_extra
    juce::juce_osc
    juce::juce_core
    juce::juce_data_structures
    juce::juce_events
    juce::juce_graphics
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)

target_link_libraries(AudienceMidiGenerator PRIVATE
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_audio_devices
    juce::juce_audio_basics
    juce::juce_gui_basics
    juce::juce_gui_extra
    juce::juce_osc
    juce::juce_core
    juce::juce_data_structures
    juce::juce_events
    juce::juce_graphics
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)

target_link_libraries(AudienceMidiDevice PRIVATE
    juce::juce_audio_devices
    juce::juce_audio_basics
    juce::juce_gui_basics
    juce::juce_gui_extra
    juce::juce_osc
    juce::juce_core
    juce::juce_data_structures
    juce::juce_events
    juce::juce_graphics
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)

option(AUDIENCE_SYNTH_BUILD_TESTS "Build Cosmic Microwave MIDI routing test harnesses" ON)

if(AUDIENCE_SYNTH_BUILD_TESTS)
    enable_testing()

    # Developer-only source-data compiler. It is deliberately excluded from the
    # default build: the shipped plug-in consumes the compact generated catalog,
    # while this tool retains the full scientific source dataset and builder.
    add_executable(GenerateAtomicScaleCatalog EXCLUDE_FROM_ALL
        tools/GenerateAtomicScaleCatalog.cpp
        Source/AtomicScaleBuilder.cpp
        Source/ElementSpectralData.cpp
    )

    target_compile_definitions(GenerateAtomicScaleCatalog PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
    )

    target_link_libraries(GenerateAtomicScaleCatalog PRIVATE
        juce::juce_core
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
    )

    add_executable(AudienceMidiMappingTests
        Tests/MidiMappingTests.cpp
        Source/MidiEngine.cpp
    )

    target_compile_definitions(AudienceMidiMappingTests PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
    )

    target_link_libraries(AudienceMidiMappingTests PRIVATE
        juce::juce_audio_basics
        juce::juce_core
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
    )

    add_test(NAME AudienceMidiMappingTests COMMAND AudienceMidiMappingTests)

    add_executable(AudienceMidiScaleModuleTests
        Tests/MidiScaleModuleTests.cpp
        Source/MidiScaleModule.cpp
    )

    target_compile_definitions(AudienceMidiScaleModuleTests PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
    )

    target_link_libraries(AudienceMidiScaleModuleTests PRIVATE
        juce::juce_audio_basics
        juce::juce_core
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
    )

    add_test(NAME AudienceMidiScaleModuleTests COMMAND AudienceMidiScaleModuleTests)

    add_executable(AudienceMidiPitchTests
        Tests/MidiPitchTests.cpp
    )

    target_compile_definitions(AudienceMidiPitchTests PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
    )

    target_link_libraries(AudienceMidiPitchTests PRIVATE
        juce::juce_core
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
    )

    add_test(NAME AudienceMidiPitchTests COMMAND AudienceMidiPitchTests)

    add_executable(AudienceMpeOutputTests
        Tests/MpeOutputTests.cpp
        Source/MpeMidiOutput.cpp
    )

    target_compile_definitions(AudienceMpeOutputTests PRIVATE
        COSMIC_MIDI_DIAGNOSTICS=1
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
    )

    target_link_libraries(AudienceMpeOutputTests PRIVATE
        juce::juce_audio_basics
        juce::juce_core
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
    )

    add_test(NAME AudienceMpeOutputTests COMMAND AudienceMpeOutputTests)

    add_executable(AudienceOscBridgeTests
        Tests/OscBridgeTests.cpp
        Source/OscBridge.cpp
    )

    target_compile_definitions(AudienceOscBridgeTests PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
    )

    target_link_libraries(AudienceOscBridgeTests PRIVATE
        juce::juce_osc
        juce::juce_events
        juce::juce_core
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
    )

    add_test(NAME AudienceOscBridgeTests COMMAND AudienceOscBridgeTests)

    add_executable(AudienceOscFingerRouterTests
        Tests/OscFingerRouterTests.cpp
        Source/OscFingerRouter.cpp
    )

    target_compile_definitions(AudienceOscFingerRouterTests PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
    )

    target_link_libraries(AudienceOscFingerRouterTests PRIVATE
        juce::juce_core
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
    )

    add_test(NAME AudienceOscFingerRouterTests COMMAND AudienceOscFingerRouterTests)

    add_executable(AudienceMidiAudienceModelTests
        Tests/MidiAudienceModelTests.cpp
        Source/MidiAudienceModel.cpp
        Source/OscFingerRouter.cpp
    )

    target_compile_definitions(AudienceMidiAudienceModelTests PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
    )

    target_link_libraries(AudienceMidiAudienceModelTests PRIVATE
        juce::juce_core
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
    )

    add_test(NAME AudienceMidiAudienceModelTests COMMAND AudienceMidiAudienceModelTests)

    add_executable(AudienceMidiPitchMapTests
        Tests/MidiPitchMapTests.cpp
        Source/MidiPitchMap.cpp
    )

    target_link_libraries(AudienceMidiPitchMapTests PRIVATE
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
    )

    add_test(NAME AudienceMidiPitchMapTests COMMAND AudienceMidiPitchMapTests)

    add_executable(AudienceAtomicScaleBuilderTests
        Tests/AtomicScaleBuilderTests.cpp
        Source/AtomicScaleBuilder.cpp
    )

    target_compile_definitions(AudienceAtomicScaleBuilderTests PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
    )

    target_link_libraries(AudienceAtomicScaleBuilderTests PRIVATE
        juce::juce_core
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
    )

    add_test(NAME AudienceAtomicScaleBuilderTests COMMAND AudienceAtomicScaleBuilderTests)

    add_executable(AudienceAtomicScaleMapTests
        Tests/AtomicScaleMapTests.cpp
        Source/AtomicScaleMap.cpp
    )

    target_link_libraries(AudienceAtomicScaleMapTests PRIVATE
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
    )

    add_test(NAME AudienceAtomicScaleMapTests COMMAND AudienceAtomicScaleMapTests)

    add_executable(AudienceAtomicScaleCatalogTests
        Tests/AtomicScaleCatalogTests.cpp
        Source/AtomicScaleCatalog.cpp
        Source/AtomicScaleMap.cpp
    )

    target_link_libraries(AudienceAtomicScaleCatalogTests PRIVATE
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
    )

    add_test(NAME AudienceAtomicScaleCatalogTests COMMAND AudienceAtomicScaleCatalogTests)

    add_executable(AudienceAtomicMidiIntegrationTests
        Tests/AtomicMidiIntegrationTests.cpp
        Source/AtomicScaleCatalog.cpp
        Source/AtomicScaleMap.cpp
        Source/MpeMidiOutput.cpp
    )

    target_compile_definitions(AudienceAtomicMidiIntegrationTests PRIVATE
        COSMIC_MIDI_DIAGNOSTICS=1
    )

    target_link_libraries(AudienceAtomicMidiIntegrationTests PRIVATE
        juce::juce_audio_basics
        juce::juce_core
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
    )

    add_test(NAME AudienceAtomicMidiIntegrationTests COMMAND AudienceAtomicMidiIntegrationTests)

    add_executable(AudienceCrowdTimeFieldTests
        Tests/CrowdTimeFieldTests.cpp
        Source/CrowdTimeField.cpp
    )

    target_link_libraries(AudienceCrowdTimeFieldTests PRIVATE
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
    )

    add_test(NAME AudienceCrowdTimeFieldTests COMMAND AudienceCrowdTimeFieldTests)

    add_executable(AudienceCrowdMidiIntegrationTests
        Tests/CrowdMidiIntegrationTests.cpp
        Source/CrowdTimeField.cpp
        Source/MidiAudienceModel.cpp
        Source/MpeMidiOutput.cpp
        Source/OscFingerRouter.cpp
    )

    target_compile_definitions(AudienceCrowdMidiIntegrationTests PRIVATE
        COSMIC_MIDI_DIAGNOSTICS=1
    )

    target_link_libraries(AudienceCrowdMidiIntegrationTests PRIVATE
        juce::juce_audio_basics
        juce::juce_core
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
    )

    add_test(NAME AudienceCrowdMidiIntegrationTests COMMAND AudienceCrowdMidiIntegrationTests)

    add_executable(AudiencePluginStateMigrationTests
        Tests/PluginStateMigrationTests.cpp
        Source/PluginStateMigration.cpp
    )

    target_compile_definitions(AudiencePluginStateMigrationTests PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
    )

    target_link_libraries(AudiencePluginStateMigrationTests PRIVATE
        juce::juce_data_structures
        juce::juce_core
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
    )

    add_test(NAME AudiencePluginStateMigrationTests COMMAND AudiencePluginStateMigrationTests)
endif()

if(APPLE AND NOT ANDROID)
    foreach(format_target IN ITEMS AudienceHarmonicSynth_VST3 AudienceHarmonicSynth_Standalone)
        add_custom_command(TARGET ${format_target} POST_BUILD
            COMMAND codesign --force --deep --sign - "$<TARGET_BUNDLE_DIR:${format_target}>"
            COMMENT "Signing MIDI-only ${format_target} bundle"
        )
    endforeach()
endif()

# B15: the macOS auto-install (clean copy) of the AudienceHarmonicSynth VST3
# into the user plug-ins folder is the default local-dev workflow. Configure
# with -DAUDIENCE_SYNTH_AUTO_INSTALL=OFF for CI or build-only environments.
# The install removes legacy same-CID names before copying the canonical bundle.
if(AUDIENCE_SYNTH_AUTO_INSTALL AND APPLE AND NOT ANDROID)
    add_custom_command(TARGET AudienceHarmonicSynth_VST3 POST_BUILD
        COMMAND ${CMAKE_COMMAND}
            "-DSOURCE_BUNDLE=$<TARGET_BUNDLE_DIR:AudienceHarmonicSynth_VST3>"
            "-DUSER_VST3_ROOT=$ENV{HOME}/Library/Audio/Plug-Ins/VST3"
            "-DSYSTEM_VST3_ROOT=/Library/Audio/Plug-Ins/VST3"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/InstallCosmicMicrowaveVST3.cmake"
        COMMENT "Replacing legacy same-CID bundles with canonical Cosmic Microwave VST3"
    )
endif()

if(AUDIENCE_SYNTH_AUTO_INSTALL AND APPLE AND NOT ANDROID)
    add_custom_command(TARGET AudienceHarmonicMidi_VST3 POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E rm -rf
            "$ENV{HOME}/Library/Audio/Plug-Ins/VST3/Cosmic Microwave MIDI.vst3"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "$<TARGET_BUNDLE_DIR:AudienceHarmonicMidi_VST3>"
            "$ENV{HOME}/Library/Audio/Plug-Ins/VST3/Cosmic Microwave MIDI.vst3"
        COMMENT "Installing MIDI VST3 bundle"
    )
endif()

if(AUDIENCE_SYNTH_AUTO_INSTALL AND APPLE AND NOT ANDROID)
    add_custom_command(TARGET AudienceMidiGenerator_VST3 POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E rm -rf
            "$ENV{HOME}/Library/Audio/Plug-Ins/VST3/Cosmic Microwave MIDI Generator.vst3"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "$<TARGET_BUNDLE_DIR:AudienceMidiGenerator_VST3>"
            "$ENV{HOME}/Library/Audio/Plug-Ins/VST3/Cosmic Microwave MIDI Generator.vst3"
        COMMENT "Installing Ableton MIDI Generator VST3 bundle"
    )
endif()
