#include "Parameters.h"
#include "../Core/LinkProtocol.h"

namespace zlink
{
juce::AudioProcessorValueTreeState::ParameterLayout makeLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    const auto id = [](Param p) { return juce::ParameterID{kParamIds[index(p)], 1}; };
    layout.add(std::make_unique<juce::AudioParameterBool>(id(Param::obs), utf("Enviar a OBS"), true));
    juce::StringArray channels;
    for (std::uint32_t i = 1; i <= kLinkChannels; ++i)
        channels.add("Link " + juce::String(i));
    layout.add(std::make_unique<juce::AudioParameterChoice>(id(Param::channel), utf("Canal"), channels, 0));
    layout.add(std::make_unique<juce::AudioParameterBool>(id(Param::device), utf("Enviar al dispositivo"), false));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        id(Param::send), utf("Nivel de envío"), juce::NormalisableRange<float>{kSendMinDb, kSendMaxDb, 0.1f}, 0.0f,
        juce::AudioParameterFloatAttributes().withLabel("dB")));
    layout.add(std::make_unique<juce::AudioParameterBool>(id(Param::mono), utf("Envío mono"), false));
    return layout;
}
} // namespace zlink
