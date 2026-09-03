cmake_minimum_required(VERSION 3.22)

if(NOT DEFINED SOURCE_BUNDLE OR NOT IS_DIRECTORY "${SOURCE_BUNDLE}")
    message(FATAL_ERROR "Cosmic Microwave source VST3 bundle is missing: ${SOURCE_BUNDLE}")
endif()

if(NOT DEFINED USER_VST3_ROOT OR USER_VST3_ROOT STREQUAL "")
    message(FATAL_ERROR "USER_VST3_ROOT must identify the user's VST3 directory")
endif()

function(remove_bundle bundle_path required)
    if(NOT EXISTS "${bundle_path}")
        return()
    endif()

    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E rm -rf "${bundle_path}"
        RESULT_VARIABLE remove_result
        ERROR_VARIABLE remove_error
    )

    if(EXISTS "${bundle_path}")
        if(required)
            message(FATAL_ERROR
                "Could not replace ${bundle_path}: ${remove_error} (exit ${remove_result})")
        endif()

        # A normal user often cannot alter /Library. Keep local builds working,
        # but make the duplicate-CID condition explicit instead of hiding it.
        message(WARNING
            "Legacy same-CID VST3 remains and should be removed with administrator permission: ${bundle_path}")
    else()
        message(STATUS "Removed same-CID VST3: ${bundle_path}")
    endif()
endfunction()

set(canonical_bundle "${USER_VST3_ROOT}/Cosmic Microwave.vst3")
remove_bundle("${canonical_bundle}" TRUE)

set(user_legacy_bundles
    "${USER_VST3_ROOT}/SpektraSynth.vst3"
    "${USER_VST3_ROOT}/Audience Harmonic Synth.vst3"
    "${USER_VST3_ROOT}/Cosmic/Cosmic Microwave.vst3"
    "${USER_VST3_ROOT}/Cosmic/SpektraSynth.vst3"
    "${USER_VST3_ROOT}/Cosmic/Audience Harmonic Synth.vst3"
)

foreach(bundle_path IN LISTS user_legacy_bundles)
    remove_bundle("${bundle_path}" TRUE)
endforeach()

if(DEFINED SYSTEM_VST3_ROOT AND NOT SYSTEM_VST3_ROOT STREQUAL "")
    set(system_legacy_bundles
        "${SYSTEM_VST3_ROOT}/Cosmic Microwave.vst3"
        "${SYSTEM_VST3_ROOT}/SpektraSynth.vst3"
        "${SYSTEM_VST3_ROOT}/Audience Harmonic Synth.vst3"
        "${SYSTEM_VST3_ROOT}/Cosmic/Cosmic Microwave.vst3"
        "${SYSTEM_VST3_ROOT}/Cosmic/SpektraSynth.vst3"
        "${SYSTEM_VST3_ROOT}/Cosmic/Audience Harmonic Synth.vst3"
    )

    foreach(bundle_path IN LISTS system_legacy_bundles)
        remove_bundle("${bundle_path}" FALSE)
    endforeach()
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E copy_directory "${SOURCE_BUNDLE}" "${canonical_bundle}"
    RESULT_VARIABLE copy_result
    ERROR_VARIABLE copy_error
)

if(NOT copy_result EQUAL 0 OR NOT IS_DIRECTORY "${canonical_bundle}")
    message(FATAL_ERROR
        "Could not install Cosmic Microwave VST3: ${copy_error} (exit ${copy_result})")
endif()

message(STATUS "Installed canonical Cosmic Microwave VST3: ${canonical_bundle}")
