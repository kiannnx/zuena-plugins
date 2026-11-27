#include "Editor.h"
#include <BinaryData.h>
#include <cmath>
#include <numbers>

namespace luma
{
namespace
{
juce::Font font(float size, bool bold = false)
{
    static auto regular =
        juce::Typeface::createSystemTypefaceFor(BinaryData::InterRegular_ttf, BinaryData::InterRegular_ttfSize);
    static auto strong = juce::Typeface::createSystemTypefaceFor(BinaryData::InterSemiBold_ttf,
                                                                 BinaryData::InterSemiBold_ttfSize);
    return juce::Font(juce::FontOptions(bold ? strong : regular).withHeight(size));
}
juce::Point<float> polar(juce::Point<float> origin, float radius, float angle)
{
    return {origin.x + radius * std::sin(angle), origin.y - radius * std::cos(angle)};
}
void line(juce::Graphics &g, juce::Point<float> a, juce::Point<float> b, float width)
{
    g.drawLine({a, b}, width);
}
void stamp(juce::Graphics &g, juce::Rectangle<float> box, bool dark)
{
    g.setColour(dark ? juce::Colours::white : juce::Colours::black);
    g.fillEllipse(box);
    const auto s = box.getWidth() / 64.0f;
    juce::Path z;
    z.startNewSubPath(19, 20);
    z.lineTo(45, 20);
    z.lineTo(19, 44);
    z.lineTo(45, 44);
    g.setColour(dark ? juce::Colours::black : juce::Colours::white);
    g.strokePath(z, juce::PathStrokeType(6.5f * s, juce::PathStrokeType::curved, juce::PathStrokeType::rounded),
                 juce::AffineTransform::scale(s).translated(box.getX(), box.getY()));
}
constexpr std::array<const char *, dials.size()> hints{
    {"Aumentá la reducción para nivelar más la señal.", "Ganancia de la rama comprimida.",
     "Frecuencia del paso alto del detector. En 0 Hz queda apagado.",
     "Sensibilidad adicional a los agudos del detector, a partir de 3,5 kHz.",
     "Escala el tiempo de recuperación de las dos etapas ópticas.",
     "Porcentaje de señal comprimida en la mezcla.",
     "Tiempo hasta el 63,2 % de la reducción de la rama rápida ante una demanda constante del detector.",
     "Al 100 % ambos canales comparten la reducción. Al 0 % cada canal responde por su cuenta.",
     "Máxima reducción que puede aplicar el compresor."}};
const juce::Colour screen{0xff151a19};
const juce::Colour screenInk{0xffefeee8};
const juce::Colour screenMuted{0xffb9c2bb};
const juce::Colour amber{0xffffce92};
const juce::Colour cyan{0xffa5e9e7};

juce::String peakText(float peak)
{
    return (peak <= -108 ? utf("−∞") : juce::String(peak, 1)) + " dBFS";
}
} // namespace
juce::Colour PanelStyle::paper() const
{
    return juce::Colour(dark ? 0xff141617 : 0xfff5f5f3);
}
juce::Colour PanelStyle::ink() const
{
    return juce::Colour(dark ? 0xfff2f1ed : 0xff181a1c);
}
juce::Colour PanelStyle::muted() const
{
    return juce::Colour(dark ? 0xffb1b4b5 : 0xff555b5e);
}
juce::Colour PanelStyle::rim() const
{
    return ink().withAlpha(0.18f);
}
void PanelStyle::apply(bool night)
{
    dark = night;
    const auto surface = dark ? juce::Colour(0xff27292b) : juce::Colour(0xfffdfdfb);
    setColour(juce::TextButton::buttonColourId, surface);
    setColour(juce::TextButton::buttonOnColourId, ink());
    setColour(juce::TextButton::textColourOffId, ink());
    setColour(juce::TextButton::textColourOnId, paper());
    setColour(juce::ComboBox::backgroundColourId, surface);
    setColour(juce::ComboBox::outlineColourId, rim());
    setColour(juce::ComboBox::arrowColourId, muted());
    setColour(juce::ComboBox::textColourId, ink());
    setColour(juce::Slider::textBoxTextColourId, ink());
    setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour(juce::PopupMenu::backgroundColourId, paper());
    setColour(juce::PopupMenu::textColourId, ink());
}
void PanelStyle::drawButtonBackground(juce::Graphics &g, juce::Button &button, const juce::Colour &fill,
                                      bool hover, bool down)
{
    const auto area = button.getLocalBounds().toFloat().reduced(0.75f);
    const auto face = button.getToggleState() ? ink() : fill;
    g.setColour(face.interpolatedWith(button.getToggleState() ? paper() : ink(), down ? 0.17f : (hover ? 0.09f : 0.0f)));
    g.fillRoundedRectangle(area, 7.0f);
    g.setColour(button.hasKeyboardFocus(false) ? muted() : rim());
    g.drawRoundedRectangle(area, 7.0f, 1.0f);
}
juce::Font PanelStyle::getTextButtonFont(juce::TextButton &, int h)
{
    return font(std::min(15.0f, h * 0.49f), true);
}
juce::Font PanelStyle::getComboBoxFont(juce::ComboBox &b)
{
    return font(std::min(15.0f, b.getHeight() * 0.52f));
}
void PanelStyle::drawRotarySlider(juce::Graphics &g, int x, int y, int w, int h, float pos, float start,
                                  float end, juce::Slider &slider)
{
    const juce::Point<float> c{static_cast<float>(x) + w * 0.5f, static_cast<float>(y) + h * 0.5f};
    const float r = std::min(std::min(w, h) * 0.43f, 65.0f);
    const float a = start + pos * (end - start);
    juce::Path track, active;
    track.addCentredArc(c.x, c.y, r, r, 0, start, end, true);
    const float zero = slider.getMinimum() < 0 ? static_cast<float>(slider.valueToProportionOfLength(0)) : 0;
    const float origin = start + zero * (end - start);
    active.addCentredArc(c.x, c.y, r, r, 0, std::min(origin, a), std::max(origin, a), true);
    g.setColour(rim());
    g.strokePath(track, juce::PathStrokeType(2.4f));
    g.setColour(ink());
    if (std::abs(origin - a) > 0.001f)
        g.strokePath(active, juce::PathStrokeType(r > 42 ? 3.0f : 2.5f));
    const auto bounds = juce::Rectangle<float>(r * 1.62f, r * 1.62f).withCentre(c);
    juce::ColourGradient face(dark ? juce::Colour(0xff414243) : juce::Colour(0xffffffff), bounds.getTopLeft(),
                              dark ? juce::Colour(0xff202224) : juce::Colour(0xffdcdfdd),
                              bounds.getBottomRight(), false);
    g.setGradientFill(face);
    g.fillEllipse(bounds);
    g.setColour(rim());
    g.drawEllipse(bounds, 1.0f);
    g.setColour(ink());
    line(g, polar(c, r * 0.41f, a), polar(c, r * 0.70f, a), r > 42 ? 3.0f : 2.0f);
}

Editor::Editor(Processor &p) : AudioProcessorEditor(p), processor(p)
{
    setLookAndFeel(&style);
    for (std::size_t i = 0; i < knobs.size(); ++i)
    {
        auto &k = knobs[i];
        const auto &d = dials[i];
        const auto id = d.parameter;
        k.setName(utf(d.name));
        k.setTitle(utf(d.name));
        k.setTooltip(utf(hints[i]));
        k.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        k.setRotaryParameters(3.82f, 8.75f, true);
        k.setTextBoxStyle(juce::Slider::TextBoxBelow, false, i < 2 ? 98 : 76, i < 2 ? 24 : 21);
        k.setRange(d.low, d.high, d.step);
        k.setDoubleClickReturnValue(true, d.initial);
        if (id == Param::highpass)
            k.setSkewFactorFromMidPoint(80);
        if (id == Param::attack)
            k.setSkewFactorFromMidPoint(8);
        k.setNumDecimalPlacesToDisplay(d.step < 1 ? 1 : 0);
        k.setTextValueSuffix(utf(d.unit));
        k.setValue(p.value(id), juce::dontSendNotification);
        k.onDragStart = [this, i, id]
        {
            dragging[i] = true;
            processor.parameter(id).beginChangeGesture();
        };
        k.onDragEnd = [this, i, id]
        {
            processor.parameter(id).endChangeGesture();
            dragging[i] = false;
        };
        k.onValueChange = [this, i, id]
        {
            const auto v = static_cast<float>(knobs[i].getValue());
            auto &a = processor.parameter(id);
            if (dragging[i])
                a.setValueNotifyingHost(a.convertTo0to1(v));
            else
                processor.gesture(id, v);
        };
        addAndMakeVisible(k);
    }
    const auto toggle = [this](juce::TextButton &b, Param param, const char *help)
    {
        b.setClickingTogglesState(true);
        b.setTooltip(utf(help));
        b.onClick = [this, &b, param]
        {
            processor.gesture(param, b.getToggleState() ? 1.0f : 0.0f);
            timerCallback();
        };
        addAndMakeVisible(b);
    };
    toggle(limit, Param::limit, "Alterná entre Compress (3:1) y Limit (12:1).");
    toggle(key, Param::external,
           "El sidechain externo usa el bus auxiliar del DAW. Sin bus, escucha la entrada.");
    key.setTitle("Fuente del detector");
    toggle(bypass, Param::bypass, "Escuchá la entrada original.");
    bypass.setButtonText("Bypass");
    toggle(autoGain, Param::autoMakeup,
           "Repone la ganancia que quita la compresión a una señal de −18 dBFS RMS (0 VU). Se suma a la perilla Ganancia.");
    autoGain.setTitle(utf("Ganancia automática"));
    for (std::size_t i = 0; i < presets.size(); ++i)
        factory.addItem(utf(presets[i].name), static_cast<int>(i) + 1);
    factory.setTextWhenNothingSelected("Manual");
    factory.setTitle("Preset de mezcla");
    factory.onChange = [this]
    {
        const int n = factory.getSelectedId();
        if (n > 0)
            processor.applyFactory(static_cast<std::size_t>(n - 1));
    };
    meter.addItem(utf("Reducción"), 1);
    meter.addItem("Entrada", 2);
    meter.addItem("Salida", 3);
    meter.setTitle(utf("Lectura del vúmetro"));
    meter.setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff262e2b));
    meter.setColour(juce::ComboBox::outlineColourId, juce::Colour(0xff48544f));
    meter.setColour(juce::ComboBox::textColourId, screenInk);
    meter.setColour(juce::ComboBox::arrowColourId, screenMuted);
    meter.onChange = [this]
    {
        processor.gesture(Param::meter, static_cast<float>(meter.getSelectedId() - 1));
        timerCallback();
    };
    model.addItem(utf("Clásico"), 1);
    model.addItem(utf("Precisión"), 2);
    model.setTitle("Modelo del detector");
    model.setTooltip(utf("Clásico conserva la respuesta anterior. Precisión usa una rodilla más suave y mide la energía por canal."));
    model.onChange = [this]
    {
        if (model.getSelectedId() > 0)
            processor.gesture(Param::detectorModel, static_cast<float>(model.getSelectedId() - 1));
    };
    addAndMakeVisible(model);
    theme.onClick = [this]
    {
        processor.setDark(!style.dark);
        timerCallback();
    };
    listen.setButtonText("Escuchar");
    listen.setTitle("Escuchar el detector");
    listen.setTooltip(utf("Escuchá la señal filtrada que recibe el detector. El bypass tiene prioridad."));
    listen.setClickingTogglesState(true);
    listen.onClick = [this] { processor.setAudition(listen.getToggleState()); timerCallback(); };
    addAndMakeVisible(factory);
    addAndMakeVisible(meter);
    addAndMakeVisible(theme);
    addAndMakeVisible(listen);
    refreshTheme(p.isDark());
    setResizable(true, true);
    setResizeLimits(820, 550, 1380, 840);
    setSize(1020, 642);
    timerCallback();
    startTimerHz(30);
}
Editor::~Editor()
{
    stopTimer();
    processor.setAudition(false);
    setLookAndFeel(nullptr);
}
void Editor::refreshTheme(bool night)
{
    style.apply(night);
    theme.setButtonText(night ? "Tema oscuro" : "Tema claro");
    sendLookAndFeelChange();
    repaint();
}
void Editor::timerCallback()
{
    const bool night = processor.isDark();
    if (night != style.dark)
        refreshTheme(night);
    auto *textEntry = dynamic_cast<juce::TextEditor *>(juce::Component::getCurrentlyFocusedComponent());
    for (std::size_t i = 0; i < knobs.size(); ++i)
        if (!dragging[i] && !knobs[i].isMouseButtonDown() && !(textEntry && knobs[i].isParentOf(textEntry)))
            knobs[i].setValue(processor.value(dials[i].parameter), juce::dontSendNotification);
    auto sync = [this](juce::TextButton &b, Param id)
    { b.setToggleState(processor.value(id) >= 0.5f, juce::dontSendNotification); };
    sync(limit, Param::limit);
    sync(key, Param::external);
    sync(bypass, Param::bypass);
    sync(autoGain, Param::autoMakeup);
    listen.setToggleState(processor.isAuditioning(), juce::dontSendNotification);
    limit.setButtonText(limit.getToggleState() ? "Limit  12:1" : "Compress  3:1");
    const auto live = processor.liveControls();
    const auto autoText = autoGain.getToggleState()
        ? utf("Auto  +") + juce::String(OpticalEngine::referenceReductionDb(live), 1) + " dB"
        : utf("Ganancia auto");
    if (autoGain.getButtonText() != autoText)
        autoGain.setButtonText(autoText);
    if (!(live == shown))
    {
        // La curva y el punto de trabajo se dibujan con los mismos controles que usa el motor.
        shown = live;
        repaint(meterFrame);
    }
    key.setButtonText(key.getToggleState() ? "Externo" : "Interno");
    int presetId = 0;
    for (std::size_t n = 0; n < presets.size(); ++n)
    {
        bool match = presets[n].limit == limit.getToggleState();
        for (std::size_t i = 0; i < dials.size(); ++i)
            match = match && std::abs(processor.value(dials[i].parameter) - presets[n].dial[i]) < 0.045f;
        if (match)
        {
            presetId = static_cast<int>(n) + 1;
            break;
        }
    }
    factory.setSelectedId(presetId, juce::dontSendNotification);
    const int previousDisplay = display;
    display = juce::jlimit(0, 2, juce::roundToInt(processor.value(Param::meter)));
    meter.setSelectedId(display + 1, juce::dontSendNotification);
    model.setSelectedId(1 + juce::jlimit(0, 1, juce::roundToInt(processor.value(Param::detectorModel))),
                        juce::dontSendNotification);
    const auto frame = processor.meterFrameCount();
    inactiveTicks = frame == lastFrame ? std::min(inactiveTicks + 1, 15) : 0;
    lastFrame = frame;
    const auto next = inactiveTicks == 15 ? MeterValues{} : processor.readMeters();
    const auto active = [](auto sample) { return std::max(sample[0], sample[1]) >= 0.05f; };
    const bool wasActive = activeHistory > 0;
    if (active(history[historyHead]))
        --activeHistory;
    history[historyHead] = {next.reductionLeftDb, next.reductionRightDb};
    if (active(history[historyHead]))
        ++activeHistory;
    historyHead = (historyHead + 1) % history.size();

    if (previousDisplay != display || wasActive || activeHistory > 0
        || next.inputVu != values.inputVu || next.outputVu != values.outputVu
        || next.inputPeakDb != values.inputPeakDb || next.peakDb != values.peakDb)
        repaint(meterFrame);
    values = next;

    juce::String nextStatus;
    if (bypass.getToggleState())
        nextStatus = utf("Bypass · señal directa");
    else
    {
        const auto source = key.getToggleState()
            ? (processor.keyConnected() ? "Detector externo" : "Sin bus auxiliar · detector interno")
            : "Detector interno";
        nextStatus = listen.getToggleState() ? utf("Escucha activa · ") + utf(source) : utf(source);
    }
    const double nextThreshold = -2.0 - 0.4 * knobs[0].getValue();
    if (status != nextStatus || thresholdDb != nextThreshold)
    {
        status = nextStatus;
        thresholdDb = nextThreshold;
        repaint(footer);
    }
}
void Editor::resized()
{
    auto b = getLocalBounds().reduced(22, 12);
    header = b.removeFromTop(62);
    auto actions = header.reduced(0, 11);
    bypass.setBounds(actions.removeFromRight(76));
    actions.removeFromRight(8);
    theme.setBounds(actions.removeFromRight(98));
    actions.removeFromRight(8);
    factory.setBounds(actions.removeFromRight(130));
    b.removeFromTop(14);
    footer = b.removeFromBottom(22);
    b.removeFromBottom(8);
    auto bottom = b.removeFromBottom(juce::jlimit(144, 186, getHeight() * 27 / 100));
    dynamics = bottom.removeFromLeft((bottom.getWidth() - 12) * 53 / 100);
    bottom.removeFromLeft(12);
    detector = bottom;
    b.removeFromBottom(12);
    const int side = b.getWidth() * 21 / 100;
    reductionPanel = b.removeFromLeft(side);
    makeupPanel = b.removeFromRight(side);
    b.reduce(12, 0);
    meterFrame = b;
    auto surface = meterFrame.reduced(13, 10);
    meterTop = surface.removeFromTop(30);
    meter.setBounds(juce::Rectangle<int>(102, 26).withCentre(meterTop.getCentre()));
    historyArea = surface.removeFromBottom(82);
    meterArea = surface;
    // Curva de compresión a la izquierda del vúmetro: cuadrada, del alto disponible.
    const int curveSide = std::min(meterArea.getHeight(), meterArea.getWidth() * 36 / 100);
    curveArea = meterArea.removeFromLeft(curveSide).reduced(4, 2);
    meterArea.removeFromLeft(8);
    auto primary = [this](juce::Rectangle<int> a, std::size_t i)
    {
        a = a.reduced(10, 12);
        labels[i] = a.removeFromTop(34);
        a.removeFromBottom(38);
        knobs[i].setBounds(a);
    };
    primary(reductionPanel, 0);
    primary(makeupPanel, 1);
    limit.setBounds(reductionPanel.reduced(14, 10).removeFromBottom(27));
    autoGain.setBounds(makeupPanel.reduced(14, 10).removeFromBottom(27));
    auto assign = [this](juce::Rectangle<int> area, const auto &indices)
    {
        const int width = area.getWidth() / static_cast<int>(indices.size());
        for (const auto i : indices)
        {
            auto cell = area.removeFromLeft(width).reduced(1, 0);
            labels[i] = cell.removeFromTop(19);
            knobs[i].setBounds(cell);
        }
    };
    auto dynamicsInner = dynamics.reduced(9, 8);
    dynamicsInner.removeFromTop(34);
    assign(dynamicsInner, std::array<std::size_t, 4>{6, 4, 8, 5});
    auto detectorInner = detector.reduced(9, 8);
    auto detectorActions = detectorInner.removeFromTop(30).reduced(2, 2);
    listen.setBounds(detectorActions.removeFromRight(76));
    detectorActions.removeFromRight(5);
    key.setBounds(detectorActions.removeFromRight(72));
    detectorActions.removeFromLeft(66);
    model.setBounds(detectorActions.removeFromLeft(100));
    detectorInner.removeFromTop(4);
    assign(detectorInner, std::array<std::size_t, 3>{2, 3, 7});
}
void Editor::paint(juce::Graphics &g)
{
    g.fillAll(style.paper());
    const auto panel = style.dark ? juce::Colour(0xff1c201f) : juce::Colour(0xfffbfbf8);
    const auto paintPanel = [&](juce::Rectangle<int> bounds)
    {
        g.setColour(panel);
        g.fillRoundedRectangle(bounds.toFloat(), 13);
        g.setColour(style.rim());
        g.drawRoundedRectangle(bounds.toFloat().reduced(0.5f), 13, 1);
    };
    for (auto bounds : {reductionPanel, makeupPanel, dynamics, detector})
        paintPanel(bounds);
    stamp(g, {static_cast<float>(header.getX()), static_cast<float>(header.getCentreY() - 14), 28, 28},
          style.dark);
    g.setColour(style.ink());
    g.setFont(font(15, true).withExtraKerningFactor(0.17f));
    g.drawText("ZUENA", header.getX() + 39, header.getY(), 77, header.getHeight(),
               juce::Justification::centredLeft);
    g.setFont(font(36).withExtraKerningFactor(-0.05f));
    g.drawText("Luma", header.getX() + 124, header.getY(), 115, header.getHeight(),
               juce::Justification::centredLeft);
    g.setFont(font(12, true).withExtraKerningFactor(0.10f));
    g.setColour(style.muted());
    g.drawText(utf("COMPRESOR ÓPTICO"), header.getX() + 246, header.getY(), 184, header.getHeight(),
               juce::Justification::centredLeft);
    g.setColour(style.rim());
    g.drawHorizontalLine(header.getBottom(), static_cast<float>(header.getX()),
                         static_cast<float>(header.getRight()));
    g.setColour(style.ink());
    g.setFont(font(15, true));
    g.drawText(utf("Dinámica"), dynamics.reduced(14, 8).removeFromTop(30), juce::Justification::centredLeft);
    g.drawText("Detector", detector.reduced(14, 8).removeFromTop(30), juce::Justification::centredLeft);
    for (std::size_t i = 0; i < dials.size(); ++i)
    {
        g.setColour(i < 2 ? style.ink() : style.muted());
        g.setFont(font(i < 2 ? 22.0f : 14.0f, i < 2));
        g.drawText(utf(dials[i].name), labels[i], juce::Justification::centred);
    }
    g.setFont(font(13));
    g.setColour(style.muted());
    g.drawText(status, footer.reduced(2, 0), juce::Justification::centredLeft);
    g.drawText("Umbral  " + juce::String(thresholdDb, 1) + " dBFS",
               footer.reduced(2, 0), juce::Justification::centredRight);
    drawTelemetry(g);
}

void Editor::drawTelemetry(juce::Graphics &g) const
{
    const auto area = meterFrame.toFloat();
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xff232c29), area.getTopLeft(),
                                          screen, area.getBottomRight(), false));
    g.fillRoundedRectangle(area, 13);
    g.setColour(juce::Colour(0xff45504b));
    g.drawRoundedRectangle(area.reduced(1), 12, 1.0f);
    const auto readout = [&](juce::Rectangle<int> position, const char *label, float peak)
    {
        g.setColour(screenMuted);
        g.setFont(font(10.5f, true).withExtraKerningFactor(0.12f));
        g.drawText(label, position.removeFromTop(12), juce::Justification::centred);
        g.setColour(peak >= 0 ? juce::Colour(0xffffa798) : screenInk);
        g.setFont(font(15, true));
        g.drawText(peakText(peak), position, juce::Justification::centred);
    };
    auto peaks = meterTop;
    readout(peaks.removeFromLeft(111), "PICO ENTRADA", values.inputPeakDb);
    readout(peaks.removeFromRight(111), "PICO SALIDA", values.peakDb);
    drawMeter(g);
    drawCurve(g);
    drawHistory(g);
}

double Editor::makeupShownDb() const noexcept
{
    return processor.value(Param::makeup) + (shown.autoMakeup ? OpticalEngine::referenceReductionDb(shown) : 0.0);
}

void Editor::drawCurve(juce::Graphics &g) const
{
    // Curva estática (entrada → salida, pico de un seno en dBFS) calculada con la misma fórmula del motor,
    // y el punto de trabajo en vivo: entrada medida y reducción aplicada.
    if (curveArea.getWidth() < 60 || curveArea.getHeight() < 60)
        return;
    auto box = curveArea.toFloat();
    g.setColour(juce::Colour(0xff101514));
    g.fillRoundedRectangle(box, 6);
    box = box.reduced(8.0f);
    constexpr float lo = -48.0f, hi = 0.0f;
    const auto px = [&](double db) { return box.getX() + box.getWidth() * static_cast<float>((db - lo) / (hi - lo)); };
    const auto py = [&](double db)
    { return box.getBottom() - box.getHeight() * static_cast<float>((juce::jlimit<double>(lo, hi + 6, db) - lo) / (hi - lo)); };
    g.setFont(font(9.5f));
    for (int db = -48; db <= 0; db += 12)
    {
        g.setColour(screenMuted.withAlpha(0.14f));
        g.drawVerticalLine(juce::roundToInt(px(db)), box.getY(), box.getBottom());
        g.drawHorizontalLine(juce::roundToInt(py(db)), box.getX(), box.getRight());
    }
    // 1:1 sin compresión.
    juce::Path unity;
    unity.startNewSubPath(px(lo), py(lo));
    unity.lineTo(px(hi), py(hi));
    juce::Path dashed;
    const float dash[] = {3.0f, 3.0f};
    juce::PathStrokeType(1.0f).createDashedStroke(dashed, unity, dash, 2);
    g.setColour(screenMuted.withAlpha(0.35f));
    g.fillPath(dashed);

    // Umbral.
    const double threshold = -2.0 - 0.40 * shown.reduction;
    if (threshold > lo)
    {
        g.setColour(amber.withAlpha(0.35f));
        g.drawVerticalLine(juce::roundToInt(px(threshold)), box.getY(), box.getBottom());
    }

    const double makeup = makeupShownDb();
    juce::Path curve;
    for (double in = lo; in <= hi + 0.01; in += 0.5)
    {
        const double out = in - OpticalEngine::staticReductionDb(shown, in) + makeup;
        if (in == lo)
            curve.startNewSubPath(px(in), py(out));
        else
            curve.lineTo(px(in), py(out));
    }
    g.setColour(screenInk.withAlpha(0.9f));
    g.strokePath(curve, juce::PathStrokeType(1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Punto de trabajo: el vúmetro de entrada (0 VU = −18 dBFS RMS) pasado a pico de seno, con la reducción real.
    const double inPeak = values.inputVu - 15.0;
    if (inPeak > lo && values.inputVu > -75.0f)
    {
        const double out = inPeak - values.reductionDb + makeup;
        const juce::Point<float> dot{px(juce::jmin<double>(inPeak, hi)), py(out)};
        g.setColour(amber.withAlpha(0.25f));
        g.fillEllipse(juce::Rectangle<float>(16, 16).withCentre(dot));
        g.setColour(amber);
        g.fillEllipse(juce::Rectangle<float>(7, 7).withCentre(dot));
    }

    g.setColour(screenMuted);
    g.setFont(font(9.5f, true).withExtraKerningFactor(0.08f));
    g.drawText("ENTRADA", box.removeFromBottom(12).translated(0, 10), juce::Justification::centredRight);
    g.drawText("CURVA", curveArea.toFloat().reduced(10, 6).removeFromTop(12), juce::Justification::centredLeft);
}

void Editor::drawMeter(juce::Graphics &g) const
{
    const auto box = meterArea.toFloat();
    const auto center = juce::Point<float>{box.getCentreX(), box.getBottom() - 21.0f};
    const auto radius = std::min(box.getWidth() * 0.46f, box.getHeight() - 43.0f);
    constexpr float arc = 1.02f;
    const auto angle = [](double t) { return static_cast<float>(-arc + 2 * arc * juce::jlimit(0.0, 1.0, t)); };
    const auto position = [this](double v)
    { return display == 0 ? 1.0 - std::min(v, 40.0) / 40.0 : std::pow(10.0, (v - 3.0) / 24.0); };
    juce::Path outer;
    outer.addCentredArc(center.x, center.y, radius, radius, 0, -arc, arc, true);
    g.setColour(screenInk.withAlpha(0.55f));
    g.strokePath(outer, juce::PathStrokeType(1.1f));
    const auto tick = [&](double v, bool major)
    {
        const auto a = angle(position(v));
        g.setColour(display != 0 && v > 0 ? amber : screenInk);
        line(g, polar(center, radius - 2, a), polar(center, radius - (major ? 11.0f : 6.0f), a),
             major ? 1.6f : 1);
        if (major)
        {
            const auto c = polar(center, radius + 13, a);
            g.setFont(font(12, true));
            const juce::String number = (display != 0 && v > 0 ? "+" : "") + juce::String(static_cast<int>(v));
            g.drawText(number, juce::Rectangle<float>(32, 16).withCentre(c), juce::Justification::centred);
        }
    };
    if (display == 0)
        for (int v = 0; v <= 40; v += 2)
            tick(v, v % 10 == 0);
    else
        for (int v = -20; v <= 3; ++v)
            if (v >= -10 || v == -20 || v == -15)
                tick(v, v == -20 || v == -10 || v == -5 || v == -3 || v == 0 || v == 3);
    const double val = display == 0 ? values.reductionDb : (display == 1 ? values.inputVu : values.outputVu);
    const auto a = angle(position(val));
    const auto tip = polar(center, radius - 4, a);
    g.setColour(juce::Colours::black.withAlpha(0.35f));
    line(g, center.translated(2, 2), tip.translated(2, 2), 4);
    g.setColour(amber);
    line(g, center, tip, 2.5f);
    g.fillEllipse(juce::Rectangle<float>(8, 8).withCentre(center));
    g.setFont(font(12));
    g.setColour(screenMuted);
    auto bottom = meterArea.reduced(4, 2).removeFromBottom(18);
    g.drawText(display == 0 ? utf("Reducción óptica") : utf("0 VU = −18 dBFS"), bottom,
               juce::Justification::centredLeft);
    g.setColour(amber);
    g.setFont(font(16, true));
    const auto value = val < -75 ? utf("−∞") : juce::String(display == 0 && val > 0.05 ? -val : val, 1);
    g.drawText(value + (display == 0 ? " dB" : " VU"), bottom, juce::Justification::centredRight);
}

void Editor::drawHistory(juce::Graphics &g) const
{
    auto zone = historyArea;
    auto label = zone.removeFromTop(22).reduced(4, 0);
    g.setFont(font(11, true));
    g.setColour(screenMuted);
    g.drawText(utf("Historial de reducción"), label.removeFromLeft(150), juce::Justification::centredLeft);
    const auto readout = [&](juce::Rectangle<int> bounds, const char *channel, float value, juce::Colour colour)
    {
        g.setColour(colour);
        g.drawText(juce::String(channel) + "  " + juce::String(value > 0.05f ? -value : 0.0f, 1) + " dB",
                   bounds, juce::Justification::centredRight);
    };
    readout(label.removeFromRight(75), "R", values.reductionRightDb, cyan);
    readout(label.removeFromRight(75), "L", values.reductionLeftDb, amber);
    auto chart = zone.toFloat().reduced(4.0f, 1.0f);
    g.setColour(juce::Colour(0xff101514));
    g.fillRoundedRectangle(chart, 6);
    chart.reduce(5.0f, 4.0f);
    const auto scale = chart.removeFromLeft(27.0f);
    g.setFont(font(9.5f));
    for (int db = 0; db <= 40; db += 10)
    {
        const float y = chart.getY() + chart.getHeight() * static_cast<float>(db) / 40.0f;
        g.setColour(screenMuted.withAlpha(0.16f));
        g.drawLine(chart.getX(), y, chart.getRight(), y, 1.0f);
        if (db % 20 == 0)
        {
            g.setColour(screenMuted);
            const auto textY = juce::jlimit(scale.getY(), scale.getBottom() - 10.0f, y - 5.0f);
            g.drawText(juce::String(-db), juce::Rectangle<float>{scale.getX(), textY, scale.getWidth() - 5.0f, 10.0f},
                       juce::Justification::centredRight);
        }
    }
    if (activeHistory == 0)
        return;
    std::array<juce::Path, 2> traces;
    for (std::size_t i = 0; i < historyLength; ++i)
    {
        const auto &point = history[(historyHead + i) % historyLength];
        const float x = chart.getX() + chart.getWidth() * static_cast<float>(i) / static_cast<float>(historyLength - 1);
        for (std::size_t ch = 0; ch < traces.size(); ++ch)
        {
            const float y = chart.getY() + chart.getHeight() * point[ch] / 40.0f;
            if (i == 0)
                traces[ch].startNewSubPath(x, y);
            else
                traces[ch].lineTo(x, y);
        }
    }
    g.setColour(cyan);
    g.strokePath(traces[1], juce::PathStrokeType(1.2f));
    g.setColour(amber);
    g.strokePath(traces[0], juce::PathStrokeType(1.5f));
}
} // namespace luma
