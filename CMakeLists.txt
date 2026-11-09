cmake_minimum_required(VERSION 3.24)

if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin")
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "macOS architectures" FORCE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version" FORCE)
endif()

project(OpenRbDelay VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(JUCE_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(JUCE_BUILD_EXTRAS OFF CACHE BOOL "" FORCE)

include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.16-security
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(JUCE)

find_program(CARGO_EXECUTABLE cargo HINTS
    "$ENV{HOME}/.cargo/bin"
    "$ENV{USERPROFILE}/.cargo/bin"
)
if(NOT CARGO_EXECUTABLE)
    message(FATAL_ERROR "cargo not found; install Rust from https://rustup.rs")
endif()

set(CARGO_TARGET_DIR "${CMAKE_BINARY_DIR}/cargo")
set(ORB_DELAY_LIBNAME "${CMAKE_STATIC_LIBRARY_PREFIX}orb_delay${CMAKE_STATIC_LIBRARY_SUFFIX}")

file(GLOB_RECURSE ORB_DELAY_RUST_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/src/*.rs"
    "${CMAKE_SOURCE_DIR}/third_party/openrb-338/*.rs"
    "${CMAKE_SOURCE_DIR}/third_party/openrb-338/*.toml"
)

if(APPLE)
    find_program(RUSTUP_EXECUTABLE rustup HINTS "$ENV{HOME}/.cargo/bin")
    if(NOT RUSTUP_EXECUTABLE)
        message(FATAL_ERROR "rustup not found; install Rust from https://rustup.rs")
    endif()

    set(ORB_DELAY_RUST_TRIPLES x86_64-apple-darwin aarch64-apple-darwin)
    set(ORB_DELAY_STATICLIB "${CARGO_TARGET_DIR}/universal/${ORB_DELAY_LIBNAME}")
    set(ORB_DELAY_ARCH_LIBS)
    foreach(triple IN LISTS ORB_DELAY_RUST_TRIPLES)
        list(APPEND ORB_DELAY_ARCH_LIBS
            "${CARGO_TARGET_DIR}/${triple}/release/${ORB_DELAY_LIBNAME}")
    endforeach()

    add_custom_command(
        OUTPUT "${ORB_DELAY_STATICLIB}"
        COMMAND "${RUSTUP_EXECUTABLE}" target add ${ORB_DELAY_RUST_TRIPLES}
        COMMAND "${CMAKE_COMMAND}" -E env
            "CARGO_TARGET_DIR=${CARGO_TARGET_DIR}"
            "MACOSX_DEPLOYMENT_TARGET=${CMAKE_OSX_DEPLOYMENT_TARGET}"
            "${CARGO_EXECUTABLE}" build --release --target x86_64-apple-darwin
            --manifest-path "${CMAKE_SOURCE_DIR}/Cargo.toml"
        COMMAND "${CMAKE_COMMAND}" -E env
            "CARGO_TARGET_DIR=${CARGO_TARGET_DIR}"
            "MACOSX_DEPLOYMENT_TARGET=${CMAKE_OSX_DEPLOYMENT_TARGET}"
            "${CARGO_EXECUTABLE}" build --release --target aarch64-apple-darwin
            --manifest-path "${CMAKE_SOURCE_DIR}/Cargo.toml"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${CARGO_TARGET_DIR}/universal"
        COMMAND /usr/bin/lipo -create ${ORB_DELAY_ARCH_LIBS} -output "${ORB_DELAY_STATICLIB}"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        DEPENDS
            "${CMAKE_SOURCE_DIR}/Cargo.toml"
            "${CMAKE_SOURCE_DIR}/Cargo.lock"
            "${CMAKE_SOURCE_DIR}/include/orb_delay.h"
            ${ORB_DELAY_RUST_SOURCES}
        BYPRODUCTS ${ORB_DELAY_ARCH_LIBS}
        COMMENT "Building universal orb_delay staticlib"
        VERBATIM
    )
else()
    set(ORB_DELAY_STATICLIB "${CARGO_TARGET_DIR}/release/${ORB_DELAY_LIBNAME}")
    add_custom_command(
        OUTPUT "${ORB_DELAY_STATICLIB}"
        COMMAND "${CMAKE_COMMAND}" -E env
            "CARGO_TARGET_DIR=${CARGO_TARGET_DIR}"
            "${CARGO_EXECUTABLE}" build --release --manifest-path "${CMAKE_SOURCE_DIR}/Cargo.toml"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        DEPENDS
            "${CMAKE_SOURCE_DIR}/Cargo.toml"
            "${CMAKE_SOURCE_DIR}/Cargo.lock"
            "${CMAKE_SOURCE_DIR}/include/orb_delay.h"
            ${ORB_DELAY_RUST_SOURCES}
        BYPRODUCTS "${ORB_DELAY_STATICLIB}"
        COMMENT "Building orb_delay staticlib"
        VERBATIM
    )
endif()
add_custom_target(orb_delay_rust DEPENDS "${ORB_DELAY_STATICLIB}")

add_library(orb_delay_lib STATIC IMPORTED GLOBAL)
set_target_properties(orb_delay_lib PROPERTIES
    IMPORTED_LOCATION "${ORB_DELAY_STATICLIB}"
    IMPORTED_LOCATION_DEBUG "${ORB_DELAY_STATICLIB}"
    IMPORTED_LOCATION_RELEASE "${ORB_DELAY_STATICLIB}"
    IMPORTED_LOCATION_RELWITHDEBINFO "${ORB_DELAY_STATICLIB}"
    IMPORTED_LOCATION_MINSIZEREL "${ORB_DELAY_STATICLIB}"
)
add_dependencies(orb_delay_lib orb_delay_rust)

# rustc --print native-static-libs for x86_64-pc-windows-msvc. MSVC does not
# pull these in on its own, so the VST3 link fails with unresolved __imp_* symbols.
if(WIN32)
    set_property(TARGET orb_delay_lib PROPERTY INTERFACE_LINK_LIBRARIES
        ntdll
        userenv
        ws2_32
        bcrypt
        advapi32
        synchronization
    )
endif()

set(ORB_DELAY_OUTPUT_DIR "${CMAKE_SOURCE_DIR}/output")
file(MAKE_DIRECTORY
    "${ORB_DELAY_OUTPUT_DIR}/VST3"
    "${ORB_DELAY_OUTPUT_DIR}/AU"
    "${ORB_DELAY_OUTPUT_DIR}/Standalone"
)

juce_add_plugin(OpenRbDelay
    VERSION 0.1.0
    COMPANY_NAME "OpenRB"
    BUNDLE_ID com.openrb.delay
    PLUGIN_MANUFACTURER_CODE OpnR
    PLUGIN_CODE ODly
    FORMATS VST3 AU Standalone
    PRODUCT_NAME "OpenRB Delay"
    PLUGIN_NAME "OpenRB Delay"
    DESCRIPTION "ReBirth delay"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD TRUE
    VST3_COPY_DIR "${ORB_DELAY_OUTPUT_DIR}/VST3"
    AU_COPY_DIR "${ORB_DELAY_OUTPUT_DIR}/AU"
    VST3_CATEGORIES Fx Delay
    AU_MAIN_TYPE kAudioUnitType_Effect
)

juce_generate_juce_header(OpenRbDelay)

target_sources(OpenRbDelay
    PRIVATE
        plugin/PluginProcessor.cpp
        plugin/PluginProcessor.h
        plugin/PluginEditor.cpp
        plugin/PluginEditor.h
        plugin/RecallImport.cpp
        plugin/RecallImport.h
)

target_include_directories(OpenRbDelay PRIVATE include)

target_compile_definitions(OpenRbDelay
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_REPORT_APP_USAGE=0
)

if(MSVC)
    set(ORB_DELAY_LINK_LIB "$<LINK_LIBRARY:WHOLE_ARCHIVE,orb_delay_lib>")
else()
    set(ORB_DELAY_LINK_LIB orb_delay_lib)
endif()

target_link_libraries(OpenRbDelay
    PRIVATE
        ${ORB_DELAY_LINK_LIB}
        juce::juce_audio_utils
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

juce_add_console_app(OpenRbDelayTests
    PRODUCT_NAME "OpenRbDelayTests"
)

juce_generate_juce_header(OpenRbDelayTests)

target_sources(OpenRbDelayTests
    PRIVATE
        tests/RecallImportTests.cpp
        plugin/PluginProcessor.cpp
        plugin/PluginProcessor.h
        plugin/PluginEditor.cpp
        plugin/PluginEditor.h
        plugin/RecallImport.cpp
        plugin/RecallImport.h
)

target_include_directories(OpenRbDelayTests PRIVATE include plugin)

target_compile_definitions(OpenRbDelayTests
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_REPORT_APP_USAGE=0
    PRIVATE
        JucePlugin_Name="OpenRB Delay"
)

target_link_libraries(OpenRbDelayTests
    PRIVATE
        ${ORB_DELAY_LINK_LIB}
        juce::juce_audio_utils
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)

enable_testing()
add_test(NAME OpenRbDelayRecallImport COMMAND OpenRbDelayTests)

# JUCE's copy step skips Standalone (it is an app, not a plugin format folder).
if(APPLE)
    set(ORB_DELAY_STANDALONE_DEST "${ORB_DELAY_OUTPUT_DIR}/Standalone/OpenRB Delay.app")
    add_custom_command(TARGET OpenRbDelay_Standalone POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E rm -rf "${ORB_DELAY_STANDALONE_DEST}"
        COMMAND "${CMAKE_COMMAND}" -E copy_directory
            "$<TARGET_BUNDLE_DIR:OpenRbDelay_Standalone>"
            "${ORB_DELAY_STANDALONE_DEST}"
        COMMENT "Copying standalone app into output/Standalone"
        VERBATIM
    )
endif()
