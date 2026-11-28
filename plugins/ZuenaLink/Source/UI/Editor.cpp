#include "Editor.h"
#include <BinaryData.h>
#include <cmath>

namespace zlink
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

// Estados: color solo en el punto indicador (la marca no usa rellenos de color).
const juce::Colour good{0xff36a173};
const juce::Colour wait{0xffd6a03e};
const juce::Colour bad{0xffd0564f};
const juce::Colour rec{0xffe0443c};

juce::String decimal(double v, int places)
{
    return juce::String(v, places).replaceCharacter('.', ',');
}

juce::String clock(double seconds)
{
    const auto t = static_cast<int>(seconds);
    const auto h = t / 3600, m = (t / 60) % 60, s = t % 60;
    const auto two = [](int x) { return juce::String(x).paddedLeft('0', 2); };
    return h > 0 ? juce::String(h) + ":" + two(m) + ":" + two(s) : two(m) + ":" + two(s);
}

juce::String size(std::uint64_t bytes)
{
    const double mb = static_cast<double>(bytes) / (1024.0 * 1024.0);
    return mb >= 1024 ? decimal(mb / 1024.0, 2) + " GB" : decimal(mb, 1) + " MB";
}

juce::String channelName(std::uint32_t i)
{
    return "Link " + juce::String(static_cast<int>(i) + 1);
}

constexpr int kMissingDevice = 10000;
} // namespace

// ---------------------------------------------------------------- estilo
juce::Colour LinkStyle::paper() const
{
    return juce::Colour(dark ? 0xff141617 : 0xfff5f5f3);
}
juce::Colour LinkStyle::surface() const
{
    return juce::Colour(dark ? 0xff1d2022 : 0xfffdfdfb);
}
juce::Colour LinkStyle::ink() const
{
    return juce::Colour(dark ? 0xfff2f1ed : 0xff181a1c);
}
juce::Colour LinkStyle::muted() const
{
    return juce::Colour(dark ? 0xffb1b4b5 : 0xff555b5e);
}
juce::Colour LinkStyle::rim() const
{
    return ink().withAlpha(0.16f);
}
void LinkStyle::apply(bool night)
{
    dark = night;
    const auto face = dark ? juce::Colour(0xff27292b) : juce::Colour(0xfffdfdfb);
    setColour(juce::TextButton::buttonColourId, face);
    setColour(juce::TextButton::buttonOnColourId, ink());
    setColour(juce::TextButton::textColourOffId, ink());
    setColour(juce::TextButton::textColourOnId, paper());
    setColour(juce::ComboBox::backgroundColourId, face);
    setColour(juce::ComboBox::outlineColourId, rim());
    setColour(juce::ComboBox::arrowColourId, muted());
    setColour(juce::ComboBox::textColourId, ink());
    setColour(juce::Slider::textBoxTextColourId, ink());
    setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour(juce::PopupMenu::backgroundColourId, paper());
    setColour(juce::PopupMenu::textColourId, ink());
    setColour(juce::PopupMenu::headerTextColourId, muted());
    setColour(juce::PopupMenu::highlightedBackgroundColourId, ink().withAlpha(0.10f));
    setColour(juce::PopupMenu::highlightedTextColourId, ink());
    setColour(juce::TooltipWindow::backgroundColourId, paper());
    setColour(juce::TooltipWindow::textColourId, ink());
    setColour(juce::TooltipWindow::outlineColourId, rim());
}
void LinkStyle::drawButtonBackground(juce::Graphics &g, juce::Button &button, const juce::Colour &fill, bool hover,
                                     bool down)
{
    const auto area = button.getLocalBounds().toFloat().reduced(0.75f);
    const auto face = button.getToggleState() ? ink() : fill;
    g.setColour(face.interpolatedWith(button.getToggleState() ? paper() : ink(), down ? 0.17f : (hover ? 0.08f : 0.0f))
                    .withMultipliedAlpha(button.isEnabled() ? 1.0f : 0.5f));
    g.fillRoundedRectangle(area, 7.0f);
    g.setColour(button.hasKeyboardFocus(false) ? muted() : rim());
    g.drawRoundedRectangle(area, 7.0f, 1.0f);
}
juce::Font LinkStyle::getTextButtonFont(juce::TextButton &, int h)
{
    return font(std::min(14.0f, h * 0.48f), true);
}
juce::Font LinkStyle::getComboBoxFont(juce::ComboBox &b)
{
    return font(std::min(14.0f, b.getHeight() * 0.5f));
}
juce::Font LinkStyle::getPopupMenuFont()
{
    return font(14.0f);
}
void LinkStyle::drawLinearSlider(juce::Graphics &g, int x, int y, int w, int h, float pos, float, float,
                                 juce::Slider::SliderStyle, juce::Slider &slider)
{
    const float cy = static_cast<float>(y) + h * 0.5f;
    const float left = static_cast<float>(x), right = static_cast<float>(x + w);
    g.setColour(rim());
    g.fillRoundedRectangle(left, cy - 2.0f, right - left, 4.0f, 2.0f);
    // El tramo activo arranca en 0 dB: se ve si el envío sube o baja respecto de la pista.
    const float zero = left + static_cast<float>(slider.valueToProportionOfLength(0.0)) * (right - left);
    g.setColour(ink());
    g.fillRoundedRectangle(std::min(zero, pos), cy - 2.0f, std::abs(pos - zero), 4.0f, 2.0f);
    g.setColour(rim());
    g.fillRect(zero - 0.5f, cy - 7.0f, 1.0f, 14.0f);
    g.setColour(surface());
    g.fillEllipse(pos - 8.0f, cy - 8.0f, 16.0f, 16.0f);
    g.setColour(ink());
    g.drawEllipse(pos - 8.0f, cy - 8.0f, 16.0f, 16.0f, slider.hasKeyboardFocus(false) ? 2.0f : 1.4f);
}
juce::Label *LinkStyle::createSliderTextBox(juce::Slider &s)
{
    auto *l = LookAndFeel_V4::createSliderTextBox(s);
    l->setFont(font(14.0f, true));
    l->setJustificationType(juce::Justification::centredRight);
    return l;
}

// ---------------------------------------------------------------- botones
Switch::Switch(const juce::String &name) : juce::Button(name)
{
    setClickingTogglesState(true);
    setTitle(name);
}
void Switch::paintButton(juce::Graphics &g, bool hover, bool)
{
    auto *s = dynamic_cast<LinkStyle *>(&getLookAndFeel());
    const auto ink = s != nullptr ? s->ink() : juce::Colours::black;
    const auto paper = s != nullptr ? s->paper() : juce::Colours::white;
    const auto pill = getLocalBounds().toFloat().withSizeKeepingCentre(40.0f, 22.0f);
    const bool on = getToggleState();
    g.setColour(on ? ink : ink.withAlpha(hover ? 0.22f : 0.16f));
    g.fillRoundedRectangle(pill, 11.0f);
    const float knob = 16.0f;
    const auto dot = juce::Rectangle<float>(knob, knob).withCentre({on ? pill.getRight() - 11.0f : pill.getX() + 11.0f, pill.getCentreY()});
    g.setColour(on ? paper : (s != nullptr ? s->surface() : juce::Colours::white));
    g.fillEllipse(dot);
    if (hasKeyboardFocus(false))
    {
        g.setColour(ink.withAlpha(0.6f));
        g.drawRoundedRectangle(pill.expanded(2.5f), 13.0f, 1.0f);
    }
}

RecordButton::RecordButton() : juce::Button("Grabar")
{
    setClickingTogglesState(true);
    setTitle("Grabar");
}
void RecordButton::paintButton(juce::Graphics &g, bool hover, bool down)
{
    auto *s = dynamic_cast<LinkStyle *>(&getLookAndFeel());
    const auto ring = s != nullptr ? s->rim() : juce::Colours::grey;
    const auto c = getLocalBounds().toFloat().getCentre();
    const auto outer = juce::Rectangle<float>(30.0f, 30.0f).withCentre(c);
    g.setColour(ring);
    g.drawEllipse(outer, hover ? 1.6f : 1.0f);
    g.setColour(rec.withMultipliedBrightness(down ? 0.85f : 1.0f));
    if (getToggleState())
        g.fillRoundedRectangle(juce::Rectangle<float>(12.0f, 12.0f).withCentre(c), 2.0f);
    else
        g.fillEllipse(juce::Rectangle<float>(18.0f, 18.0f).withCentre(c));
    if (hasKeyboardFocus(false))
    {
        g.setColour(ring.withAlpha(0.9f));
        g.drawEllipse(outer.expanded(3.0f), 1.0f);
    }
}

// ---------------------------------------------------------------- editor
Editor::Editor(Processor &p) : AudioProcessorEditor(p), processor(p)
{
    setLookAndFeel(&style);
    setTitle("ZUENA Link");

    obsSwitch.setTooltip(utf("Manda la pista a la fuente «ZUENA Link» de OBS, sin cable virtual."));
    obsSwitch.onClick = [this] { processor.gesture(Param::obs, obsSwitch.getToggleState() ? 1.0f : 0.0f); timerCallback(); };
    addAndMakeVisible(obsSwitch);

    for (std::uint32_t i = 0; i < kLinkChannels; ++i)
        channel.addItem(channelName(i), static_cast<int>(i) + 1);
    channel.setTitle("Canal de OBS");
    channel.setTooltip(utf("Elegí el mismo canal en la fuente de OBS. Usá canales distintos para mandar varias pistas por separado."));
    channel.onChange = [this]
    {
        if (channel.getSelectedId() > 0)
            processor.gesture(Param::channel, static_cast<float>(channel.getSelectedId() - 1));
        timerCallback();
    };
    addAndMakeVisible(channel);

    deviceSwitch.setTooltip(utf("Manda la pista a una salida de Windows: cable virtual, canales de loopback de tu placa, Streamlabs, Discord…"));
    deviceSwitch.onClick = [this] { processor.gesture(Param::device, deviceSwitch.getToggleState() ? 1.0f : 0.0f); timerCallback(); };
    addAndMakeVisible(deviceSwitch);

    deviceList.setTitle("Salida");
    deviceList.setTextWhenNothingSelected(utf("Elegí una salida…"));
    deviceList.setTextWhenNoChoicesAvailable(utf("No hay salidas de audio"));
    deviceList.onChange = [this] { applyDevice(); };
    addAndMakeVisible(deviceList);

    mode.addItem("Compartido", 1);
    mode.addItem("Exclusivo", 2);
    mode.setTitle("Modo de la salida");
    mode.setTooltip(utf("Compartido: la salida sigue sonando para otros programas, con la latencia más baja que permita el driver. Exclusivo: la salida queda solo para ZUENA Link, con el período mínimo del hardware."));
    mode.onChange = [this] { applyDevice(); };
    addAndMakeVisible(mode);

    pair.setTitle("Canales de la salida");
    pair.setTooltip(utf("En placas con más de dos salidas, elegí a qué par mandar (por ejemplo 3-4)."));
    pair.onChange = [this] { applyDevice(); };
    addAndMakeVisible(pair);

    record.setTooltip(utf("Graba lo que se envía a un WAV, en un hilo aparte que nunca frena al DAW."));
    record.onClick = [this]
    {
        if (record.getToggleState())
            processor.engine().startRecording();
        else
            processor.engine().stopRecording();
        timerCallback();
    };
    addAndMakeVisible(record);

    format.addItem("WAV 16 bits", 1);
    format.addItem("WAV 24 bits", 2);
    format.addItem("WAV 32 bits flotante", 3);
    format.setTitle(utf("Formato de grabación"));
    format.setTooltip(utf("16 bits lleva dither. 32 bits flotante guarda cualquier pico sin recortar."));
    format.onChange = [this]
    {
        auto r = processor.engine().recording();
        r.format = static_cast<SampleFormat>(std::clamp(format.getSelectedId() - 1, 0, 2));
        processor.engine().setRecording(r);
        timerCallback();
    };
    addAndMakeVisible(format);

    folder.setTitle(utf("Carpeta de grabación"));
    folder.setTooltip(utf("Elegí dónde se guardan las grabaciones."));
    folder.onClick = [this] { chooseFolder(); };
    addAndMakeVisible(folder);

    reveal.setTitle(utf("Abrir la grabación"));
    reveal.setTooltip(utf("Muestra la última grabación (o la carpeta) en el Explorador."));
    reveal.onClick = [this]
    {
        const auto last = snap.rec.file;
        juce::File f(juce::String(last.wstring().c_str()));
        if (!last.empty() && f.existsAsFile())
            f.revealToUser();
        else
        {
            juce::File dir(juce::String(processor.engine().recording().folder.wstring().c_str()));
            dir.createDirectory();
            dir.startAsProcess();
        }
    };
    addAndMakeVisible(reveal);

    send.setSliderStyle(juce::Slider::LinearHorizontal);
    send.setTextBoxStyle(juce::Slider::TextBoxRight, false, 78, 24);
    send.setRange(kSendMinDb, kSendMaxDb, 0.1);
    send.setDoubleClickReturnValue(true, 0.0);
    send.setNumDecimalPlacesToDisplay(1);
    send.setTextValueSuffix(" dB");
    send.textFromValueFunction = [](double v) { return (v <= kSendMinDb + 0.05 ? utf("−60,0") : decimal(v, 1)) + " dB"; };
    send.valueFromTextFunction = [](const juce::String &t) { return t.replaceCharacter(',', '.').retainCharacters("-0123456789.").getDoubleValue(); };
    send.setTitle(utf("Nivel de envío"));
    send.setTooltip(utf("Nivel de lo que se envía (la pista suena igual). Doble clic: 0 dB."));
    send.setValue(p.value(Param::send), juce::dontSendNotification);
    send.onDragStart = [this] { dragging = true; processor.parameter(Param::send).beginChangeGesture(); };
    send.onDragEnd = [this] { processor.parameter(Param::send).endChangeGesture(); dragging = false; };
    send.onValueChange = [this]
    {
        auto &a = processor.parameter(Param::send);
        const auto v = static_cast<float>(send.getValue());
        if (dragging)
            a.setValueNotifyingHost(a.convertTo0to1(v));
        else
            processor.gesture(Param::send, v);
    };
    addAndMakeVisible(send);

    mono.setClickingTogglesState(true);
    mono.setTitle(utf("Envío mono"));
    mono.setTooltip(utf("Suma izquierda y derecha en el envío (útil para una voz). La pista sigue en estéreo."));
    mono.onClick = [this] { processor.gesture(Param::mono, mono.getToggleState() ? 1.0f : 0.0f); };
    addAndMakeVisible(mono);

    theme.setTitle("Tema");
    theme.onClick = [this]
    {
        processor.setDark(!style.dark);
        timerCallback();
    };
    addAndMakeVisible(theme);

    refreshTheme(p.isDark());
    processor.engine().refreshDevices();
    setResizable(true, true);
    setResizeLimits(660, 500, 1000, 680);
    setSize(720, 520);
    timerCallback();
    startTimerHz(30);
}

Editor::~Editor()
{
    stopTimer();
    setLookAndFeel(nullptr);
}

void Editor::refreshTheme(bool night)
{
    style.apply(night);
    theme.setButtonText(night ? "Tema oscuro" : "Tema claro");
    sendLookAndFeelChange();
    repaint();
}

const Endpoint *Editor::selectedEndpoint() const
{
    const int id = deviceList.getSelectedId();
    if (id <= 0 || id == kMissingDevice || static_cast<std::size_t>(id) > endpoints.size())
        return nullptr;
    return &endpoints[static_cast<std::size_t>(id - 1)];
}

void Editor::rebuildDevices()
{
    endpoints = snap.endpoints;
    endpointsShown = snap.endpointsVersion;
    const auto current = processor.engine().device();
    deviceList.clear(juce::dontSendNotification);
    bool heading = false, others = false;
    int selected = 0;
    for (std::size_t i = 0; i < endpoints.size(); ++i)
    {
        const auto &e = endpoints[i];
        if (isRecommended(e.route) && !heading)
        {
            deviceList.addSectionHeading(utf("Recomendadas para transmitir"));
            heading = true;
        }
        if (!isRecommended(e.route) && !others)
        {
            if (heading)
                deviceList.addSeparator();
            deviceList.addSectionHeading("Otras salidas");
            others = true;
        }
        juce::String label = juce::String::fromUTF8(e.name.c_str());
        if (e.isDefault)
            label << utf(" · predeterminada");
        deviceList.addItem(label, static_cast<int>(i) + 1);
        if (e.id == current.id)
            selected = static_cast<int>(i) + 1;
    }
    if (selected == 0 && !current.id.empty())
    {
        deviceList.addSeparator();
        deviceList.addItem(juce::String::fromUTF8(current.name.c_str()) + utf(" (no conectada)"), kMissingDevice);
        selected = kMissingDevice;
    }
    deviceList.setSelectedId(selected, juce::dontSendNotification);
    updatePairs();
}

void Editor::updatePairs()
{
    const auto *e = selectedEndpoint();
    const auto current = processor.engine().device();
    const auto pairs = e != nullptr ? std::max<std::uint32_t>(1, e->channels / 2) : 1;
    pair.clear(juce::dontSendNotification);
    for (std::uint32_t i = 0; i < pairs; ++i)
        pair.addItem("Canales " + juce::String(static_cast<int>(2 * i + 1)) + "-" + juce::String(static_cast<int>(2 * i + 2)),
                     static_cast<int>(i) + 1);
    pair.setSelectedId(static_cast<int>(std::min(current.pair, pairs - 1)) + 1, juce::dontSendNotification);
    pair.setEnabled(pairs > 1);
    mode.setSelectedId(current.exclusive ? 2 : 1, juce::dontSendNotification);
}

void Editor::applyDevice()
{
    auto d = processor.engine().device();
    if (const auto *e = selectedEndpoint())
    {
        if (e->id != d.id)
            d.pair = 0;
        d.id = e->id;
        d.name = e->name;
    }
    d.exclusive = mode.getSelectedId() == 2;
    if (pair.getSelectedId() > 0)
        d.pair = static_cast<std::uint32_t>(pair.getSelectedId() - 1);
    processor.engine().setDevice(d);
    updatePairs();
    // Elegir una salida la activa: es lo que el usuario quiere el 99 % de las veces.
    if (!deviceSwitch.getToggleState() && selectedEndpoint() != nullptr)
        processor.gesture(Param::device, 1.0f);
    timerCallback();
}

void Editor::chooseFolder()
{
    const juce::File start(juce::String(processor.engine().recording().folder.wstring().c_str()));
    chooser = std::make_unique<juce::FileChooser>(utf("Carpeta de grabación"), start);
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                         [this](const juce::FileChooser &fc)
                         {
                             const auto dir = fc.getResult();
                             if (dir == juce::File{})
                                 return;
                             auto r = processor.engine().recording();
                             r.folder = std::filesystem::path(dir.getFullPathName().toWideCharPointer());
                             processor.engine().setRecording(r);
                             timerCallback();
                         });
}

void Editor::timerCallback()
{
    if (processor.isDark() != style.dark)
        refreshTheme(processor.isDark());
    snap = processor.engine().snapshot();

    // Controles ↔ parámetros (también cuando cambian por automatización).
    obsSwitch.setToggleState(processor.value(Param::obs) >= 0.5f, juce::dontSendNotification);
    deviceSwitch.setToggleState(processor.value(Param::device) >= 0.5f, juce::dontSendNotification);
    mono.setToggleState(processor.value(Param::mono) >= 0.5f, juce::dontSendNotification);
    const auto ch = static_cast<std::uint32_t>(std::clamp(juce::roundToInt(processor.value(Param::channel)), 0, static_cast<int>(kLinkChannels) - 1));
    channel.setSelectedId(static_cast<int>(ch) + 1, juce::dontSendNotification);
    for (std::uint32_t i = 0; i < kLinkChannels; ++i)
    {
        const bool used = snap.channelInUse[i] && i != ch;
        if (used != inUseShown[i])
        {
            inUseShown[i] = used;
            channel.changeItemText(static_cast<int>(i) + 1, channelName(i) + (used ? utf(" · en uso") : juce::String()));
        }
    }
    if (!dragging && !send.isMouseButtonDown())
        send.setValue(processor.value(Param::send), juce::dontSendNotification);
    if (snap.endpointsVersion != endpointsShown && !deviceList.isPopupActive())
        rebuildDevices();
    const auto rs = processor.engine().recording();
    format.setSelectedId(static_cast<int>(rs.format) + 1, juce::dontSendNotification);
    record.setToggleState(snap.rec.recording, juce::dontSendNotification);

    // --- OBS
    juce::String obs;
    juce::Colour od = style.muted();
    const auto name = channelName(ch);
    switch (snap.obs)
    {
    case ObsState::off:
        obs = "Apagado";
        break;
    case ObsState::opening:
        obs = utf("Preparando ") + name + utf("…");
        break;
    case ObsState::failed:
        obs = utf("No se pudo abrir ") + name + utf(". Probá otro canal.");
        od = bad;
        break;
    case ObsState::waiting:
        obs = utf("Esperando a OBS: agregá la fuente de audio «ZUENA Link» y elegí ") + name + ".";
        od = wait;
        break;
    case ObsState::connected:
        obs = utf("Conectado a OBS · ") + decimal(snap.obsLatencyMs, 1) + utf(" ms · ") + juce::String(snap.obsCuts) +
              (snap.obsCuts == 1 ? " corte" : " cortes");
        od = snap.obsCuts == 0 ? good : wait;
        break;
    case ObsState::busy:
        obs = name + utf(" lo usa otra pista") +
              (snap.otherOwner.empty() ? juce::String() : utf(" (") + juce::String::fromUTF8(snap.otherOwner.c_str()) + ")") +
              utf(". Elegí otro canal.");
        od = bad;
        break;
    case ObsState::exporting:
        obs = utf("Exportando: el envío en vivo está en pausa.");
        break;
    }
    if (snap.obsContender && snap.obs != ObsState::busy)
        obs << utf(" Otra pista también pide ") << name << ".";

    // --- Dispositivo
    juce::String dev, hint;
    juce::Colour dd = style.muted();
    const auto *sel = selectedEndpoint();
    if (!snap.deviceOn)
    {
        dev = deviceSwitch.getToggleState() ? utf("Elegí una salida en el menú.") : utf("Apagado");
        // Sin salida elegida: si hay una placa con loopback o un cable, se ofrece.
        if (sel == nullptr)
            for (const auto &e : endpoints)
                if (isRecommended(e.route) && e.route.kind != RouteKind::hardware)
                {
                    hint = utf("Detectamos ") + juce::String::fromUTF8(e.name.c_str()) + utf(": elegila para transmitir.");
                    break;
                }
        if (hint.isEmpty() && sel == nullptr)
            for (const auto &e : endpoints)
                if (e.route.interfaceHasLoopback)
                {
                    hint = utf("Detectamos una placa ") + juce::String::fromUTF8(e.route.vendor.c_str()) +
                           utf(" con loopback: elegí sus salidas para transmitir.");
                    break;
                }
    }
    else if (snap.deviceMissing)
    {
        dev = utf("«") + juce::String::fromUTF8(snap.deviceSettings.name.c_str()) + utf("» no está conectada. Se reconecta sola cuando vuelva.");
        dd = wait;
    }
    else
    {
        const auto &d = snap.device;
        switch (d.phase)
        {
        case WasapiOutput::Phase::stopped:
        case WasapiOutput::Phase::opening:
            dev = utf("Abriendo la salida…");
            break;
        case WasapiOutput::Phase::running:
            dev = decimal(d.deviceRate / 1000.0, d.deviceRate % 1000 == 0 ? 0 : 1) + utf(" kHz · ") +
                  decimal(d.latencyMs, 1) + utf(" ms · ") + (d.exclusive ? "exclusivo" : (d.lowLatencyShared ? "compartido, baja latencia" : "compartido")) +
                  utf(" · ") + juce::String(d.cuts) + (d.cuts == 1 ? " corte" : " cortes");
            dd = d.cuts == 0 ? good : wait;
            break;
        case WasapiOutput::Phase::retrying:
            dev = juce::String::fromUTF8(d.error.c_str()) + utf(" Reintentando…");
            dd = bad;
            break;
        }
    }
    if (sel != nullptr && hint.isEmpty())
        hint = juce::String::fromUTF8(sel->route.obsHint.c_str());
    if (snap.offline && snap.deviceOn)
        dev = utf("Exportando: el envío en vivo está en pausa.");

    // --- Grabación
    juce::String recText, recHint;
    juce::Colour rd = style.muted();
    if (snap.rec.recording)
    {
        recText = utf("Grabando · ") + clock(snap.rec.seconds) + utf(" · ") + size(snap.rec.bytes);
        if (snap.rec.cuts > 0)
            recText << utf(" · ") << juce::String(snap.rec.cuts) << utf(" huecos (disco lento)");
        rd = rec;
    }
    else if (!snap.rec.error.empty())
    {
        recText = juce::String::fromUTF8(snap.rec.error.c_str());
        rd = bad;
    }
    else if (!snap.rec.file.empty())
        recText = utf("Último archivo: ") + juce::String(snap.rec.file.filename().wstring().c_str()) + utf(" · ") + clock(snap.rec.seconds);
    else
        recText = utf("Listo para grabar.");
    if (snap.rec.clipped > 0 && rs.format != SampleFormat::float32)
        recText << utf(" · picos recortados: usá 32 bits flotante");
    recHint = juce::String(rs.folder.wstring().c_str());

    if (obs != obsStatus || dev != deviceStatus || hint != deviceHint || recText != recordStatus || recHint != recordHint ||
        od != obsDot || dd != deviceDot || rd != recordDot)
    {
        obsStatus = obs;
        deviceStatus = dev;
        deviceHint = hint;
        recordStatus = recText;
        recordHint = recHint;
        obsDot = od;
        deviceDot = dd;
        recordDot = rd;
        for (const auto &r : rows)
            repaint(r);
    }

    // Medidor de envío: pico con caída de 24 dB/s.
    const auto toLevel = [](float peak) { return peak <= 1e-6f ? -90.0f : 20.0f * std::log10(peak); };
    const float fall = 24.0f / 30.0f;
    const float l = toLevel(processor.engine().takePeak(0)), r = toLevel(processor.engine().takePeak(1));
    const float nl = std::max(l, meterL - fall), nr = std::max(r, meterR - fall);
    if (std::abs(nl - meterL) > 0.05f || std::abs(nr - meterR) > 0.05f)
    {
        meterL = nl;
        meterR = nr;
        repaint(meterArea);
    }
}

void Editor::drawRow(juce::Graphics &g, int row, const char *title, const juce::String &status, juce::Colour dot,
                     const juce::String &hint) const
{
    const auto card = rows[static_cast<std::size_t>(row)].toFloat();
    g.setColour(style.surface());
    g.fillRoundedRectangle(card, 12.0f);
    g.setColour(style.rim());
    g.drawRoundedRectangle(card.reduced(0.5f), 12.0f, 1.0f);
    auto text = textAreas[static_cast<std::size_t>(row)];
    g.setColour(style.ink());
    g.setFont(font(16.0f, true));
    g.drawText(utf(title), text.removeFromTop(30), juce::Justification::centredLeft); // alineado con los controles
    text.removeFromTop(2);
    auto line = text.removeFromTop(18);
    g.setColour(dot);
    g.fillEllipse(juce::Rectangle<float>(7.0f, 7.0f).withCentre({static_cast<float>(line.getX()) + 3.5f, static_cast<float>(line.getCentreY())}));
    line.removeFromLeft(14);
    g.setColour(style.ink());
    g.setFont(font(13.0f));
    g.drawFittedText(status, line, juce::Justification::centredLeft, 1, 0.92f);
    if (hint.isNotEmpty())
    {
        g.setColour(style.muted());
        g.setFont(font(12.0f));
        g.drawFittedText(hint, text.removeFromTop(32), juce::Justification::topLeft, 2, 0.92f);
    }
}

void Editor::paint(juce::Graphics &g)
{
    g.fillAll(style.paper());
    // Encabezado
    auto h = header;
    stamp(g, h.removeFromLeft(30).withSizeKeepingCentre(30, 30).toFloat(), style.dark);
    h.removeFromLeft(12);
    g.setColour(style.ink());
    g.setFont(font(21.0f, true));
    g.drawText("ZUENA Link", h.removeFromTop(h.getHeight() / 2 + 4), juce::Justification::bottomLeft);
    g.setColour(style.muted());
    g.setFont(font(12.5f));
    g.drawText(utf("Tu pista en OBS, en otra salida o en un WAV"), h, juce::Justification::topLeft);

    // Franja de envío
    g.setColour(style.ink());
    g.setFont(font(13.0f, true));
    g.drawText(utf("Envío"), sendStrip.withWidth(60), juce::Justification::centredLeft);
    const auto bar = [&](juce::Rectangle<float> r, float db)
    {
        g.setColour(style.rim());
        g.fillRoundedRectangle(r, 1.5f);
        const float p = std::clamp((db + 60.0f) / 66.0f, 0.0f, 1.0f);
        g.setColour(db > -0.1f ? bad : style.ink());
        g.fillRoundedRectangle(r.withWidth(r.getWidth() * p), 1.5f);
    };
    const auto m = meterArea.toFloat();
    bar(m.withHeight(4.0f).withY(m.getCentreY() - 5.0f), meterL);
    bar(m.withHeight(4.0f).withY(m.getCentreY() + 1.0f), meterR);
    g.setColour(style.rim());
    const float zero = m.getX() + m.getWidth() * 60.0f / 66.0f;
    g.fillRect(zero, m.getCentreY() - 8.0f, 1.0f, 16.0f);

    drawRow(g, 0, "OBS directo", obsStatus, obsDot, {});
    drawRow(g, 1, "Dispositivo", deviceStatus, deviceDot, deviceHint);
    drawRow(g, 2, "Grabar WAV", recordStatus, recordDot, recordHint);

    g.setColour(style.muted());
    g.setFont(font(12.0f));
    g.drawText(utf("La pista suena igual · 0 ms de latencia agregada"), footer, juce::Justification::centredLeft);
    g.drawText("v" + juce::String(JucePlugin_VersionString), footer, juce::Justification::centredRight);
}

void Editor::resized()
{
    auto b = getLocalBounds().reduced(20, 14);
    header = b.removeFromTop(46);
    theme.setBounds(header.withTrimmedLeft(header.getWidth() - 110).withSizeKeepingCentre(110, 30));
    b.removeFromTop(12);
    sendStrip = b.removeFromTop(36);
    {
        auto s = sendStrip.withTrimmedLeft(60);
        mono.setBounds(s.removeFromRight(64).withSizeKeepingCentre(64, 28));
        s.removeFromRight(14);
        meterArea = s.removeFromRight(juce::jlimit(110, 220, s.getWidth() / 3));
        s.removeFromRight(14);
        send.setBounds(s);
    }
    footer = b.removeFromBottom(20);
    b.removeFromBottom(8);
    const int gap = 10;
    const int rowH = (b.getHeight() - 2 * gap) / 3;
    for (std::size_t i = 0; i < rows.size(); ++i)
    {
        rows[i] = b.removeFromTop(rowH);
        b.removeFromTop(gap);
    }
    const auto layoutRow = [&](std::size_t i, juce::Component &lead)
    {
        auto r = rows[i].reduced(14, 12);
        lead.setBounds(r.removeFromLeft(48).withSizeKeepingCentre(48, 36).withY(r.getY() + 0));
        r.removeFromLeft(10);
        return r;
    };
    // Fila OBS: canal a la derecha.
    {
        auto r = layoutRow(0, obsSwitch);
        channel.setBounds(r.removeFromRight(150).withHeight(30));
        r.removeFromRight(12);
        textAreas[0] = r;
    }
    // Fila dispositivo: salida, modo y par en una línea a la derecha del título.
    {
        auto r = layoutRow(1, deviceSwitch);
        // El título ocupa el comienzo de la primera línea; la salida, el modo y el par, el resto.
        textAreas[1] = r;
        auto controls = r.removeFromTop(30).withTrimmedLeft(112);
        pair.setBounds(controls.removeFromRight(112));
        controls.removeFromRight(8);
        mode.setBounds(controls.removeFromRight(112));
        controls.removeFromRight(8);
        deviceList.setBounds(controls);
    }
    // Fila grabación: formato, carpeta y abrir.
    {
        auto r = layoutRow(2, record);
        reveal.setBounds(r.removeFromRight(74).withHeight(30));
        r.removeFromRight(8);
        folder.setBounds(r.removeFromRight(90).withHeight(30));
        r.removeFromRight(8);
        format.setBounds(r.removeFromRight(170).withHeight(30));
        r.removeFromRight(12);
        textAreas[2] = r;
    }
}
} // namespace zlink
