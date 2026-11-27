#include "PluginEditor.h"
#include "../State/Text.h"
#include "GlobalPrefs.h"

namespace zeq::ui
{
namespace
{
constexpr int kHeader = 84, kFooter = 48, kMargin = 18;
const int kRanges[] = { 6, 12, 30 };

void paintLogo (juce::Graphics& g, juce::Rectangle<float> r)
{
    const float s = r.getWidth() / 64.0f;
    // Marca ZUENA: punto negro con Z blanca en claro; invertido (punto blanco, Z negra) en oscuro.
    g.setColour (theme::isDark() ? juce::Colours::white : juce::Colours::black);
    g.fillEllipse (r);
    juce::Path z;
    z.startNewSubPath (r.getX() + 19.0f * s, r.getY() + 20.0f * s);
    z.lineTo (r.getX() + 45.0f * s, r.getY() + 20.0f * s);
    z.lineTo (r.getX() + 19.0f * s, r.getY() + 44.0f * s);
    z.lineTo (r.getX() + 45.0f * s, r.getY() + 44.0f * s);
    g.setColour (theme::isDark() ? juce::Colours::black : juce::Colours::white);
    g.strokePath (z, juce::PathStrokeType (6.5f * s, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}
} // namespace

// ------------------------------------------------------------------------- BandChip

void BandChip::paintButton (juce::Graphics& g, bool highlighted, bool)
{
    const auto r = getLocalBounds().toFloat().reduced (1.0f);
    const bool on = getToggleState();
    g.setColour (on ? theme::ink : (highlighted ? theme::surface : theme::raised));
    g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
    if (! on)
    {
        g.setColour (highlighted ? theme::ink.withAlpha (0.22f) : theme::line);
        g.drawRoundedRectangle (r, r.getHeight() * 0.5f, 1.0f);
    }
    const bool narrow = r.getWidth() < 42.0f;
    const auto dot = juce::Rectangle<float> (8.0f, 8.0f).withCentre ({ r.getX() + (narrow ? 9.0f : 14.0f), r.getCentreY() });
    g.setColour (bypassed ? theme::muted : theme::bandColour (band));
    g.fillEllipse (dot);
    g.setColour (on ? theme::raised : (bypassed ? theme::muted : theme::ink));
    g.setFont (theme::font (12.0f, true));
    g.drawText (juce::String (band + 1), r.withTrimmedLeft (narrow ? 15.0f : 22.0f), juce::Justification::centredLeft);
}

void BandChip::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
    {
        if (onContextMenu)
            onContextMenu();
        return;
    }
    if (e.mods.isAltDown())
    {
        if (onToggleBypass)
            onToggleBypass();
        return;
    }
    juce::Button::mouseDown (e);
}

void BandChip::mouseDoubleClick (const juce::MouseEvent&)
{
    if (onToggleBypass)
        onToggleBypass();
}

// ------------------------------------------------------------------------- ShortcutsOverlay

ShortcutsOverlay::ShortcutsOverlay()
{
    setOpaque (false);
    setVisible (false);
}

void ShortcutsOverlay::paint (juce::Graphics& g)
{
    g.setColour (juce::Colours::black.withAlpha (theme::isDark() ? 0.56f : 0.24f));
    g.fillAll();

    const auto card = getLocalBounds().withSizeKeepingCentre (680, 396).toFloat();
    juce::DropShadow (theme::shadow, 28, { 0, 12 }).drawForRectangle (g, card.toNearestInt());
    g.setColour (theme::raised);
    g.fillRoundedRectangle (card, 22.0f);
    g.setColour (theme::line);
    g.drawRoundedRectangle (card, 22.0f, 1.0f);

    auto inner = card.reduced (26.0f, 22.0f);
    g.setColour (theme::ink);
    g.setFont (theme::font (16.5f, true));
    g.drawText (zeq::utf8 ("Atajos y gestos rápidos"), inner.removeFromTop (26.0f), juce::Justification::centredLeft);
    g.setColour (theme::muted);
    g.setFont (theme::font (12.0f));
    g.drawText ("Clic en cualquier lugar o tecla Esc / ? para cerrar", inner.removeFromTop (20.0f), juce::Justification::centredLeft);
    inner.removeFromTop (12.0f);

    struct Row { juce::String key; juce::String desc; };
    const Row leftCol[] = {
        { "Doble clic / Ctrl+clic", "Crear banda (sugerencia inteligente)" },
        { "Arrastrar en blanco", zeq::utf8 ("Selección múltiple con lazo") },
        { "Shift + arrastrar", zeq::utf8 ("Ajuste fino de alta precisión") },
        { "Alt + arrastrar nodo", "Bloquear eje (solo frec. o ganancia)" },
        { "Ctrl + Alt + arrastrar", "Ajustar Q sin rueda" },
        { "Alt + Shift + arrastrar", "Escuchar banda en Solo al mover" },
        { "Rueda / Alt + rueda", zeq::utf8 ("Q  /  Pendiente o Rango dinámico") },
        { "Ctrl+clic en nodo/perilla", "Restablecer a valor por defecto" },
    };
    const Row rightCol[] = {
        { "Tab / Shift+Tab", "Recorrer bandas por frecuencia" },
        { "1 .. 9", "Seleccionar banda 1 a 9" },
        { "Flechas (Shift = fino)", "Mover frecuencia (semitono) y dB" },
        { "B  /  S  /  D", zeq::utf8 ("Bypass / Solo / Dinámica en banda") },
        { "T  /  I", "Cambiar tipo  /  Invertir ganancia" },
        { "F  /  P  /  M", "Congelar / Fase / Modo oscuro" },
        { "N  /  Supr", "Nueva banda  /  Borrar seleccionadas" },
        { "Clic en valor de perilla", "Escribir Hz, kHz o nota (ej. A4, F#2)" },
    };

    const float colW = (inner.getWidth() - 24.0f) * 0.5f;
    auto drawCol = [&] (const Row* rows, int count, float x0)
    {
        float y = inner.getY();
        for (int i = 0; i < count; ++i)
        {
            g.setColour (theme::ink);
            g.setFont (theme::font (11.5f, true));
            g.drawText (rows[i].key, juce::Rectangle<float> (x0, y, 148.0f, 22.0f), juce::Justification::centredLeft);
            g.setColour (theme::muted);
            g.setFont (theme::font (11.5f));
            g.drawText (rows[i].desc, juce::Rectangle<float> (x0 + 152.0f, y, colW - 152.0f, 22.0f), juce::Justification::centredLeft);
            y += 34.0f;
        }
    };
    drawCol (leftCol, 8, inner.getX());
    drawCol (rightCol, 8, inner.getX() + colW + 24.0f);
}

// ------------------------------------------------------------------------- ZuenaEqEditor

ZuenaEqEditor::ZuenaEqEditor (ZuenaEqProcessor& p)
    : AudioProcessorEditor (p), proc (p), access (p.getState()), graph (p, access), card (access, p.getUndoManager())
{
    setLookAndFeel (&lnf);
    tooltips.setLookAndFeel (&lnf);

    root.onPaint = [this] (juce::Graphics& g) { paintContent (g); };
    root.onResized = [this] { layoutContent(); };
    addAndMakeVisible (root);

    root.addAndMakeVisible (graph);
    // Oculta hasta que se selecciona una banda: con addAndMakeVisible aparecía vacía al abrir (EB-28).
    root.addChildComponent (card);
    root.addChildComponent (shortcutsOverlay);

    graph.onSelectionChanged = [this] (int b) { select (b); };
    graph.onToggleFreezeRequested = [this] { setFrozen (! graph.isFrozen()); };
    graph.onToggleDarkRequested = [this] { setDarkMode (! theme::isDark()); };
    card.onDeleted = [this] { graph.setSelected (-1); };
    card.onClosed = [this] { graph.setSelected (-1); };
    card.onStepBand = [this] (int delta)
    {
        const int next = access.stepBand (card.getBand(), delta);
        if (next >= 0)
            graph.setSelected (next);
    };
    card.onUserMoved = [this] (juce::Point<int> newTopLeft)
    {
        const auto g = graph.getBounds().reduced (8);
        const int x = juce::jlimit (g.getX(), juce::jmax (g.getX(), g.getRight() - card.getWidth()), newTopLeft.x);
        const int y = juce::jlimit (g.getY(), juce::jmax (g.getY(), g.getBottom() - card.getHeight()), newTopLeft.y);
        cardPinnedByUser = true;
        pinnedCardPos = { (float) x, (float) y };
        animCard.setPosition (pinnedCardPos);
        card.setTopLeftPosition (x, y);
    };
    card.setAutoListenProvider ([this] { return graph.getAutoListen(); });
    shortcutsOverlay.onClose = [this] { setShortcutsVisible (false); };

    // Presets
    const auto& presetList = presets::factoryList();
    for (int i = 0; i < (int) presetList.size(); ++i)
        presetBox.addItem (presetList[(size_t) i].name, i + 1);
    presetBox.setTextWhenNothingSelected ("Presets");
    presetBox.setName ("Presets");
    presetBox.setTooltip (zeq::utf8 ("Cargar un ajuste de fábrica (reversible con Deshacer)"));
    presetBox.onChange = [this]
    {
        const int idx = presetBox.getSelectedItemIndex();
        if (idx >= 0)
        {
            graph.setSelected (-1);
            presets::applyFactoryPreset (proc.getState(), proc.getUndoManager(), idx);
            presetBox.setSelectedId (0, juce::dontSendNotification);
        }
    };
    root.addAndMakeVisible (presetBox);

    // Comparación A / B
    slotABtn.setTooltip ("Estado A");
    slotBBtn.setTooltip ("Estado B");
    copyAbBtn.setTooltip ("Copiar el estado actual a la otra ranura");
    slotABtn.onClick = [this] { presets::switchSlot (proc, proc.getState(), proc.getUndoManager(), 0); refreshAbButtons(); };
    slotBBtn.onClick = [this] { presets::switchSlot (proc, proc.getState(), proc.getUndoManager(), 1); refreshAbButtons(); };
    copyAbBtn.onClick = [this] { presets::copyCurrentToOther (proc, proc.getState()); };
    for (auto* b : { &slotABtn, &slotBBtn, &copyAbBtn })
        root.addAndMakeVisible (b);

    // Analizador, escala y modo de fase
    for (int i = 0; i < (int) analyzerButtons.size(); ++i)
    {
        analyzerButtons[(size_t) i].onClick = [this, i] { setAnalyzer ((AnalyzerMode) i); };
        root.addAndMakeVisible (analyzerButtons[(size_t) i]);
    }
    for (int i = 0; i < (int) rangeButtons.size(); ++i)
    {
        rangeButtons[(size_t) i].onClick = [this, i] { setRange (kRanges[i], true); };
        root.addAndMakeVisible (rangeButtons[(size_t) i]);
    }

    qualityBox.addItemList (params::qualityNames(), 1);
    qualityBox.setName ("Modo de fase");
    qualityBox.setTooltip (zeq::utf8 ("Latencia cero (0 muestras) o Fase natural (sobremuestreo FIR 2x de alta precisión)"));
    qualityAtt = std::make_unique<juce::ComboBoxParameterAttachment> (
        *p.getState().getParameter (params::qualityModeId), qualityBox, &p.getUndoManager());
    root.addAndMakeVisible (qualityBox);

    // Deshacer / Rehacer, Auto-ganancia, Bypass global, Escala % y Salida dB
    undo.setTooltip ("Deshacer (Ctrl+Z)");
    redo.setTooltip ("Rehacer (Ctrl+Shift+Z)");
    undo.onClick = [this] { proc.getUndoManager().undo(); };
    redo.onClick = [this] { proc.getUndoManager().redo(); };
    root.addAndMakeVisible (undo);
    root.addAndMakeVisible (redo);

    autoGainBtn.setClickingTogglesState (true);
    autoGainBtn.setName ("Auto ganancia");
    autoGainBtn.setTooltip (zeq::utf8 ("Compensar automáticamente el cambio de sonoridad estática"));
    autoGainAtt = std::make_unique<juce::ButtonParameterAttachment> (
        *p.getState().getParameter (params::autoGainId), autoGainBtn, &p.getUndoManager());
    root.addAndMakeVisible (autoGainBtn);

    globalBypassBtn.setClickingTogglesState (true);
    globalBypassBtn.setName ("Bypass global");
    globalBypassBtn.setTooltip ("Desactivar temporalmente todo el ecualizador sin clics");
    globalBypassAtt = std::make_unique<juce::ButtonParameterAttachment> (
        *p.getState().getParameter (params::globalBypassId), globalBypassBtn, &p.getUndoManager());
    root.addAndMakeVisible (globalBypassBtn);

    gainScale.attach (*p.getState().getParameter (params::gainScaleId), &p.getUndoManager());
    output.attach (*p.getState().getParameter (params::outputGainId), &p.getUndoManager());
    root.addAndMakeVisible (gainScale);
    root.addAndMakeVisible (output);

    // Barra inferior: + Banda, fichas de banda, Audición, Congelar, Fase y Atajos (?)
    addBandBtn.setTooltip ("Crear una banda nueva en una frecuencia libre (tecla N)");
    addBandBtn.onClick = [this]
    {
        proc.getUndoManager().beginNewTransaction ("Nueva banda");
        const int created = access.createSmartDefault();
        if (created >= 0)
            graph.setSelected (created);
    };
    root.addAndMakeVisible (addBandBtn);

    for (int b = 0; b < kMaxBands; ++b)
    {
        auto chip = std::make_unique<BandChip> (b);
        chip->onClick = [this, b] { graph.setSelected (b); };
        chip->onToggleBypass = [this, b]
        {
            proc.getUndoManager().beginNewTransaction();
            access.set (b, BandParam::Bypass, access.get (b, BandParam::Bypass) > 0.5f ? 0.0f : 1.0f);
        };
        chip->onContextMenu = [this, b] { showChipMenu (b); };
        root.addChildComponent (*chip);
        chips.push_back (std::move (chip));
    }

    autoListenBtn.setClickingTogglesState (true);
    autoListenBtn.setTooltip (zeq::utf8 ("Escuchar automáticamente la zona de la banda al arrastrar su nodo o su frecuencia"));
    autoListenBtn.onClick = [this] { setAutoListen (autoListenBtn.getToggleState()); };
    root.addAndMakeVisible (autoListenBtn);

    freezeBtn.setClickingTogglesState (true);
    freezeBtn.setTooltip ("Congelar la imagen actual del analizador de espectro (tecla F)");
    freezeBtn.onClick = [this] { setFrozen (freezeBtn.getToggleState()); };
    root.addAndMakeVisible (freezeBtn);

    phaseBtn.setClickingTogglesState (true);
    phaseBtn.setTooltip (zeq::utf8 ("Mostrar la respuesta de fase total (±180°, tecla P)"));
    phaseBtn.onClick = [this] { setPhaseVisible (phaseBtn.getToggleState()); };
    root.addAndMakeVisible (phaseBtn);

    darkBtn.setClickingTogglesState (true);
    darkBtn.setTooltip ("Alternar entre Modo Claro y Modo Oscuro de ZUENA (tecla M)");
    darkBtn.onClick = [this] { setDarkMode (darkBtn.getToggleState()); };
    root.addAndMakeVisible (darkBtn);

    helpBtn.setClickingTogglesState (true);
    helpBtn.setTooltip (zeq::utf8 ("Ver guía de atajos de teclado y gestos de ratón (tecla ?)"));
    helpBtn.onClick = [this] { setShortcutsVisible (helpBtn.getToggleState()); };
    root.addAndMakeVisible (helpBtn);

    sizeBtn.setName (zeq::utf8 ("Tamaño"));
    sizeBtn.setTooltip (zeq::utf8 ("Tamaño de la interfaz (se recuerda para todas las instancias)"));
    sizeBtn.onClick = [this] { showSizeMenu(); };
    root.addAndMakeVisible (sizeBtn);

    hint.setText (zeq::utf8 ("Doble clic o Ctrl+clic: crear · Arrastrar vacío: selección múltiple · Alt+clic: bypass · Rueda: Q"),
                  juce::dontSendNotification);
    hint.setFont (theme::font (11.5f));
    hint.setColour (juce::Label::textColourId, theme::muted);
    hint.setJustificationType (juce::Justification::centred);
    root.addAndMakeVisible (hint);

    const auto& tree = p.getState().state;
    setAnalyzer ((AnalyzerMode) juce::jlimit (0, 3, (int) tree.getProperty ("ui_analyzer", 3)));
    setRange ((int) tree.getProperty ("ui_range", 12), false);
    setPhaseVisible ((bool) tree.getProperty ("ui_phase", false));
    setAutoListen ((bool) tree.getProperty ("ui_autolisten", false));
    setDarkMode ((bool) tree.getProperty ("ui_dark", theme::isDark()), false);
    refreshAbButtons();

    addMouseListener (this, true);
    setWantsKeyboardFocus (true);
    setResizable (true, true);
    // Tamaño: el que eligió el usuario, o el más grande que entra en la pantalla (1360×780 no entra en
    // notebooks con escalado de Windows de 125–150 %).
    float startScale = prefs::get ("scale").getFloatValue() / 100.0f;
    if (std::find (kUiScales.begin(), kUiScales.end(), startScale) == kUiScales.end())
    {
        const auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay();
        startScale = display != nullptr ? fitScale (display->userBounds.toNearestInt()) : 1.0f;
    }
    uiScale = startScale;
    setResizeLimits (juce::roundToInt (kMinW * uiScale), juce::roundToInt (kMinH * uiScale),
                     juce::roundToInt (kMaxW * uiScale), juce::roundToInt (kMaxH * uiScale));
    setSize (juce::roundToInt (kBaseW * uiScale), juce::roundToInt (kBaseH * uiScale));
    sizeBtn.setButtonText (juce::String (juce::roundToInt (uiScale * 100.0f)) + " %");

    lastFrameMs = juce::Time::getMillisecondCounterHiRes();
    // Tope de ~60–80 fps: en monitores de 120/144/165 Hz el vblank llega 2–3 veces más seguido y cada fotograma
    // extra costaba CPU sin verse distinto. Las animaciones usan dt real, así que no cambian de velocidad.
    vblank = std::make_unique<juce::VBlankAttachment> (this, [this]
    {
        if (juce::Time::getMillisecondCounterHiRes() - lastFrameMs >= 12.0)
            frame();
    });
    startTimerHz (10);
}

ZuenaEqEditor::~ZuenaEqEditor()
{
    stopTimer();
    vblank.reset();
    tooltips.setLookAndFeel (nullptr);
    setLookAndFeel (nullptr);
}

void ZuenaEqEditor::frame()
{
    const double nowMs = juce::Time::getMillisecondCounterHiRes();
    const float dt = lastFrameMs > 0.0 ? (float) std::clamp ((nowMs - lastFrameMs) * 0.001, 0.001, 0.05) : (1.0f / 60.0f);
    lastFrameMs = nowMs;

    graph.tick (dt);

    const int selBand = card.getBand();
    card.refresh (selBand >= 0 ? proc.getEngine().dynamicGainDb (selBand) : 0.0f);

    if (card.isVisible())
    {
        const auto dst = targetCardBounds();
        if (! cardWasVisible)
        {
            animCard = dst.translated (0.0f, 8.0f);
            cardAlpha = 0.75f;
            cardWasVisible = true;
        }
        else
        {
            animCard.setX (motion::damp (animCard.getX(), dst.getX(), 20.0f, dt));
            animCard.setY (motion::damp (animCard.getY(), dst.getY(), 20.0f, dt));
            animCard.setWidth (dst.getWidth());
            animCard.setHeight (motion::damp (animCard.getHeight(), dst.getHeight(), 22.0f, dt));
            cardAlpha = motion::damp (cardAlpha, 1.0f, 20.0f, dt);
        }
        card.setAlpha (cardAlpha);
        const auto rounded = animCard.toNearestInt();
        if (card.getBounds() != rounded)
            card.setBounds (rounded);
    }
    else
    {
        cardWasVisible = false;
        cardPinnedByUser = false;
    }

    int usedCount = 0;
    bool chipsChanged = false;
    for (int b = 0; b < kMaxBands; ++b)
    {
        auto& chip = *chips[(size_t) b];
        const bool used = access.get (b, BandParam::Used) > 0.5f;
        const bool byp = access.get (b, BandParam::Bypass) > 0.5f;
        usedCount += used ? 1 : 0;
        if (chip.isVisible() != used)
        {
            chip.setVisible (used);
            chipsChanged = true;
        }
        if (chip.bypassed != byp)
        {
            chip.bypassed = byp;
            chip.repaint();
        }
        chip.setToggleState (graph.isBandSelected (b), juce::dontSendNotification);
    }
    addBandBtn.setEnabled (usedCount < kMaxBands);
    if (chipsChanged)
        layoutContent();

    undo.setEnabled (proc.getUndoManager().canUndo());
    redo.setEnabled (proc.getUndoManager().canRedo());
    // El tema es global al proceso: todas las instancias comparten modo (evita el parpadeo claro/oscuro, EB-17).
    // 1) Si el proyecto de ESTA instancia cambió ui_dark (carga de estado, A/B), se aplica.
    // 2) Si otra instancia cambió el modo global, esta lo sigue y actualiza su proyecto.
    const bool treeDark = (bool) proc.getState().state.getProperty ("ui_dark", appliedDark);
    if (treeDark != appliedDark)
        setDarkMode (treeDark, false);
    else if (theme::isDark() != appliedDark)
        setDarkMode (theme::isDark(), false);
    refreshAbButtons();

    const float targetPeak = proc.getEngine().outputPeakDbfs();
    const float nextMeter = targetPeak > meterDb ? motion::damp (meterDb, targetPeak, 30.0f, dt)
                                                 : motion::damp (meterDb, targetPeak, 8.0f, dt);
    if (std::abs (nextMeter - meterDb) > 0.05f)
    {
        meterDb = nextMeter;
        root.repaint (meterBounds.toNearestInt().expanded (2)); // coordenadas lógicas de `root`
    }
}

void ZuenaEqEditor::select (int band)
{
    const bool wasHidden = ! card.isVisible();
    if (band < 0)
        cardPinnedByUser = false;
    card.showBand (band);
    layoutCard (wasHidden);
}

juce::Rectangle<float> ZuenaEqEditor::targetCardBounds() const
{
    const auto g = graph.getBounds();
    const int w = juce::jmin (640, g.getWidth() - 24); // ficha 0.3.5: perillas grandes + columna de acciones
    const int h = card.preferredHeight();
    if (cardPinnedByUser)
    {
        const float px = juce::jlimit ((float) (g.getX() + 8), (float) juce::jmax (g.getX() + 8, g.getRight() - w - 8), pinnedCardPos.x);
        const float py = juce::jlimit ((float) (g.getY() + 8), (float) juce::jmax (g.getY() + 8, g.getBottom() - h - 8), pinnedCardPos.y);
        return { px, py, (float) w, (float) h };
    }
    const auto p = access.read (juce::jmax (0, card.getBand()));
    const double logPos = std::log (juce::jlimit (10.0, 30000.0, p.freqHz) / 10.0) / std::log (3000.0);
    const int x = logPos > 0.5 ? g.getX() + 14 : g.getRight() - w - 14;
    return { (float) x, (float) (g.getBottom() - h - 34), (float) w, (float) h };
}

void ZuenaEqEditor::layoutCard (bool snap)
{
    if (card.getBand() < 0)
        return;
    const auto dst = targetCardBounds();
    if (snap)
    {
        animCard = dst;
        cardAlpha = 1.0f;
        cardWasVisible = true;
        card.setAlpha (1.0f);
        card.setBounds (dst.toNearestInt());
    }
}

void ZuenaEqEditor::showChipMenu (int band)
{
    const auto p = access.read (band);
    juce::PopupMenu menu;
    menu.addSectionHeader ("Banda " + juce::String (band + 1));
    menu.addItem (1, "Seleccionar", true, graph.getSelected() == band);
    menu.addItem (2, "Bypass", true, p.bypass);
    menu.addItem (3, "Solo", true, p.solo);
    menu.addSeparator();
    menu.addItem (4, "Eliminar banda");
    menu.setLookAndFeel (&lnf);
    juce::Component::SafePointer<ZuenaEqEditor> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition(), [safe, band, p] (int r)
    {
        if (safe == nullptr || r == 0)
            return;
        safe->proc.getUndoManager().beginNewTransaction();
        if (r == 1)
            safe->graph.setSelected (band);
        else if (r == 2)
            safe->access.set (band, BandParam::Bypass, p.bypass ? 0.0f : 1.0f);
        else if (r == 3)
            safe->access.setSolo (band, ! p.solo);
        else if (r == 4)
        {
            safe->access.remove (band);
            if (safe->graph.getSelected() == band)
                safe->graph.setSelected (-1);
        }
    });
}

void ZuenaEqEditor::setAnalyzer (AnalyzerMode m)
{
    graph.setAnalyzerMode (m);
    proc.getState().state.setProperty ("ui_analyzer", (int) m, nullptr);
    for (int i = 0; i < (int) analyzerButtons.size(); ++i)
        analyzerButtons[(size_t) i].setToggleState (i == (int) m, juce::dontSendNotification);
}

void ZuenaEqEditor::setRange (int db, bool animate)
{
    db = db == 6 || db == 30 ? db : 12;
    graph.setRangeDb ((float) db, animate);
    proc.getState().state.setProperty ("ui_range", db, nullptr);
    for (int i = 0; i < (int) rangeButtons.size(); ++i)
        rangeButtons[(size_t) i].setToggleState (kRanges[i] == db, juce::dontSendNotification);
}

void ZuenaEqEditor::setPhaseVisible (bool show)
{
    graph.setShowPhase (show);
    proc.getState().state.setProperty ("ui_phase", show, nullptr);
    phaseBtn.setToggleState (show, juce::dontSendNotification);
}

void ZuenaEqEditor::setAutoListen (bool on)
{
    graph.setAutoListen (on);
    proc.getState().state.setProperty ("ui_autolisten", on, nullptr);
    autoListenBtn.setToggleState (on, juce::dontSendNotification);
}

void ZuenaEqEditor::setFrozen (bool freeze)
{
    graph.setFrozen (freeze);
    freezeBtn.setToggleState (freeze, juce::dontSendNotification);
}

void ZuenaEqEditor::timerCallback()
{
    if (juce::Time::getMillisecondCounterHiRes() - lastFrameMs > 150.0)
        frame();
}

void ZuenaEqEditor::setDarkMode (bool dark, bool persistGlobal)
{
    appliedDark = dark;
    theme::setDark (dark);
    proc.getState().state.setProperty ("ui_dark", dark, nullptr);
    darkBtn.setToggleState (dark, juce::dontSendNotification);
    lnf.applyTheme();
    sendLookAndFeelChange();
    hint.setColour (juce::Label::textColourId, theme::muted);
    output.refreshTheme();
    gainScale.refreshTheme();
    card.refreshTheme();
    repaint();

    if (persistGlobal)
        prefs::set ("dark", dark ? "1" : "0"); // conserva la escala y demás claves
}

void ZuenaEqEditor::setShortcutsVisible (bool show)
{
    shortcutsOverlay.setVisible (show);
    if (show)
        shortcutsOverlay.toFront (false);
    helpBtn.setToggleState (show, juce::dontSendNotification);
}

void ZuenaEqEditor::refreshAbButtons()
{
    const int slot = presets::activeSlot (proc.getState());
    slotABtn.setToggleState (slot == 0, juce::dontSendNotification);
    slotBBtn.setToggleState (slot == 1, juce::dontSendNotification);
    const auto copyLbl = slot == 0 ? zeq::utf8 ("A→B") : zeq::utf8 ("B→A");
    if (copyAbBtn.getButtonText() != copyLbl)
        copyAbBtn.setButtonText (copyLbl);
}

float ZuenaEqEditor::fitScale (juce::Rectangle<int> area) noexcept
{
    // Margen para la barra de título y los bordes de la ventana del host.
    const float maxW = (float) area.getWidth() * 0.92f, maxH = (float) area.getHeight() * 0.86f;
    for (auto it = kUiScales.rbegin(); it != kUiScales.rend(); ++it)
        if (*it <= 1.0f && (float) kBaseW * *it <= maxW && (float) kBaseH * *it <= maxH)
            return *it;
    return kUiScales.front();
}

void ZuenaEqEditor::setUiScale (float scale, bool persist)
{
    scale = juce::jlimit (kUiScales.front(), kUiScales.back(), scale);
    // Se conserva el tamaño lógico actual (lo que el usuario estiró) y cambia solo la escala.
    const float logicalW = (float) getWidth() / uiScale, logicalH = (float) getHeight() / uiScale;
    uiScale = scale;
    sizeBtn.setButtonText (juce::String (juce::roundToInt (scale * 100.0f)) + " %");
    setResizeLimits (juce::roundToInt (kMinW * scale), juce::roundToInt (kMinH * scale),
                     juce::roundToInt (kMaxW * scale), juce::roundToInt (kMaxH * scale));
    setSize (juce::roundToInt (logicalW * scale), juce::roundToInt (logicalH * scale));
    resized(); // aunque el tamaño en píxeles no cambie, la escala sí
    if (persist)
        prefs::set ("scale", juce::String (juce::roundToInt (scale * 100.0f)));
}

void ZuenaEqEditor::showSizeMenu()
{
    juce::PopupMenu menu;
    menu.addSectionHeader (zeq::utf8 ("Tamaño de la interfaz"));
    for (size_t i = 0; i < kUiScales.size(); ++i)
    {
        const int pct = juce::roundToInt (kUiScales[i] * 100.0f);
        menu.addItem ((int) i + 1, juce::String (pct) + " %", true, std::abs (kUiScales[i] - uiScale) < 0.001f);
    }
    menu.setLookAndFeel (&lnf);
    juce::Component::SafePointer<ZuenaEqEditor> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&sizeBtn), [safe] (int r)
    {
        if (safe != nullptr && r > 0 && r <= (int) kUiScales.size())
            safe->setUiScale (kUiScales[(size_t) r - 1]);
    });
}

void ZuenaEqEditor::paint (juce::Graphics& g)
{
    g.fillAll (theme::paper);
}

void ZuenaEqEditor::resized()
{
    // Todo el contenido vive en `root`, con tamaño lógico y escalado como un bloque.
    root.setTransform (juce::AffineTransform::scale (uiScale));
    root.setBounds (0, 0, (int) std::ceil ((float) getWidth() / uiScale), (int) std::ceil ((float) getHeight() / uiScale));
}

void ZuenaEqEditor::paintContent (juce::Graphics& g)
{
    g.fillAll (theme::paper);
    const auto top = root.getLocalBounds().removeFromTop (kHeader).toFloat();
    const float iriScale = theme::isDark() ? 0.55f : 1.0f;
    juce::ColourGradient light (theme::iri1.withAlpha (0.09f * iriScale), top.getX(), top.getY(),
                                theme::iri3.withAlpha (0.05f * iriScale), top.getRight(), top.getBottom(), false);
    g.setGradientFill (light);
    g.fillRect (root.getLocalBounds().toFloat());

    paintLogo (g, { (float) kMargin + 2.0f, top.getCentreY() - 14.0f, 28.0f, 28.0f });
    g.setColour (theme::ink);
    g.setFont (theme::font (15.5f, true).withExtraKerningFactor (0.22f));
    g.drawText ("ZUENA", juce::Rectangle<float> ((float) kMargin + 38.0f, top.getCentreY() - 12.0f, 82.0f, 24.0f), juce::Justification::centredLeft);
    g.setColour (theme::muted);
    g.setFont (theme::font (15.5f));
    g.drawText ("EQ", juce::Rectangle<float> ((float) kMargin + 116.0f, top.getCentreY() - 12.0f, 30.0f, 24.0f), juce::Justification::centredLeft);

    if (! meterBounds.isEmpty())
    {
        g.setColour (theme::line);
        g.fillRoundedRectangle (meterBounds, 3.0f);
        const float norm = juce::jlimit (0.0f, 1.0f, (meterDb + 60.0f) / 63.0f);
        if (norm > 0.01f)
        {
            const float fillH = meterBounds.getHeight() * norm;
            const juce::Rectangle<float> bar (meterBounds.getX(), meterBounds.getBottom() - fillH, meterBounds.getWidth(), fillH);
            g.setColour (meterDb >= 0.0f ? theme::danger : theme::ink.withAlpha (0.78f));
            g.fillRoundedRectangle (bar, 3.0f);
        }
    }
}

void ZuenaEqEditor::layoutContent()
{
    auto r = root.getLocalBounds();
    auto header = r.removeFromTop (kHeader).reduced (kMargin, 0);
    header.removeFromLeft (144);

    // Derecha: Medidor + Salida + Escala % + Bypass + Auto + Rehacer + Deshacer
    auto meterSlot = header.removeFromRight (8);
    meterBounds = meterSlot.withSizeKeepingCentre (6, 48).toFloat();
    header.removeFromRight (4);
    output.setBounds (header.removeFromRight (62).withSizeKeepingCentre (62, 76));
    header.removeFromRight (4);
    gainScale.setBounds (header.removeFromRight (62).withSizeKeepingCentre (62, 76));
    header.removeFromRight (6);
    globalBypassBtn.setBounds (header.removeFromRight (58).withSizeKeepingCentre (58, 32));
    header.removeFromRight (4);
    autoGainBtn.setBounds (header.removeFromRight (46).withSizeKeepingCentre (46, 32));
    header.removeFromRight (8);
    redo.setBounds (header.removeFromRight (68).withSizeKeepingCentre (68, 32));
    header.removeFromRight (4);
    undo.setBounds (header.removeFromRight (72).withSizeKeepingCentre (72, 32));
    header.removeFromRight (8);

    // Izquierda: Presets + A / B / A->B
    presetBox.setBounds (header.removeFromLeft (126).withSizeKeepingCentre (126, 32));
    header.removeFromLeft (6);
    slotABtn.setBounds (header.removeFromLeft (28).withSizeKeepingCentre (28, 32));
    header.removeFromLeft (3);
    slotBBtn.setBounds (header.removeFromLeft (28).withSizeKeepingCentre (28, 32));
    header.removeFromLeft (3);
    copyAbBtn.setBounds (header.removeFromLeft (42).withSizeKeepingCentre (42, 32));
    header.removeFromLeft (10);

    // Centro: Analizador + Escala dB + Modo de fase
    const int analyzerWidths[] = { 38, 38, 42, 76 };
    for (size_t i = 0; i < analyzerButtons.size(); ++i)
    {
        analyzerButtons[i].setBounds (header.removeFromLeft (analyzerWidths[i]).withSizeKeepingCentre (analyzerWidths[i], 32));
        header.removeFromLeft (3);
    }
    header.removeFromLeft (7);
    const int rangeWidths[] = { 38, 40, 40 };
    for (size_t i = 0; i < rangeButtons.size(); ++i)
    {
        rangeButtons[i].setBounds (header.removeFromLeft (rangeWidths[i]).withSizeKeepingCentre (rangeWidths[i], 32));
        header.removeFromLeft (3);
    }
    header.removeFromLeft (7);
    qualityBox.setBounds (header.removeFromLeft (118).withSizeKeepingCentre (118, 32));

    // Barra inferior: + Banda y fichas a la izquierda; Audición, Congelar, Fase, Oscuro y ? a la derecha
    auto footer = r.removeFromBottom (kFooter).reduced (kMargin, 8);
    helpBtn.setBounds (footer.removeFromRight (32));
    footer.removeFromRight (6);
    sizeBtn.setBounds (footer.removeFromRight (62));
    footer.removeFromRight (6);
    darkBtn.setBounds (footer.removeFromRight (60));
    footer.removeFromRight (6);
    phaseBtn.setBounds (footer.removeFromRight (52));
    footer.removeFromRight (6);
    freezeBtn.setBounds (footer.removeFromRight (74));
    footer.removeFromRight (6);
    autoListenBtn.setBounds (footer.removeFromRight (74));
    footer.removeFromRight (10);

    addBandBtn.setBounds (footer.removeFromLeft (72));
    footer.removeFromLeft (8);

    int count = 0;
    for (auto& chip : chips)
        count += chip->isVisible() ? 1 : 0;
    constexpr int gap = 5;
    const int chipW = count > 0 ? juce::jlimit (26, 44, (footer.getWidth() + gap) / count - gap) : 44;
    int x = footer.getX();
    for (auto& chip : chips)
        if (chip->isVisible())
        {
            chip->setBounds (x, footer.getY(), chipW, footer.getHeight());
            x += chipW + gap;
        }

    const auto hintArea = footer.withLeft (x + 10);
    const int hintWidth = juce::GlyphArrangement::getStringWidthInt (hint.getFont(), hint.getText()) + 8;
    hint.setVisible (hintArea.getWidth() >= hintWidth);
    hint.setBounds (hintArea);

    const auto graphRect = r.reduced (kMargin, 0);
    graph.setBounds (graphRect);
    shortcutsOverlay.setBounds (graphRect);
    layoutCard (! cardWasVisible);
}

void ZuenaEqEditor::mouseDown (const juce::MouseEvent&)
{
    proc.getUndoManager().beginNewTransaction();
}

bool ZuenaEqEditor::keyPressed (const juce::KeyPress& k)
{
    const bool cmd = k.getModifiers().isCommandDown();
    const bool shift = k.getModifiers().isShiftDown();
    const int code = k.getKeyCode();
    const auto ch = k.getTextCharacter();

    if (cmd && (code == 'Z' || code == 'z') && shift)
        return proc.getUndoManager().redo();
    if (cmd && (code == 'Z' || code == 'z'))
        return proc.getUndoManager().undo();
    if (cmd && (code == 'Y' || code == 'y'))
        return proc.getUndoManager().redo();
    if (cmd && (code == 'A' || code == 'a'))
    {
        graph.selectAllBands();
        return true;
    }

    if (code == juce::KeyPress::escapeKey)
    {
        if (shortcutsOverlay.isVisible())
            setShortcutsVisible (false);
        else
            graph.setSelected (-1);
        return true;
    }
    if (ch == '?' || (! cmd && (code == 'H' || code == 'h')))
    {
        setShortcutsVisible (! shortcutsOverlay.isVisible());
        return true;
    }
    if (! cmd && (code == 'F' || code == 'f'))
    {
        setFrozen (! graph.isFrozen());
        return true;
    }
    if (! cmd && (code == 'P' || code == 'p'))
    {
        setPhaseVisible (! graph.getShowPhase());
        return true;
    }
    if (! cmd && (code == 'M' || code == 'm'))
    {
        setDarkMode (! theme::isDark());
        return true;
    }
    if (! cmd && (code == 'N' || code == 'n' || ch == '+'))
    {
        addBandBtn.triggerClick();
        return true;
    }
    if (code == juce::KeyPress::tabKey || ch == '[' || ch == ']')
    {
        const int dir = (shift || ch == '[') ? -1 : 1;
        const int next = access.stepBand (graph.getSelected(), dir);
        if (next >= 0)
            graph.setSelected (next);
        return true;
    }
    if (! cmd && ch >= '1' && ch <= '9')
    {
        const int idx = (int) (ch - '1');
        if (access.get (idx, BandParam::Used) > 0.5f)
            graph.setSelected (idx);
        return true;
    }

    if ((code == juce::KeyPress::deleteKey || code == juce::KeyPress::backspaceKey) && graph.getSelected() >= 0)
    {
        proc.getUndoManager().beginNewTransaction ("Eliminar bandas");
        graph.deleteSelection();
        return true;
    }

    const int sel = graph.getSelected();
    if (sel >= 0 && ! cmd)
    {
        if (code == 'B' || code == 'b')
        {
            proc.getUndoManager().beginNewTransaction();
            graph.toggleBypassSelection();
            return true;
        }
        if (code == 'S' || code == 's')
        {
            proc.getUndoManager().beginNewTransaction();
            access.setSolo (sel, access.get (sel, BandParam::Solo) < 0.5f);
            return true;
        }
        if (code == 'D' || code == 'd')
        {
            proc.getUndoManager().beginNewTransaction();
            graph.toggleDynamicSelection();
            return true;
        }
        if (code == 'T' || code == 't')
        {
            proc.getUndoManager().beginNewTransaction();
            const int cur = (int) access.read (sel).type;
            const int next = (cur + (shift ? -1 : 1) + (int) FilterType::Count) % (int) FilterType::Count;
            access.set (sel, BandParam::Type, (float) next);
            return true;
        }
        if (code == 'I' || code == 'i')
        {
            proc.getUndoManager().beginNewTransaction();
            const auto p = access.read (sel);
            if (hasGain (p.type))
                access.set (sel, BandParam::Gain, (float) -p.gainDb);
            return true;
        }
        if (code == juce::KeyPress::leftKey || code == juce::KeyPress::rightKey)
        {
            proc.getUndoManager().beginNewTransaction();
            const float oct = (code == juce::KeyPress::rightKey ? 1.0f : -1.0f) * (shift ? (1.0f / 120.0f) : (1.0f / 12.0f));
            graph.nudgeSelection (oct, 0.0f, 1.0f);
            return true;
        }
        if (code == juce::KeyPress::upKey || code == juce::KeyPress::downKey)
        {
            proc.getUndoManager().beginNewTransaction();
            const float db = (code == juce::KeyPress::upKey ? 1.0f : -1.0f) * (shift ? 0.1f : 0.5f);
            graph.nudgeSelection (0.0f, db, 1.0f);
            return true;
        }
    }
    return false;
}
} // namespace zeq::ui
