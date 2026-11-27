#pragma once
#include "../Processor.h"

namespace luma
{
class PanelStyle final : public juce::LookAndFeel_V4
{
  public:
    bool dark = false;
    juce::Colour paper() const;
    juce::Colour ink() const;
    juce::Colour muted() const;
    juce::Colour rim() const;
    void apply(bool);
    void drawRotarySlider(juce::Graphics &, int, int, int, int, float, float, float, juce::Slider &) override;
    void drawButtonBackground(juce::Graphics &, juce::Button &, const juce::Colour &, bool, bool) override;
    juce::Font getTextButtonFont(juce::TextButton &, int) override;
    juce::Font getComboBoxFont(juce::ComboBox &) override;
};

class Editor final : public juce::AudioProcessorEditor, private juce::Timer
{
  public:
    explicit Editor(Processor &);
    ~Editor() override;
    void paint(juce::Graphics &) override;
    void resized() override;

  private:
    Processor &processor;
    PanelStyle style;
    std::array<juce::Slider, dials.size()> knobs;
    std::array<bool, dials.size()> dragging{};
    juce::TextButton limit, key, bypass, theme, listen, autoGain;
    juce::ComboBox factory, meter, model;
    juce::TooltipWindow tooltips{this, 550};
    juce::Rectangle<int> meterArea, meterFrame, meterTop, historyArea, header, dynamics, detector, footer;
    juce::Rectangle<int> reductionPanel, makeupPanel, curveArea;
    Controls shown{}; // controles con los que se dibujó la curva (se redibuja solo si cambian)
    std::array<juce::Rectangle<int>, dials.size()> labels;
    static constexpr std::size_t historyLength = 180;
    std::array<std::array<float, 2>, historyLength> history{};
    std::size_t historyHead = 0, activeHistory = 0;
    std::uint32_t lastFrame = 0;
    int inactiveTicks = 0;
    MeterValues values;
    juce::String status;
    double thresholdDb = -21.2;
    int display = 0;
    void timerCallback() override;
    void drawTelemetry(juce::Graphics &) const;
    void drawMeter(juce::Graphics &) const;
    void drawHistory(juce::Graphics &) const;
    void drawCurve(juce::Graphics &) const;
    double makeupShownDb() const noexcept;
    void refreshTheme(bool);
};
} // namespace luma
