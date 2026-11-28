# ZUENA Link — lleva el audio de una pista a OBS (fuente propia), a una salida de Windows y a un WAV.
# Capas: link_core (C++20 puro) <- link_platform (Win32) <- State/UI (JUCE) y la fuente de OBS.
include(${CMAKE_CURRENT_SOURCE_DIR}/LinkLibs.cmake)

juce_add_plugin(ZuenaLink
    COMPANY_NAME "ZUENA"
    BUNDLE_ID "com.zuena.link"
    PLUGIN_MANUFACTURER_CODE Zuen
    PLUGIN_CODE Zln1
    FORMATS VST3
    PRODUCT_NAME "ZUENA Link"
    VERSION 0.1.0
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    VST3_CATEGORIES Fx Tools
    VST3_AUTO_MANIFEST FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE)

if(WIN32)
    # La versión de archivo de la DLL sigue a VERSION (EB-21).
    get_target_property(link_generated ZuenaLink JUCE_GENERATED_SOURCES_DIRECTORY)
    get_target_property(link_info ZuenaLink JUCE_INFO_FILE)
    add_custom_command(OUTPUT "${link_generated}/ZuenaLink_resources.rc" APPEND DEPENDS "${link_info}")
endif()
target_sources(ZuenaLink PRIVATE Source/Processor.cpp Source/State/Parameters.cpp Source/UI/Editor.cpp)
target_link_libraries(ZuenaLink PRIVATE link_platform zeq_resources juce::juce_audio_processors juce::juce_gui_basics
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
target_compile_definitions(ZuenaLink PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
if(MSVC)
    target_compile_options(ZuenaLink PRIVATE /utf-8)
endif()

# Fuente de audio para OBS Studio: zuena-link.dll (se instala en %ProgramData%\obs-studio\plugins\zuena-link\bin\64bit).
# La biblioteca de importación de obs.dll se genera desde obs/obs.def: no hace falta OBS para compilar.
if(MSVC)
    set(ZLINK_OBS_IMPLIB ${CMAKE_CURRENT_BINARY_DIR}/obs.lib)
    add_custom_command(OUTPUT ${ZLINK_OBS_IMPLIB}
        COMMAND ${CMAKE_AR} /nologo /def:${CMAKE_CURRENT_SOURCE_DIR}/obs/obs.def /out:${ZLINK_OBS_IMPLIB} /machine:x64
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/obs/obs.def
        COMMENT "Biblioteca de importación de obs.dll")
    add_custom_target(link_obs_implib DEPENDS ${ZLINK_OBS_IMPLIB})
    add_library(zuena_link_obs SHARED obs/ZuenaLinkObs.cpp)
    add_dependencies(zuena_link_obs link_obs_implib)
    set_target_properties(zuena_link_obs PROPERTIES OUTPUT_NAME zuena-link)
    target_link_libraries(zuena_link_obs PRIVATE link_platform ${ZLINK_OBS_IMPLIB})
    target_compile_options(zuena_link_obs PRIVATE /W4 /WX /permissive- /utf-8)
endif()

if(ZUENA_BUILD_TESTS)
    add_executable(link_core_tests Tests/CoreTests.cpp)
    target_link_libraries(link_core_tests PRIVATE link_core Catch2::Catch2WithMain)
    add_executable(link_platform_tests Tests/PlatformTests.cpp)
    target_link_libraries(link_platform_tests PRIVATE link_platform Catch2::Catch2)
    foreach(t link_core_tests link_platform_tests)
        if(MSVC)
            target_compile_options(${t} PRIVATE /W4 /WX /utf-8)
        endif()
    endforeach()
    add_test(NAME link_core COMMAND link_core_tests)
    add_test(NAME link_platform COMMAND link_platform_tests)

    juce_add_console_app(link_qa PRODUCT_NAME "link_qa")
    target_sources(link_qa PRIVATE Tests/LinkChecks.cpp)
    target_compile_definitions(link_qa PRIVATE JUCE_PLUGINHOST_VST3=1 JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(link_qa PRIVATE ZuenaLink link_platform juce::juce_audio_processors)
    if(MSVC)
        target_compile_options(link_qa PRIVATE /utf-8)
    endif()
    add_test(NAME link_vst3 COMMAND link_qa host "$<TARGET_GENEX_EVAL:ZuenaLink_VST3,$<TARGET_PROPERTY:ZuenaLink_VST3,JUCE_PLUGIN_ARTEFACT_FILE>>")
    add_test(NAME link_editor COMMAND link_qa editor)
    find_program(NODE_EXECUTABLE node)
    if(NODE_EXECUTABLE)
        add_test(NAME link_text COMMAND ${NODE_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/check-ui-text.mjs)
    endif()
endif()
