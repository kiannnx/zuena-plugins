# ZUENA EQ — ecualizador dinámico VST3.
# Capas: zeq_core (DSP, C++20 puro, sin JUCE) <- State (APVTS) <- UI (editor JUCE).

add_library(zeq_core STATIC
    Source/Core/FilterDesign.cpp
    Source/Core/EqEngine.cpp
    Source/Core/Spectrum.cpp)
target_include_directories(zeq_core PUBLIC Source/Core)
target_compile_features(zeq_core PUBLIC cxx_std_20)
if(MSVC)
    target_compile_options(zeq_core PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(zeq_core PRIVATE -Wall -Wextra -Werror)
endif()

juce_add_binary_data(zeq_resources SOURCES
    Resources/Inter-Regular.ttf
    Resources/Inter-SemiBold.ttf)

juce_add_plugin(ZuenaEQ
    COMPANY_NAME "ZUENA"
    BUNDLE_ID "com.zuena.eq"
    PLUGIN_MANUFACTURER_CODE Zuen
    PLUGIN_CODE Zeq1
    FORMATS VST3 Standalone
    PRODUCT_NAME "ZUENA EQ"
    VERSION 0.3.5
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    VST3_CATEGORIES Fx EQ
    VST3_AUTO_MANIFEST FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE)

# JUCE genera ZuenaEQ_resources.rc (versión de archivo de la DLL) sin depender de Info.txt: al subir
# VERSION la DLL seguía diciendo 0.1.0 (EB-21). Se añade la dependencia que falta.
get_target_property(zeq_generated_dir ZuenaEQ JUCE_GENERATED_SOURCES_DIRECTORY)
get_target_property(zeq_info_file ZuenaEQ JUCE_INFO_FILE)
add_custom_command(OUTPUT "${zeq_generated_dir}/ZuenaEQ_resources.rc" APPEND DEPENDS "${zeq_info_file}")

target_sources(ZuenaEQ PRIVATE
    Source/PluginProcessor.cpp
    Source/State/Parameters.cpp
    Source/UI/ZuenaLookAndFeel.cpp
    Source/UI/EqGraph.cpp
    Source/UI/BandCard.cpp
    Source/UI/PluginEditor.cpp)

target_compile_definitions(ZuenaEQ PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(ZuenaEQ
    PRIVATE
        zeq_core
        zeq_resources
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

if(ZUENA_BUILD_TESTS)
    # Herramientas de verificación (no se distribuyen):
    #  - zeq_snapshot: captura del editor real, enlazado al código compartido del plugin.
    #  - zeq_host: host VST3 independiente (validación del .vst3 compilado y medición de caja negra).
    juce_add_console_app(zeq_snapshot PRODUCT_NAME "zeq_snapshot")
    target_sources(zeq_snapshot PRIVATE
        tools/Snapshot.cpp
        Source/PluginProcessor.cpp
        Source/State/Parameters.cpp
        Source/UI/ZuenaLookAndFeel.cpp
        Source/UI/EqGraph.cpp
        Source/UI/BandCard.cpp
        Source/UI/PluginEditor.cpp)
    target_compile_definitions(zeq_snapshot PRIVATE
        JucePlugin_Name="ZUENA EQ" JUCE_MODAL_LOOPS_PERMITTED=1 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(zeq_snapshot PRIVATE
        zeq_core
        zeq_resources
        juce::juce_audio_utils
        juce::juce_recommended_config_flags)

    juce_add_console_app(zeq_host PRODUCT_NAME "zeq_host")
    target_sources(zeq_host PRIVATE tools/Host.cpp)
    target_compile_definitions(zeq_host PRIVATE JUCE_PLUGINHOST_VST3=1 JUCE_MODAL_LOOPS_PERMITTED=1 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(zeq_host PRIVATE
        juce::juce_audio_utils
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

    add_executable(run_tests
        Tests/FilterTests.cpp
        Tests/EngineTests.cpp)
    target_link_libraries(run_tests PRIVATE zeq_core Catch2::Catch2WithMain)
    if(MSVC)
        target_compile_options(run_tests PRIVATE /W4 /WX /permissive-)
    endif()
    add_test(NAME run_tests COMMAND run_tests)
    # EB-01/02/03: recorre el editor real (necesita sesión de escritorio).
    add_test(NAME ui_audit COMMAND zeq_snapshot audit)

    find_program(NODE_EXECUTABLE node)
    if(NODE_EXECUTABLE)
        # EB-01..04, EB-12..14 sin binarios nativos sin firmar.
        add_test(NAME verify_filters COMMAND ${NODE_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/verify-filters.mjs)
        add_test(NAME verify_engine COMMAND ${NODE_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/verify-engine.mjs)
        add_test(NAME verify_ui_contract COMMAND ${NODE_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/verify-ui-contract.mjs)
        add_test(NAME check_text COMMAND ${NODE_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/check-text.mjs)
    endif()
endif()
