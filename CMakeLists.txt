cmake_minimum_required(VERSION 3.22)
project(AudienceHarmonicSynth VERSION 1.0.35 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "macOS deployment target")

set(AUDIENCE_HARMONIC_SYNTH_VERSION "${PROJECT_VERSION}")
set(AUDIENCE_HARMONIC_SYNTH_FORMATS VST3 Standalone)
set(AUDIENCE_HARMONIC_SYNTH_NEEDS_MIDI_INPUT TRUE)
set(AUDIENCE_HARMONIC_SYNTH_NEEDS_MIDI_OUTPUT TRUE)

if(ANDROID)
    set(AUDIENCE_HARMONIC_SYNTH_VERSION "1.0.1")
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
    COPY_PLUGIN_AFTER_BUILD     TRUE
    PLUGIN_MANUFACTURER_CODE    Mhmt
    PLUGIN_CODE                 Ahss
    FORMATS                     ${AUDIENCE_HARMONIC_SYNTH_FORMATS}
    PRODUCT_NAME                "SpektraSynth"
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
    Source/MpeMidiOutput.cpp
    Source/PluginEditor.cpp
    Source/AuroraComponent.cpp
    Source/LibraryRail.cpp
    Source/DebugPanel.cpp
    Source/AtomicScaleBuilder.cpp
    Source/ElementSpectralData.cpp
    Source/PartialEngine.cpp
    Source/SampleLibrary.cpp
    Source/OscBridge.cpp
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
    COPY_PLUGIN_AFTER_BUILD     TRUE
    PLUGIN_MANUFACTURER_CODE    Mhmt
    PLUGIN_CODE                 Ahmd
    FORMATS                     VST3 Standalone
    PRODUCT_NAME                "SpektraSynth MIDI"
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
    COPY_PLUGIN_AFTER_BUILD     TRUE
    PLUGIN_MANUFACTURER_CODE    Mhmt
    PLUGIN_CODE                 Amgn
    FORMATS                     VST3
    PRODUCT_NAME                "SpektraSynth MIDI Generator"
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
    PRODUCT_NAME                "SpektraSynth MIDI Device"
)

juce_generate_juce_header(AudienceMidiDevice)

target_sources(AudienceMidiDevice PRIVATE
    Source/AudienceMidiDeviceApp.cpp
    Source/MidiEngine.cpp
    Source/OscBridge.cpp
    Source/Simulator.cpp
)

target_compile_definitions(AudienceHarmonicSynth PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    AUDIENCE_SYNTH_SOURCE_SAMPLES_PATH="${CMAKE_CURRENT_SOURCE_DIR}/Samples"
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
    juce::juce_audio_formats
    juce::juce_dsp
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

option(AUDIENCE_SYNTH_BUILD_TESTS "Build Audience Harmonic Synth test harnesses" ON)

if(AUDIENCE_SYNTH_BUILD_TESTS)
    enable_testing()

    add_executable(AudienceSignalFlowTests
        Tests/SignalFlowTests.cpp
        Source/AtomicScaleBuilder.cpp
        Source/ElementSpectralData.cpp
        Source/PartialEngine.cpp
        Source/SampleLibrary.cpp
    )

    target_compile_definitions(AudienceSignalFlowTests PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        AUDIENCE_SYNTH_SOURCE_SAMPLES_PATH="${CMAKE_CURRENT_SOURCE_DIR}/Samples"
    )

    target_link_libraries(AudienceSignalFlowTests PRIVATE
        juce::juce_audio_formats
        juce::juce_audio_basics
        juce::juce_dsp
        juce::juce_core
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
    )

    add_test(NAME AudienceSignalFlowTests COMMAND AudienceSignalFlowTests)

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

    add_executable(AudienceSampleLibraryTests
        Tests/SampleLibraryTests.cpp
        Source/SampleLibrary.cpp
    )

    target_compile_definitions(AudienceSampleLibraryTests PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        AUDIENCE_SYNTH_SOURCE_SAMPLES_PATH="${CMAKE_CURRENT_SOURCE_DIR}/Samples"
    )

    target_link_libraries(AudienceSampleLibraryTests PRIVATE
        juce::juce_audio_formats
        juce::juce_audio_basics
        juce::juce_core
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
    )

    add_test(NAME AudienceSampleLibraryTests COMMAND AudienceSampleLibraryTests)

    add_executable(AudiencePerformanceSmokeTests
        Tests/PerformanceSmokeTests.cpp
        Source/AtomicScaleBuilder.cpp
        Source/ElementSpectralData.cpp
        Source/PartialEngine.cpp
        Source/SampleLibrary.cpp
    )

    target_compile_definitions(AudiencePerformanceSmokeTests PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        AUDIENCE_SYNTH_SOURCE_SAMPLES_PATH="${CMAKE_CURRENT_SOURCE_DIR}/Samples"
    )

    target_link_libraries(AudiencePerformanceSmokeTests PRIVATE
        juce::juce_audio_formats
        juce::juce_audio_basics
        juce::juce_dsp
        juce::juce_core
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
    )

    add_test(NAME AudiencePerformanceSmokeTests COMMAND AudiencePerformanceSmokeTests)
endif()

if(NOT ANDROID)
    foreach(format_target IN ITEMS AudienceHarmonicSynth_VST3 AudienceHarmonicSynth_Standalone)
        if(APPLE)
            add_custom_command(TARGET ${format_target} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_directory
                    "${CMAKE_CURRENT_SOURCE_DIR}/Samples"
                    "$<TARGET_BUNDLE_CONTENT_DIR:${format_target}>/Resources/Samples"
                COMMAND /usr/bin/find
                    "$<TARGET_BUNDLE_CONTENT_DIR:${format_target}>/Resources/Samples"
                    -name "*.orig" -delete
                COMMAND /usr/bin/find
                    "$<TARGET_BUNDLE_CONTENT_DIR:${format_target}>/Resources/Samples"
                    -name ".DS_Store" -delete
                COMMAND codesign --force --deep --sign - "$<TARGET_BUNDLE_DIR:${format_target}>"
                COMMENT "Copying sample libraries into ${format_target} resources"
            )
        else()
            add_custom_command(TARGET ${format_target} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_directory
                    "${CMAKE_CURRENT_SOURCE_DIR}/Samples"
                    "$<TARGET_FILE_DIR:${format_target}>/Resources/Samples"
                COMMENT "Copying sample libraries into ${format_target} resources"
            )
        endif()
    endforeach()
endif()

if(APPLE AND NOT ANDROID)
    add_custom_command(TARGET AudienceHarmonicSynth_VST3 POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E rm -rf
            "$ENV{HOME}/Library/Audio/Plug-Ins/VST3/Audience Harmonic Synth.vst3"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "$<TARGET_BUNDLE_DIR:AudienceHarmonicSynth_VST3>"
            "$ENV{HOME}/Library/Audio/Plug-Ins/VST3/Audience Harmonic Synth.vst3"
        COMMENT "Installing cleaned VST3 bundle"
    )
endif()

if(APPLE AND NOT ANDROID)
    add_custom_command(TARGET AudienceHarmonicMidi_VST3 POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E rm -rf
            "$ENV{HOME}/Library/Audio/Plug-Ins/VST3/Audience Harmonic MIDI.vst3"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "$<TARGET_BUNDLE_DIR:AudienceHarmonicMidi_VST3>"
            "$ENV{HOME}/Library/Audio/Plug-Ins/VST3/Audience Harmonic MIDI.vst3"
        COMMENT "Installing MIDI VST3 bundle"
    )
endif()

if(APPLE AND NOT ANDROID)
    add_custom_command(TARGET AudienceMidiGenerator_VST3 POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E rm -rf
            "$ENV{HOME}/Library/Audio/Plug-Ins/VST3/Audience MIDI Generator.vst3"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "$<TARGET_BUNDLE_DIR:AudienceMidiGenerator_VST3>"
            "$ENV{HOME}/Library/Audio/Plug-Ins/VST3/Audience MIDI Generator.vst3"
        COMMENT "Installing Ableton MIDI Generator VST3 bundle"
    )
endif()
