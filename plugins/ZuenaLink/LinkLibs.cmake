# Bibliotecas de ZUENA Link sin JUCE: las usan el VST3, la fuente de OBS y las pruebas.
#   link_core     → C++20 puro (anillo, puente de relojes, WAV, rutas, protocolo). Sin Windows, sin GUI.
#   link_platform → Win32 (memoria compartida, WASAPI, dispositivos, grabación, motor y receptor).
set(ZLINK_DIR ${CMAKE_CURRENT_LIST_DIR})

add_library(link_core STATIC
    ${ZLINK_DIR}/Source/Core/ClockBridge.cpp
    ${ZLINK_DIR}/Source/Core/WavFormat.cpp
    ${ZLINK_DIR}/Source/Core/DeviceRoutes.cpp)
target_include_directories(link_core PUBLIC ${ZLINK_DIR}/Source/Core)
target_compile_features(link_core PUBLIC cxx_std_20)

add_library(link_platform STATIC
    ${ZLINK_DIR}/Source/Platform/SharedChannel.cpp
    ${ZLINK_DIR}/Source/Platform/DeviceCatalog.cpp
    ${ZLINK_DIR}/Source/Platform/WasapiOutput.cpp
    ${ZLINK_DIR}/Source/Platform/Recorder.cpp
    ${ZLINK_DIR}/Source/Platform/LinkEngine.cpp
    ${ZLINK_DIR}/Source/Platform/LinkReceiver.cpp)
target_include_directories(link_platform PUBLIC ${ZLINK_DIR}/Source/Platform)
target_link_libraries(link_platform PUBLIC link_core ole32 avrt advapi32 shell32)

foreach(lib link_core link_platform)
    if(MSVC)
        target_compile_options(${lib} PRIVATE /W4 /WX /permissive- /utf-8)
    else()
        target_compile_options(${lib} PRIVATE -Wall -Wextra -Werror)
    endif()
endforeach()
