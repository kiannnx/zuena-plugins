add_library(luma_core STATIC Source/Core/OpticalEngine.cpp)
target_include_directories(luma_core PUBLIC Source/Core)
target_compile_features(luma_core PUBLIC cxx_std_20)
if(MSVC)
    target_compile_options(luma_core PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(luma_core PRIVATE -Wall -Wextra -Werror)
endif()

juce_add_plugin(ZuenaLuma
    COMPANY_NAME "ZUENA"
    BUNDLE_ID "com.zuena.luma"
    PLUGIN_MANUFACTURER_CODE Zuen
    PLUGIN_CODE Zlm1
    FORMATS VST3
    PRODUCT_NAME "ZUENA Luma"
    VERSION 0.4.0
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    VST3_CATEGORIES Fx Dynamics
    VST3_AUTO_MANIFEST FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE)

if(WIN32)
    get_target_property(luma_generated ZuenaLuma JUCE_GENERATED_SOURCES_DIRECTORY)
    get_target_property(luma_info ZuenaLuma JUCE_INFO_FILE)
    add_custom_command(OUTPUT "${luma_generated}/ZuenaLuma_resources.rc" APPEND DEPENDS "${luma_info}")
endif()
target_sources(ZuenaLuma PRIVATE Source/Processor.cpp Source/State/Parameters.cpp Source/UI/Editor.cpp)
target_link_libraries(ZuenaLuma PRIVATE luma_core zeq_resources juce::juce_audio_processors
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
target_compile_definitions(ZuenaLuma PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
if(MSVC)
    target_compile_options(ZuenaLuma PRIVATE /utf-8)
endif()

if(ZUENA_BUILD_TESTS)
    add_executable(luma_tests Tests/EngineTests.cpp Tests/Realtime.cpp)
    target_link_libraries(luma_tests PRIVATE luma_core Catch2::Catch2WithMain)
    add_test(NAME luma_dsp COMMAND luma_tests)
    add_executable(luma_measure tools/Measure.cpp)
    target_link_libraries(luma_measure PRIVATE luma_core)
    juce_add_console_app(luma_qa PRODUCT_NAME "luma_qa")
    target_sources(luma_qa PRIVATE Tests/PluginChecks.cpp)
    target_compile_definitions(luma_qa PRIVATE JUCE_PLUGINHOST_VST3=1 JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(luma_qa PRIVATE ZuenaLuma luma_core juce::juce_audio_processors)
    if(MSVC)
        target_compile_options(luma_qa PRIVATE /utf-8)
    endif()
    add_test(NAME luma_vst3 COMMAND luma_qa host "$<TARGET_GENEX_EVAL:ZuenaLuma_VST3,$<TARGET_PROPERTY:ZuenaLuma_VST3,JUCE_PLUGIN_ARTEFACT_FILE>>")
    add_test(NAME luma_editor COMMAND luma_qa editor)
    find_program(NODE_EXECUTABLE node)
    if(NODE_EXECUTABLE)
        add_test(NAME luma_text COMMAND ${NODE_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/check-ui-text.mjs)
    endif()
endif()
