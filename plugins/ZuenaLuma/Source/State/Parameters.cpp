#include "Parameters.h"

namespace luma
{
const char *paramID(Param p)
{
    constexpr std::array<const char *, 4> switches{"limit", "external", "bypass", "meter"};
    if (p >= Param::limit && p <= Param::meter)
        return switches[index(p) - index(Param::limit)];
    if (p == Param::detectorModel)
        return "detectorModel";
    if (p == Param::autoMakeup)
        return "autoMakeup";
    for (const auto &dial : dials)
        if (dial.parameter == p)
            return dial.id;
    return "";
}

juce::AudioProcessorValueTreeState::ParameterLayout makeLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    const auto addDial = [&layout](const Dial &d)
    {
        auto range = juce::NormalisableRange<float>{d.low, d.high, d.step};
        if (d.parameter == Param::highpass)
            range.setSkewForCentre(80.0f);
        if (d.parameter == Param::attack)
            range.setSkewForCentre(8.0f);
        auto attributes = juce::AudioParameterFloatAttributes().withLabel(utf(d.unit).trim());
        layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{d.id, 1}, utf(d.name), range,
                                                               d.initial, attributes));
    };
    for (const auto &d : dials)
        if (d.parameter < Param::limit)
            addDial(d);
    const auto boolean = [&layout](Param p, const char *name)
    {
        layout.add(
            std::make_unique<juce::AudioParameterBool>(juce::ParameterID{paramID(p), 1}, utf(name), false));
    };
    boolean(Param::limit, "Modo Limit");
    boolean(Param::external, "Sidechain externo");
    boolean(Param::bypass, "Bypass");
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{"meter", 1}, utf("Vúmetro"), juce::StringArray{utf("Reducción"), "Entrada", "Salida"},
        0));
    for (const auto &d : dials)
        if (d.parameter > Param::meter)
            addDial(d);
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{paramID(Param::detectorModel), 1}, "Modelo detector",
        juce::StringArray{utf("Clásico"), utf("Precisión")}, static_cast<int>(DetectorModel::precision)));
    boolean(Param::autoMakeup, "Ganancia automática");
    return layout;
}
} // namespace luma
