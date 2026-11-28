#pragma once
#include "../Processor.h"
#include <memory>

namespace zlink
{
class LinkStyle final : public juce::LookAndFeel_V4
{
  public:
    bool dark = false;
    juce::Colour paper() const;
    juce::Colour surface() const;
    juce::Colour ink() const;
    juce::Colour muted() const;
    juce::Colour rim() const;
    void apply(bool);
    void drawButtonBackground(juce::Graphics &, juce::Button &, const juce::Colour &, bool, bool) override;
    juce::Font getTextButtonFont(juce::TextButton &, int) override;
    juce::Font getComboBoxFont(juce::ComboBox &) override;
    juce::Font getPopupMenuFont() override;
    void drawLinearSlider(juce::Graphics &, int, int, int, int, float, float, float, juce::Slider::SliderStyle,
                          juce::Slider &) override;
    juce::Label *createSliderTextBox(juce::Slider &) override;
};

/** Interruptor tipo píldora: cada destino se prende y se apaga con uno. */
class Switch final : public juce::Button
{
  public:
    explicit Switch(const juce::String &name);
    void paintButton(juce::Graphics &, bool hover, bool down) override;
};

/** Botón de grabar: círculo rojo; cuadrado mientras graba. */
class RecordButton final : public juce::Button
{
  public:
    RecordButton();
    void paintButton(juce::Graphics &, bool hover, bool down) override;
};

class Editor final : public juce::AudioProcessorEditor, private juce::Timer
{
  public:
    explicit Editor(Processor &);
    ~Editor() override;
    void paint(juce::Graphics &) override;
    void resized() override;

    // Para la auditoría automática de la interfaz (link_qa).
    struct Texts
    {
        juce::String obs, device, deviceHint, record, recordHint;
    };
    Texts texts() const { return {obsStatus, deviceStatus, deviceHint, recordStatus, recordHint}; }
    void refreshNow() { timerCallback(); }

    Switch obsSwitch{"Enviar a OBS"}, deviceSwitch{"Enviar al dispositivo"};
    RecordButton record;
    juce::ComboBox channel, deviceList, mode, pair, format;
    juce::TextButton folder{"Carpeta"}, reveal{"Abrir"}, theme, mono{"Mono"};
    juce::Slider send;

  private:
    Processor &processor;
    LinkStyle style;
    juce::TooltipWindow tooltips{this, 600};
    std::unique_ptr<juce::FileChooser> chooser;
    juce::Rectangle<int> header, sendStrip, meterArea, footer;
    std::array<juce::Rectangle<int>, 3> rows, textAreas;
    LinkSnapshot snap;
    std::uint32_t endpointsShown = 0xFFFFFFFFu;
    std::vector<Endpoint> endpoints; // la lista que muestra el menú (en el mismo orden)
    juce::String obsStatus, deviceStatus, deviceHint, recordStatus, recordHint;
    juce::Colour obsDot, deviceDot, recordDot;
    float meterL = 0, meterR = 0;
    bool dragging = false;
    std::array<bool, kLinkChannels> inUseShown{};

    void timerCallback() override;
    void refreshTheme(bool);
    void rebuildDevices();
    void applyDevice();
    void updatePairs();
    void chooseFolder();
    void drawRow(juce::Graphics &, int row, const char *title, const juce::String &status, juce::Colour dot,
                 const juce::String &hint) const;
    const Endpoint *selectedEndpoint() const;
};
} // namespace zlink
