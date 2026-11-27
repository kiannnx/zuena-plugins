#include "BandCard.h"
#include "Format.h"

namespace zeq::ui
{
namespace
{
// Layout de la ficha (coordenadas lógicas; la escala de la interfaz la aplica el contenedor raíz).
constexpr int kInset = 6;        // margen para la sombra
constexpr int kPad = 18;         // relleno interior
constexpr int kHeader = 34;      // icono + título + navegación
constexpr int kSelectors = 32;   // Tipo / Pendiente / Canales
constexpr int kMainKnob = 118;   // perillas grandes (dial + valor + nombre)
constexpr int kDynHeader = 24;   // título de Dinámica + medidor
constexpr int kDynKnob = 92;     // perillas de dinámica
constexpr int kSideCol = 168;    // columna de botones a la derecha
constexpr int kGap = 12;
} // namespace

void IconMiniButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    const auto r = getLocalBounds().toFloat().reduced (1.5f);
    if (highlighted || down)
    {
        g.setColour (down ? theme::line : (theme::isDark() ? theme::raised.brighter (0.10f) : theme::surface.darker (0.03f)));
        g.fillRoundedRectangle (r, 8.0f);
    }
    const float cx = r.getCentreX(), cy = r.getCentreY();
    juce::Path p;
    if (glyph == Glyph::Prev)
    {
        p.startNewSubPath (cx + 2.2f, cy - 4.6f);
        p.lineTo (cx - 2.6f, cy);
        p.lineTo (cx + 2.2f, cy + 4.6f);
    }
    else if (glyph == Glyph::Next)
    {
        p.startNewSubPath (cx - 2.2f, cy - 4.6f);
        p.lineTo (cx + 2.6f, cy);
        p.lineTo (cx - 2.2f, cy + 4.6f);
    }
    else
    {
        p.startNewSubPath (cx - 4.0f, cy - 4.0f);
        p.lineTo (cx + 4.0f, cy + 4.0f);
        p.startNewSubPath (cx + 4.0f, cy - 4.0f);
        p.lineTo (cx - 4.0f, cy + 4.0f);
    }
    g.setColour (highlighted ? theme::ink : theme::muted);
    g.strokePath (p, juce::PathStrokeType (1.7f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

BandCard::BandCard (BandAccess& a, juce::UndoManager& undo) : access (a), undoManager (undo)
{
    title.setFont (theme::font (16.0f, true));
    title.setColour (juce::Label::textColourId, theme::ink);
    title.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (title);

    noteBadge.setFont (theme::font (12.0f, true));
    noteBadge.setColour (juce::Label::textColourId, theme::muted);
    noteBadge.setJustificationType (juce::Justification::centredLeft);
    noteBadge.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (noteBadge);

    prevBtn.onClick = [this] { if (onStepBand) onStepBand (-1); };
    nextBtn.onClick = [this] { if (onStepBand) onStepBand (1); };
    closeBtn.onClick = [this] { if (onClosed) onClosed(); };
    for (auto* b : { &prevBtn, &nextBtn, &closeBtn })
        addAndMakeVisible (b);

    type.addItemList (params::typeNames(), 1);
    slope.addItemList (params::slopeNames(), 1);
    placement.addItemList (params::placementNames(), 1);
    type.setTooltip ("Tipo de filtro (tecla T)");
    slope.setTooltip ("Pendiente del corte (Alt + rueda)");
    placement.setTooltip ("Canales que procesa la banda");
    type.setName ("Tipo");
    slope.setName ("Pendiente");
    placement.setName (zeq::utf8 ("Estéreo"));
    for (auto* c : { &type, &slope, &placement })
        addAndMakeVisible (c);

    for (auto* b : { &bypass, &dynamic })
    {
        b->setClickingTogglesState (true);
        addAndMakeVisible (b);
    }
    bypass.setTooltip ("Activar/desactivar banda (B / Alt+clic)");
    dynamic.setTooltip (zeq::utf8 ("Ecualización dinámica por umbral (D)"));

    solo.setClickingTogglesState (false);
    solo.setTooltip ("Escuchar solo la zona de esta banda (S)");
    solo.onClick = [this] { if (band >= 0) access.setSolo (band, access.get (band, BandParam::Solo) < 0.5f); };
    addAndMakeVisible (solo);

    invertGain.setTooltip ("Invertir el signo de la ganancia (buscar resonancia realzando e invertir a corte)");
    invertGain.onClick = [this]
    {
        if (band < 0)
            return;
        undoManager.beginNewTransaction();
        const float g = access.get (band, BandParam::Gain);
        access.set (band, BandParam::Gain, std::abs (g) > 0.01f ? -g : -3.0f);
    };
    addAndMakeVisible (invertGain);

    remove.getProperties().set ("danger", true);
    remove.setTooltip ("Eliminar banda (Supr)");
    remove.onClick = [this]
    {
        if (band < 0)
            return;
        access.remove (band);
        if (onDeleted)
            onDeleted();
    };
    addAndMakeVisible (remove);

    freq.onGestureStart = [this]
    {
        if (band >= 0 && isAutoListenOn && isAutoListenOn())
        {
            freqPrevSolo = access.get (band, BandParam::Solo) > 0.5f;
            if (! freqPrevSolo)
            {
                freqTempSolo = true;
                access.setSolo (band, true);
            }
        }
    };
    freq.onGestureEnd = [this]
    {
        if (freqTempSolo && band >= 0)
            access.setSolo (band, freqPrevSolo);
        freqTempSolo = false;
    };

    for (auto* k : { &freq, &gain, &q })
        k->setStyle (Knob::Style::Large);
    for (auto* k : { &threshold, &range, &ratio, &attack, &release })
        k->setStyle (Knob::Style::Medium);
    for (auto* k : { &freq, &gain, &q, &threshold, &range, &ratio, &attack, &release })
        addAndMakeVisible (k);

    setVisible (false);
}

void BandCard::showBand (int newBand)
{
    if (freqTempSolo && band >= 0)
    {
        access.setSolo (band, freqPrevSolo);
        freqTempSolo = false;
    }

    typeAtt.reset();
    slopeAtt.reset();
    placeAtt.reset();
    bypassAtt.reset();
    dynAtt.reset();
    for (auto* k : { &freq, &gain, &q, &threshold, &range, &ratio, &attack, &release })
        k->detach();

    band = newBand;
    if (band < 0)
    {
        setVisible (false);
        return;
    }

    auto* undo = &undoManager;
    title.setText ("Banda " + juce::String (band + 1), juce::dontSendNotification);
    typeAtt = std::make_unique<juce::ComboBoxParameterAttachment> (access.param (band, BandParam::Type), type, undo);
    slopeAtt = std::make_unique<juce::ComboBoxParameterAttachment> (access.param (band, BandParam::Slope), slope, undo);
    placeAtt = std::make_unique<juce::ComboBoxParameterAttachment> (access.param (band, BandParam::Placement), placement, undo);
    bypassAtt = std::make_unique<juce::ButtonParameterAttachment> (access.param (band, BandParam::Bypass), bypass, undo);
    dynAtt = std::make_unique<juce::ButtonParameterAttachment> (access.param (band, BandParam::DynOn), dynamic, undo);

    freq.attach (access.param (band, BandParam::Freq), undo);
    gain.attach (access.param (band, BandParam::Gain), undo);
    q.attach (access.param (band, BandParam::Q), undo);
    threshold.attach (access.param (band, BandParam::Threshold), undo);
    range.attach (access.param (band, BandParam::Range), undo);
    ratio.attach (access.param (band, BandParam::Ratio), undo);
    attack.attach (access.param (band, BandParam::Attack), undo);
    release.attach (access.param (band, BandParam::Release), undo);

    // El arco de cada perilla toma el color de la banda: la ficha y su nodo en el gráfico se leen como una sola cosa.
    const auto accent = theme::bandColour (band);
    for (auto* k : { &freq, &gain, &q, &threshold, &range, &ratio, &attack, &release })
        k->setAccent (accent);

    shownType = FilterType::Count;
    refresh (0.0f);
    setVisible (true);
}

void BandCard::refresh (float liveDynDb)
{
    if (band < 0)
        return;
    const auto p = access.read (band);
    solo.setToggleState (p.solo, juce::dontSendNotification);

    const juce::String badge = format::note (p.freqHz);
    if (noteBadge.getText() != badge)
        noteBadge.setText (badge, juce::dontSendNotification);

    const bool dynChanged = std::abs (liveDynDb - currentDynDb) > 0.05f;
    currentDynDb = liveDynDb;

    if (p.type != shownType || p.slopeIndex != shownSlope || p.dynOn != shownDyn)
    {
        shownType = p.type;
        shownSlope = p.slopeIndex;
        shownDyn = p.dynOn;
        updateVisibility();
        resized();
        repaint();
    }
    else if (dynChanged && shownDyn && hasGain (shownType))
    {
        repaint (dynHeaderBounds.expanded (4));
    }
}

void BandCard::updateVisibility()
{
    const bool g = hasGain (shownType);
    const bool cut = hasSlope (shownType);
    const bool usesQ = usesBandQ (shownType, shownSlope);
    const bool dyn = g && shownDyn;
    gain.setVisible (g);
    q.setVisible (usesQ);
    slope.setVisible (cut);
    dynamic.setVisible (g);
    invertGain.setVisible (g);
    for (auto* k : { &threshold, &range, &ratio, &attack, &release })
        k->setVisible (dyn);
}

int BandCard::preferredHeight() const
{
    const bool dyn = hasGain (shownType) && shownDyn;
    const int body = kInset + kPad + kHeader + kGap + kSelectors + kGap + kMainKnob + kPad + kInset;
    return body + (dyn ? kGap + kDynHeader + kDynKnob : 0);
}

void BandCard::refreshTheme()
{
    title.setColour (juce::Label::textColourId, theme::ink);
    noteBadge.setColour (juce::Label::textColourId, theme::muted);
    for (auto* k : { &freq, &gain, &q, &threshold, &range, &ratio, &attack, &release })
        k->refreshTheme();
    if (band >= 0)
        for (auto* k : { &freq, &gain, &q, &threshold, &range, &ratio, &attack, &release })
            k->setAccent (theme::bandColour (band));
    repaint();
}

void BandCard::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat().reduced ((float) kInset);
    juce::DropShadow (theme::shadow, 24, { 0, 10 }).drawForRectangle (g, r.toNearestInt());
    g.setColour (theme::raised.withAlpha (0.98f));
    g.fillRoundedRectangle (r, 20.0f);
    g.setColour (theme::line);
    g.drawRoundedRectangle (r, 20.0f, 1.0f);

    if (band < 0)
        return;
    const auto col = theme::bandColour (band);

    // Franja superior con el color de la banda: identifica la ficha a simple vista.
    {
        juce::Graphics::ScopedSaveState s (g);
        juce::Path clip;
        clip.addRoundedRectangle (r, 20.0f);
        g.reduceClipRegion (clip);
        g.setColour (col.withAlpha (theme::isDark() ? 0.85f : 0.9f));
        g.fillRect (r.withHeight (3.0f));
    }

    // Icono del tipo de filtro (clic: siguiente tipo).
    g.setColour (col.withAlpha (theme::isDark() ? 0.22f : 0.15f));
    g.fillRoundedRectangle (iconBounds, 9.0f);
    if (shownType < FilterType::Count)
        theme::drawFilterIcon (g, shownType, iconBounds.reduced (5.0f), col, 1.8f);

    // Separador y sección de dinámica con medidor en vivo de la ganancia aplicada.
    if (dividerY > 0.0f)
    {
        g.setColour (theme::line);
        g.drawHorizontalLine ((int) dividerY, r.getX() + (float) kPad, r.getRight() - (float) kPad);

        g.setColour (theme::muted);
        g.setFont (theme::font (11.0f, true).withExtraKerningFactor (0.12f));
        g.drawText (zeq::utf8 ("DINÁMICA"), dynHeaderBounds.withWidth (110), juce::Justification::centredLeft);

        if (! dynMeterBounds.isEmpty())
        {
            const auto m = dynMeterBounds;
            g.setColour (theme::line);
            g.fillRoundedRectangle (m, m.getHeight() * 0.5f);
            // Escala ±18 dB centrada en 0: a la izquierda recorta, a la derecha realza.
            const float span = 18.0f;
            const float norm = juce::jlimit (-1.0f, 1.0f, currentDynDb / span);
            const float cx = m.getCentreX();
            const float w = std::abs (norm) * m.getWidth() * 0.5f;
            if (w > 0.5f)
            {
                const auto bar = norm < 0.0f ? juce::Rectangle<float> (cx - w, m.getY(), w, m.getHeight())
                                             : juce::Rectangle<float> (cx, m.getY(), w, m.getHeight());
                g.setColour (col);
                g.fillRoundedRectangle (bar, m.getHeight() * 0.5f);
            }
            g.setColour (theme::muted.withAlpha (0.6f));
            g.fillRect (juce::Rectangle<float> (1.0f, m.getHeight() + 4.0f).withCentre ({ cx, m.getCentreY() }));

            g.setColour (std::abs (currentDynDb) > 0.05f ? theme::ink : theme::muted);
            g.setFont (theme::font (12.0f, true));
            g.drawText (zeq::utf8 ("Δ ") + format::gain (currentDynDb),
                        juce::Rectangle<float> (m.getRight() + 8.0f, m.getY() - 6.0f, 78.0f, m.getHeight() + 12.0f),
                        juce::Justification::centredLeft);
        }
    }
}

void BandCard::resized()
{
    auto r = getLocalBounds().reduced (kInset).reduced (kPad);

    // Encabezado: icono · Banda N · nota  ……  ‹ ›  ×
    auto header = r.removeFromTop (kHeader);
    iconBounds = header.removeFromLeft (kHeader).toFloat();
    header.removeFromLeft (10);
    closeBtn.setBounds (header.removeFromRight (30).withSizeKeepingCentre (30, 30));
    header.removeFromRight (8);
    nextBtn.setBounds (header.removeFromRight (28).withSizeKeepingCentre (28, 30));
    prevBtn.setBounds (header.removeFromRight (28).withSizeKeepingCentre (28, 30));
    header.removeFromRight (8);
    const int titleW = juce::GlyphArrangement::getStringWidthInt (title.getFont(), title.getText()) + 10;
    title.setBounds (header.removeFromLeft (titleW));
    noteBadge.setBounds (header);

    r.removeFromTop (kGap);

    // Columna derecha de acciones; el resto para selectores y perillas.
    auto body = r.removeFromTop (kSelectors + kGap + kMainKnob);
    auto side = body.removeFromRight (kSideCol);
    body.removeFromRight (kGap + 4);

    // Selectores a lo ancho de la columna principal.
    auto selectors = body.removeFromTop (kSelectors);
    std::vector<juce::ComboBox*> combos { &type };
    if (slope.isVisible())
        combos.push_back (&slope);
    combos.push_back (&placement);
    const int cw = (selectors.getWidth() - 8 * ((int) combos.size() - 1)) / (int) combos.size();
    for (auto* c : combos)
    {
        c->setBounds (selectors.removeFromLeft (cw));
        selectors.removeFromLeft (8);
    }

    body.removeFromTop (kGap);

    // Perillas principales repartidas en partes iguales.
    std::vector<Knob*> mains;
    for (auto* k : { &freq, &gain, &q })
        if (k->isVisible())
            mains.push_back (k);
    const int kw = body.getWidth() / 3;
    for (auto* k : mains)
        k->setBounds (body.removeFromLeft (kw));

    // Acciones: 2 × 2 y Eliminar abajo, separado.
    const int bh = 32, bgap = 8;
    const int half = (side.getWidth() - bgap) / 2;
    auto row1 = side.removeFromTop (bh);
    bypass.setBounds (row1.removeFromLeft (half));
    row1.removeFromLeft (bgap);
    solo.setBounds (row1);
    side.removeFromTop (bgap);
    auto row2 = side.removeFromTop (bh);
    if (dynamic.isVisible())
    {
        dynamic.setBounds (row2.removeFromLeft (half));
        row2.removeFromLeft (bgap);
        invertGain.setBounds (row2);
    }
    remove.setBounds (side.removeFromBottom (bh));

    // Sección de dinámica.
    dividerY = -1.0f;
    dynMeterBounds = {};
    dynHeaderBounds = {};
    if (threshold.isVisible())
    {
        r.removeFromTop (kGap);
        dividerY = (float) r.getY() - (float) kGap * 0.5f;
        auto dynHeader = r.removeFromTop (kDynHeader);
        dynHeaderBounds = dynHeader;
        dynMeterBounds = dynHeader.withTrimmedLeft (110).withTrimmedRight (90).toFloat().withSizeKeepingCentre ((float) (dynHeader.getWidth() - 200), 6.0f);
        auto dynRow = r.removeFromTop (kDynKnob);
        const int dw = dynRow.getWidth() / 5;
        for (auto* k : { &threshold, &range, &ratio, &attack, &release })
            k->setBounds (dynRow.removeFromLeft (dw));
    }
}

void BandCard::mouseDown (const juce::MouseEvent& e)
{
    // Clic sobre el icono de filtro cicla al siguiente tipo; arrastrar el fondo mueve la ficha.
    if (band >= 0 && iconBounds.contains (e.position))
    {
        undoManager.beginNewTransaction();
        const int cur = (int) access.read (band).type;
        const int dir = e.mods.isPopupMenu() || e.mods.isShiftDown() ? -1 : 1;
        const int next = (cur + dir + (int) FilterType::Count) % (int) FilterType::Count;
        access.set (band, BandParam::Type, (float) next);
        return;
    }
    draggingCard = true;
    cardDragStartOffset = e.getEventRelativeTo (getParentComponent()).getPosition() - getPosition();
}

void BandCard::mouseDrag (const juce::MouseEvent& e)
{
    if (! draggingCard || getParentComponent() == nullptr)
        return;
    const auto parentPos = e.getEventRelativeTo (getParentComponent()).getPosition();
    if (onUserMoved)
        onUserMoved (parentPos - cardDragStartOffset);
}

void BandCard::mouseUp (const juce::MouseEvent&)
{
    draggingCard = false;
}
} // namespace zeq::ui
