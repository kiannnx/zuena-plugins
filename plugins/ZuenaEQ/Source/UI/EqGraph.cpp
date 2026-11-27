#include "EqGraph.h"
#include "../State/Text.h"

#include "../Core/FilterDesign.h"
#include "Format.h"

namespace zeq::ui
{
namespace
{
constexpr double kMinHz = 10.0, kMaxHz = 30000.0;
constexpr float kSpecFloor = -90.0f, kSpecTop = 6.0f;
constexpr float kNodeRadius = 9.0f;

bool usesQ (const BandParams& p)
{
    return usesBandQ (p.type, p.slopeIndex);
}
} // namespace

EqGraph::EqGraph (ZuenaEqProcessor& p, BandAccess& a) : proc (p), access (a)
{
    setOpaque (false);
    setRepaintsOnMouseActivity (false);
    for (auto& sp : nodeAppear)
        sp.snap (0.0f);
}

double EqGraph::baseSampleRate() const
{
    const double sr = proc.getSampleRate();
    return sr > 0.0 ? sr : 48000.0;
}

double EqGraph::effectiveSampleRate() const
{
    const double sr = proc.getEngine().effectiveSampleRate();
    return sr > 0.0 ? sr : 48000.0;
}

double EqGraph::currentGainScale() const
{
    if (auto* v = proc.getState().getRawParameterValue (params::gainScaleId))
        return std::clamp ((double) v->load (std::memory_order_relaxed) * 0.01, 0.0, 2.0);
    return 1.0;
}

void EqGraph::setRangeDb (float r, bool animate)
{
    targetRangeDb = r;
    if (! animate)
        displayedRangeDb = r;
    repaint();
}

void EqGraph::setFrozen (bool freeze)
{
    spectrumFrozen = freeze;
    preSpectrum.setFrozen (freeze);
    postSpectrum.setFrozen (freeze);
    repaint();
}

bool EqGraph::isBandSelected (int band) const noexcept
{
    return band >= 0 && band < kMaxBands && (band == selected || multiSelected[(size_t) band]);
}

int EqGraph::selectedCount() const noexcept
{
    int c = 0;
    for (int b = 0; b < kMaxBands; ++b)
        if (snapshot[(size_t) b].used && isBandSelected (b))
            ++c;
    return c;
}

void EqGraph::setSelected (int band)
{
    multiSelected.fill (false);
    if (band >= 0 && band < kMaxBands)
        multiSelected[(size_t) band] = true;
    if (band == selected)
    {
        repaint();
        return;
    }
    selected = band;
    if (onSelectionChanged)
        onSelectionChanged (band);
    repaint();
}

void EqGraph::selectAllBands()
{
    int first = -1;
    multiSelected.fill (false);
    for (int b = 0; b < kMaxBands; ++b)
        if (snapshot[(size_t) b].used)
        {
            multiSelected[(size_t) b] = true;
            if (first < 0)
                first = b;
        }
    if (first != selected)
    {
        selected = first;
        if (onSelectionChanged)
            onSelectionChanged (selected);
    }
    repaint();
}

void EqGraph::nudgeSelection (float freqOctaves, float gainDeltaDb, float qFactor)
{
    for (int b = 0; b < kMaxBands; ++b)
    {
        if (! snapshot[(size_t) b].used || ! isBandSelected (b))
            continue;
        const auto p = access.read (b);
        if (std::abs (freqOctaves) > 1.0e-6f)
            access.set (b, BandParam::Freq, (float) juce::jlimit (kMinHz, kMaxHz, p.freqHz * std::pow (2.0, (double) freqOctaves)));
        if (std::abs (gainDeltaDb) > 1.0e-6f)
        {
            if (hasGain (p.type))
                access.set (b, BandParam::Gain, (float) juce::jlimit (-kMaxGainDb, kMaxGainDb, p.gainDb + (double) gainDeltaDb));
            else if (usesQ (p))
                access.set (b, BandParam::Q, (float) juce::jlimit (kMinQ, kMaxQ, p.q * std::pow (2.0, (double) gainDeltaDb * 0.25)));
        }
        if (std::abs (qFactor - 1.0f) > 1.0e-5f && usesQ (p))
            access.set (b, BandParam::Q, (float) juce::jlimit (kMinQ, kMaxQ, p.q * (double) qFactor));
    }
}

void EqGraph::toggleBypassSelection()
{
    if (selected < 0)
        return;
    const float target = snapshot[(size_t) selected].bypass ? 0.0f : 1.0f;
    for (int b = 0; b < kMaxBands; ++b)
        if (snapshot[(size_t) b].used && isBandSelected (b))
            access.set (b, BandParam::Bypass, target);
}

void EqGraph::toggleDynamicSelection()
{
    if (selected < 0)
        return;
    const float target = snapshot[(size_t) selected].dynOn ? 0.0f : 1.0f;
    for (int b = 0; b < kMaxBands; ++b)
        if (snapshot[(size_t) b].used && isBandSelected (b) && hasGain (snapshot[(size_t) b].type))
            access.set (b, BandParam::DynOn, target);
}

void EqGraph::deleteSelection()
{
    for (int b = 0; b < kMaxBands; ++b)
        if (snapshot[(size_t) b].used && isBandSelected (b))
            access.remove (b);
    setSelected (-1);
}

juce::Rectangle<float> EqGraph::plotArea() const { return getLocalBounds().toFloat().reduced (1.0f); }

float EqGraph::xForFreq (double hz) const
{
    const auto a = plotArea();
    return a.getX() + a.getWidth() * (float) (std::log (hz / kMinHz) / std::log (kMaxHz / kMinHz));
}

double EqGraph::freqForX (float x) const
{
    const auto a = plotArea();
    const double n = juce::jlimit (0.0, 1.0, (double) ((x - a.getX()) / a.getWidth()));
    return kMinHz * std::pow (kMaxHz / kMinHz, n);
}

float EqGraph::yForDb (double db) const
{
    const auto a = plotArea();
    return a.getCentreY() - (float) (db / displayedRangeDb) * a.getHeight() * 0.44f;
}

double EqGraph::dbForY (float y) const
{
    const auto a = plotArea();
    return (double) ((a.getCentreY() - y) / (a.getHeight() * 0.44f)) * displayedRangeDb;
}

float EqGraph::yForPhase (double deg) const
{
    const auto a = plotArea();
    return a.getCentreY() - (float) (deg / 180.0) * a.getHeight() * 0.40f;
}

float EqGraph::yForSpectrum (float dbfs) const
{
    const auto a = plotArea();
    const float n = (juce::jlimit (kSpecFloor, kSpecTop, dbfs) - kSpecFloor) / (kSpecTop - kSpecFloor);
    return a.getBottom() - n * a.getHeight();
}

juce::Point<float> EqGraph::nodePosition (int, const BandParams& p) const
{
    const double scale = currentGainScale();
    const float y = hasGain (p.type) ? yForDb (juce::jlimit ((double) -displayedRangeDb, (double) displayedRangeDb, p.gainDb * scale)) : yForDb (0.0);
    return { xForFreq (juce::jlimit (kMinHz, kMaxHz, p.freqHz)), y };
}

juce::Point<float> EqGraph::dynRangePosition (const BandParams& p) const
{
    const double scale = currentGainScale();
    const float y = yForDb (juce::jlimit ((double) -displayedRangeDb, (double) displayedRangeDb, (p.gainDb + p.rangeDb) * scale));
    return { xForFreq (juce::jlimit (kMinHz, kMaxHz, p.freqHz)), y };
}

int EqGraph::nodeAt (juce::Point<float> pos) const
{
    int best = -1;
    float bestDist = kNodeRadius + 6.0f;
    for (int b = 0; b < kMaxBands; ++b)
    {
        if (! snapshot[(size_t) b].used)
            continue;
        const float d = nodePosition (b, snapshot[(size_t) b]).getDistanceFrom (pos);
        if (d < bestDist)
        {
            bestDist = d;
            best = b;
        }
    }
    return best;
}

int EqGraph::dynHandleAt (juce::Point<float> pos) const
{
    int best = -1;
    float bestDist = 9.5f;
    for (int b = 0; b < kMaxBands; ++b)
    {
        const auto& p = snapshot[(size_t) b];
        if (! p.used || p.bypass || ! p.dynOn || ! hasGain (p.type))
            continue;
        const auto mainPos = nodePosition (b, p);
        const auto rangePos = dynRangePosition (p);
        if (mainPos.getDistanceFrom (rangePos) < 13.0f)
            continue;
        const float d = rangePos.getDistanceFrom (pos);
        if (d < bestDist)
        {
            bestDist = d;
            best = b;
        }
    }
    return best;
}

void EqGraph::tick (float dt)
{
    // Cualquier cambio visible marca `changed`; si nada cambió no se redibuja (con el analizador apagado o
    // congelado y sin tocar nada, el gráfico no cuesta nada).
    bool changed = false;
    auto track = [&changed] (float before, float after) { changed = changed || std::abs (after - before) > 1.0e-3f; };

    const float prevRange = displayedRangeDb, prevCursorAlpha = cursorAlpha, prevGhost = ghostAlpha;
    displayedRangeDb = motion::damp (displayedRangeDb, targetRangeDb, 14.0f, dt);
    cursorAlpha = motion::damp (cursorAlpha, mouseInside ? 1.0f : 0.0f, 16.0f, dt);
    const bool wantGhost = mouseInside && hovered < 0 && hoveredRangeBand < 0 && drag.band < 0 && ! lassoActive;
    ghostAlpha = motion::damp (ghostAlpha, wantGhost ? 1.0f : 0.0f, 14.0f, dt);
    track (prevRange, displayedRangeDb);
    track (prevCursorAlpha, cursorAlpha);
    track (prevGhost, ghostAlpha);
    if (mouseInside != lastMouseInside || ((mouseInside || cursorAlpha > 0.001f) && cursorPos != lastCursor))
        changed = true;
    lastMouseInside = mouseInside;
    lastCursor = cursorPos;

    const double gScale = currentGainScale();
    const bool gBypass = proc.getState().getRawParameterValue (params::globalBypassId)->load (std::memory_order_relaxed) > 0.5f;
    const double sr = effectiveSampleRate();
    if (gScale != lastGainScale || gBypass != lastGlobalBypass || sr != lastSampleRate)
        changed = true;
    lastGainScale = gScale;
    lastGlobalBypass = gBypass;
    lastSampleRate = sr;

    for (int b = 0; b < kMaxBands; ++b)
    {
        const auto p = access.read (b);
        snapshot[(size_t) b] = p;
        if (! (p == lastTickParams[(size_t) b]))
            changed = true;
        lastTickParams[(size_t) b] = p;
        dynSnapshot[(size_t) b] = proc.getEngine().dynamicGainDb (b);
        const float prevDyn = dynVisual[(size_t) b], prevAppear = nodeAppear[(size_t) b].value, prevEmph = nodeEmphasis[(size_t) b];
        dynVisual[(size_t) b] = motion::damp (dynVisual[(size_t) b], dynSnapshot[(size_t) b], 24.0f, dt);

        if (firstTick)
        {
            nodeAppear[(size_t) b].snap (p.used ? 1.0f : 0.0f);
        }
        else if (p.used && ! wasUsed[(size_t) b])
        {
            nodeAppear[(size_t) b].value = 0.25f;
            nodeAppear[(size_t) b].velocity = 4.5f;
            nodeAppear[(size_t) b].target = 1.0f;
        }
        else if (! p.used)
        {
            nodeAppear[(size_t) b].snap (0.0f);
            multiSelected[(size_t) b] = false;
        }
        nodeAppear[(size_t) b].step (dt);
        wasUsed[(size_t) b] = p.used;

        const float emphTarget = (isBandSelected (b) || b == hovered) ? 1.0f : 0.0f;
        nodeEmphasis[(size_t) b] = motion::damp (nodeEmphasis[(size_t) b], emphTarget, 18.0f, dt);
        if (p.used || prevAppear > 0.0f)
        {
            track (prevDyn, dynVisual[(size_t) b]);
            track (prevAppear, nodeAppear[(size_t) b].value);
            track (prevEmph, nodeEmphasis[(size_t) b]);
        }
    }
    firstTick = false;

    const double baseSr = baseSampleRate();
    preSpectrum.setSampleRate (baseSr);
    postSpectrum.setSampleRate (baseSr);
    // Las colas se consumen siempre (no se acumulan); el espectro nuevo solo redibuja si se está mostrando.
    const bool newPre = preSpectrum.update (proc.getEngine().preTap());
    const bool newPost = postSpectrum.update (proc.getEngine().postTap());
    if ((newPre && (analyzerMode == AnalyzerMode::Pre || analyzerMode == AnalyzerMode::Both))
        || (newPost && (analyzerMode == AnalyzerMode::Post || analyzerMode == AnalyzerMode::Both)))
        changed = true;
    if (selected >= 0 && ! snapshot[(size_t) selected].used)
        setSelected (-1);

    // Red de seguridad: aunque nada parezca cambiar, se redibuja cada 250 ms.
    const double now = juce::Time::getMillisecondCounterHiRes();
    if (changed || now - lastRepaintMs > 250.0)
    {
        lastRepaintMs = now;
        ++tickRepaints;
        repaint();
    }
}

const EqGraph::CurveCache& EqGraph::cachedCurve (CurveCache& c, FilterType type, double freq, double gain, double q, int slope, double sr)
{
    if (c.type == type && c.freq == freq && c.gain == gain && c.q == q && c.slope == slope && c.sr == sr && c.db.size() == curveFreqs.size())
        return c;
    c.type = type;
    c.freq = freq;
    c.gain = gain;
    c.q = q;
    c.slope = slope;
    c.sr = sr;
    const auto d = designBand (type, freq, gain, q, slope, sr);
    c.db.resize (curveFreqs.size());
    c.h.resize (curveFreqs.size());
    for (size_t i = 0; i < curveFreqs.size(); ++i)
    {
        c.h[i] = response (d, curveFreqs[i], sr);
        c.db[i] = 20.0 * std::log10 (std::max (std::abs (c.h[i]), 1.0e-9));
    }
    return c;
}

// ------------------------------------------------------------------------- dibujo

void EqGraph::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat().reduced (0.5f);
    juce::Path frame;
    frame.addRoundedRectangle (bounds, theme::radius);

    g.setColour (theme::raised);
    g.fillPath (frame);
    const float reflScale = theme::isDark() ? 0.55f : 1.0f;
    juce::ColourGradient reflection (theme::iri1.withAlpha (0.09f * reflScale), bounds.getX(), bounds.getY(),
                                     theme::iri4.withAlpha (0.07f * reflScale), bounds.getRight(), bounds.getBottom(), false);
    reflection.addColour (0.55, theme::iri2.withAlpha (0.05f * reflScale));
    g.setGradientFill (reflection);
    g.fillPath (frame);

    {
        juce::Graphics::ScopedSaveState clip (g);
        g.reduceClipRegion (frame);
        const auto area = plotArea();
        drawGrid (g, area);
        if (analyzerMode == AnalyzerMode::Pre || analyzerMode == AnalyzerMode::Both)
            drawSpectrum (g, area, preSpectrum, false);
        if (analyzerMode == AnalyzerMode::Post || analyzerMode == AnalyzerMode::Both)
            drawSpectrum (g, area, postSpectrum, true);
        drawGhostPreview (g, area);
        drawCurves (g, area);
        drawLasso (g);
        drawNodes (g);
        drawCursorReadout (g, area);
    }

    g.setColour (theme::line);
    g.strokePath (frame, juce::PathStrokeType (1.0f));

    bool any = false;
    for (const auto& p : snapshot)
        any = any || p.used;
    if (! any)
    {
        g.setColour (theme::muted);
        g.setFont (theme::font (14.5f));
        g.drawText (zeq::utf8 ("Doble clic o Ctrl+clic para crear una banda · Clic derecho para elegir tipo"),
                    getLocalBounds().withTrimmedTop (getHeight() / 3).removeFromTop (24),
                    juce::Justification::centred);
    }
}

void EqGraph::drawGrid (juce::Graphics& g, juce::Rectangle<float> area)
{
    g.setFont (theme::font (11.0f));
    const double marks[] = { 20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000 };
    for (double hz : marks)
    {
        const float x = xForFreq (hz);
        g.setColour (theme::line.withMultipliedAlpha (hz == 100 || hz == 1000 || hz == 10000 ? 1.0f : 0.55f));
        g.drawVerticalLine ((int) x, area.getY(), area.getBottom());
        g.setColour (theme::muted);
        const auto label = hz >= 1000 ? juce::String ((int) (hz / 1000)) + "k" : juce::String ((int) hz);
        g.drawText (label, juce::Rectangle<float> (x + 4.0f, area.getBottom() - 20.0f, 40.0f, 16.0f), juce::Justification::left);
    }

    const float refRange = targetRangeDb;
    const float step = refRange / 2.0f;
    for (float db = -refRange; db <= refRange + 0.01f; db += step)
    {
        const float y = yForDb (db);
        if (y < area.getY() - 4.0f || y > area.getBottom() + 4.0f)
            continue;
        g.setColour (juce::approximatelyEqual (db, 0.0f) ? theme::ink.withAlpha (0.18f) : theme::line.withMultipliedAlpha (0.55f));
        g.drawHorizontalLine ((int) y, area.getX(), area.getRight());
        g.setColour (theme::muted);
        const auto label = (db > 0 ? "+" : "") + juce::String ((int) std::lround (db));
        g.drawText (label, juce::Rectangle<float> (area.getRight() - 40.0f, y - 15.0f, 34.0f, 14.0f), juce::Justification::right);
    }

    const bool autoPhase = selected >= 0 && snapshot[(size_t) selected].used && snapshot[(size_t) selected].type == FilterType::AllPass;
    if (showPhase || autoPhase)
    {
        g.setFont (theme::font (10.5f));
        g.setColour (theme::muted.withAlpha (0.65f));
        for (int deg : { -180, -90, 90, 180 })
        {
            const float y = yForPhase ((double) deg);
            const auto lbl = (deg > 0 ? "+" : "") + juce::String (deg) + zeq::utf8 ("°");
            g.drawText (lbl, juce::Rectangle<float> (area.getX() + 6.0f, y - 7.0f, 44.0f, 14.0f), juce::Justification::left);
        }
    }
}

namespace
{
/** Suavizado de presentación (≈ 1/12 de octava a 3 px por columna): dos pasadas de [¼ ½ ¼] en dB.
    No toca la medición: el espectro y la retención de picos siguen saliendo del FFT tal cual. */
void smoothColumns (std::vector<float>& v, int passes)
{
    if (v.size() < 3)
        return;
    std::vector<float> tmp (v.size());
    for (int p = 0; p < passes; ++p)
    {
        tmp.front() = v.front();
        tmp.back() = v.back();
        for (size_t i = 1; i + 1 < v.size(); ++i)
            tmp[i] = 0.25f * v[i - 1] + 0.5f * v[i] + 0.25f * v[i + 1];
        v.swap (tmp);
    }
}

/** Contorno por las columnas ya suavizadas. Con 3 px y el suavizado, los tramos rectos se ven continuos;
    las cuadráticas se subdividen al rellenar y trazar y duplicaban el costo del espectro. */
juce::Path smoothCurve (const std::vector<float>& xs, const std::vector<float>& ys)
{
    juce::Path p;
    if (xs.empty())
        return p;
    p.preallocateSpace ((int) xs.size() * 3 + 8);
    p.startNewSubPath (xs[0], ys[0]);
    for (size_t i = 1; i < xs.size(); ++i)
        p.lineTo (xs[i], ys[i]);
    return p;
}
} // namespace

void EqGraph::drawSpectrum (juce::Graphics& g, juce::Rectangle<float> area, const Spectrum& s, bool post)
{
    // Columnas de 3 px: cada una toma el máximo del FFT en su tramo de frecuencias (no se pierde ningún pico).
    const float step = 3.0f;
    const int n = juce::jmax (3, (int) std::ceil (area.getWidth() / step) + 1);
    std::vector<float> xs ((size_t) n), level ((size_t) n), peak (post ? (size_t) n : 0);
    for (int i = 0; i < n; ++i)
    {
        const float x = juce::jmin (area.getRight(), area.getX() + step * (float) i);
        xs[(size_t) i] = x;
        const double lo = freqForX (x - step * 0.5f), hi = freqForX (x + step * 0.5f);
        level[(size_t) i] = s.maxLevelDbBetween (lo, hi);
        if (post)
            peak[(size_t) i] = s.maxPeakDbBetween (lo, hi);
    }
    smoothColumns (level, 2);
    if (post)
        smoothColumns (peak, 1);

    std::vector<float> ys ((size_t) n);
    for (int i = 0; i < n; ++i)
        ys[(size_t) i] = yForSpectrum (level[(size_t) i]);
    const auto outline = smoothCurve (xs, ys);
    juce::Path body (outline);
    body.lineTo (area.getRight(), area.getBottom());
    body.lineTo (area.getX(), area.getBottom());
    body.closeSubPath();

    const bool dark = theme::isDark();
    if (post)
    {
        // Luz iridiscente (nunca relleno sólido) en un solo degradado que se desvanece hacia abajo:
        // una pasada de relleno (dos pasadas a pantalla completa costaban ~1 ms más por fotograma).
        const float k = dark ? 0.85f : 1.0f;
        juce::ColourGradient light (theme::iri2.withAlpha (0.30f * k), 0.0f, area.getY() + area.getHeight() * 0.15f,
                                    theme::iri1.withAlpha (0.03f * k), 0.0f, area.getBottom(), false);
        light.addColour (0.45, theme::iri1.withAlpha (0.16f * k));
        g.setGradientFill (light);
        g.fillPath (body);

        const auto lineCol = dark ? theme::iri1 : theme::iri2.darker (0.40f);
        g.setColour (lineCol.withAlpha (spectrumFrozen ? 0.85f : 0.62f));
        g.strokePath (outline, juce::PathStrokeType (spectrumFrozen ? 1.6f : 1.25f));

        // Retención de picos: línea fina por encima, sin relleno.
        std::vector<float> py ((size_t) n);
        for (int i = 0; i < n; ++i)
            py[(size_t) i] = yForSpectrum (peak[(size_t) i]);
        g.setColour ((dark ? theme::iri2 : theme::iri2.darker (0.5f)).withAlpha (dark ? 0.40f : 0.32f));
        g.strokePath (smoothCurve (xs, py), juce::PathStrokeType (0.9f));
    }
    else
    {
        // Pre: contorno gris discreto; con Pre + Post no compite con el espectro procesado.
        const bool alone = analyzerMode == AnalyzerMode::Pre;
        g.setColour (theme::ink.withAlpha (alone ? 0.06f : 0.035f));
        g.fillPath (body);
        g.setColour (theme::ink.withAlpha (alone ? 0.30f : 0.18f));
        g.strokePath (outline, juce::PathStrokeType (1.0f));
    }
}

void EqGraph::drawGhostPreview (juce::Graphics& g, juce::Rectangle<float> area)
{
    if (ghostAlpha < 0.03f)
        return;
    const double hz = freqForX (cursorPos.x);
    const double gdb = juce::jlimit (-kMaxGainDb, kMaxGainDb, dbForY (cursorPos.y));
    const FilterType type = BandAccess::smartTypeFor (hz, gdb);
    const double sr = effectiveSampleRate();
    const auto d = designBand (type, hz, hasGain (type) ? gdb : 0.0, params::defaultQFor (type), type == FilterType::LowCut || type == FilterType::HighCut ? 24 : 12, sr);

    const int n = juce::jmax (2, (int) (area.getWidth() / 4.0f) + 1);
    juce::Path ghost;
    for (int i = 0; i < n; ++i)
    {
        const float x = area.getX() + area.getWidth() * (float) i / (float) (n - 1);
        const double f = freqForX (x);
        const double db = 20.0 * std::log10 (std::max (std::abs (response (d, f, sr)), 1.0e-9));
        const float y = juce::jlimit (area.getY(), area.getBottom(), yForDb (db));
        i == 0 ? ghost.startNewSubPath (x, y) : ghost.lineTo (x, y);
    }

    juce::Path dashed;
    const float dash[] = { 4.0f, 4.0f };
    juce::PathStrokeType (1.25f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded)
        .createDashedStroke (dashed, ghost, dash, 2);
    g.setColour (theme::ink.withAlpha (0.18f * ghostAlpha));
    g.fillPath (dashed);

    const float gy = hasGain (type) ? cursorPos.y : yForDb (0.0);
    g.setColour (theme::ink.withAlpha (0.22f * ghostAlpha));
    g.drawEllipse (cursorPos.x - 5.5f, gy - 5.5f, 11.0f, 11.0f, 1.4f);
}

void EqGraph::drawLasso (juce::Graphics& g)
{
    if (! lassoActive)
        return;
    const auto box = juce::Rectangle<float> (lassoStart, lassoEnd);
    g.setColour (theme::iri2.withAlpha (0.14f));
    g.fillRoundedRectangle (box, 8.0f);
    g.setColour (theme::ink.withAlpha (0.35f));
    g.drawRoundedRectangle (box, 8.0f, 1.1f);
}

void EqGraph::drawCurves (juce::Graphics& g, juce::Rectangle<float> area)
{
    const double sr = effectiveSampleRate();
    const double gScale = currentGainScale();
    const bool globallyBypassed = proc.getState().getRawParameterValue (params::globalBypassId)->load (std::memory_order_relaxed) > 0.5f;

    const int n = juce::jmax (2, (int) (area.getWidth() / 2.0f) + 1);
    if ((int) curveFreqs.size() != n || curveAreaX != area.getX() || curveAreaW != area.getWidth())
    {
        // Columnas nuevas (cambió el tamaño): las respuestas guardadas ya no sirven.
        curveFreqs.resize ((size_t) n);
        for (int i = 0; i < n; ++i)
            curveFreqs[(size_t) i] = freqForX (area.getX() + area.getWidth() * (float) i / (float) (n - 1));
        curveAreaX = area.getX();
        curveAreaW = area.getWidth();
        for (auto* cache : { &staticCurves, &dynCurves })
            for (auto& c : *cache)
                c.db.clear();
    }
    std::vector<double> total ((size_t) n, 0.0), totalDyn ((size_t) n, 0.0), band ((size_t) n), bandDyn ((size_t) n);
    std::vector<std::complex<double>> totalH ((size_t) n, { 1.0, 0.0 });

    bool anyDynamic = false;
    bool anyActive = false;
    const float zeroY = yForDb (0.0);
    const bool needPhase = showPhase || (selected >= 0 && snapshot[(size_t) selected].used && snapshot[(size_t) selected].type == FilterType::AllPass);

    for (int b = 0; b < kMaxBands; ++b)
    {
        const auto& p = snapshot[(size_t) b];
        if (! p.used)
            continue;

        const int slope = kSlopesDbPerOct[(size_t) juce::jlimit (0, 7, p.slopeIndex)];
        const double scaledGain = p.gainDb * gScale;
        const auto& cs = cachedCurve (staticCurves[(size_t) b], p.type, p.freqHz, scaledGain, p.q, slope, sr);
        const bool phaseFromStatic = ! p.bypass && ! globallyBypassed && needPhase && ! (p.dynOn && hasGain (p.type));
        for (int i = 0; i < n; ++i)
        {
            band[(size_t) i] = cs.db[(size_t) i];
            if (phaseFromStatic)
                totalH[(size_t) i] *= cs.h[(size_t) i];
        }
        if (! p.bypass)
        {
            anyActive = true;
            for (int i = 0; i < n; ++i)
                total[(size_t) i] += band[(size_t) i];
        }

        const float emph = nodeEmphasis[(size_t) b];
        const auto colour = theme::bandColour (b);

        if (p.type != FilterType::AllPass)
        {
            juce::Path fill;
            fill.startNewSubPath (area.getX(), zeroY);
            for (int i = 0; i < n; ++i)
                fill.lineTo (area.getX() + area.getWidth() * (float) i / (float) (n - 1), juce::jlimit (area.getY(), area.getBottom(), yForDb (band[(size_t) i])));
            fill.lineTo (area.getRight(), zeroY);
            fill.closeSubPath();
            const float fillAlpha = (p.bypass || globallyBypassed) ? 0.03f : (0.07f + 0.14f * emph);
            g.setColour (colour.withAlpha (fillAlpha));
            g.fillPath (fill);
            if (emph > 0.05f)
            {
                g.setColour (colour.withAlpha (((p.bypass || globallyBypassed) ? 0.28f : 0.80f) * emph));
                g.strokePath (fill, juce::PathStrokeType (1.2f));
            }
        }

        const float dyn = dynVisual[(size_t) b];
        if (! p.bypass && ! globallyBypassed && p.dynOn && hasGain (p.type))
        {
            anyDynamic = true;
            // 0.01 dB es menos de una décima de píxel incluso en ±6 dB: no cambia el dibujo y evita recalcular
            // la banda dinámica en cada fotograma cuando su ganancia ya se asentó.
            const double dynQ = std::round ((double) dyn * 100.0) / 100.0;
            const auto& cd = cachedCurve (dynCurves[(size_t) b], p.type, p.freqHz, scaledGain + dynQ, p.q, slope, sr);
            for (int i = 0; i < n; ++i)
            {
                bandDyn[(size_t) i] = cd.db[(size_t) i];
                if (needPhase)
                    totalH[(size_t) i] *= cd.h[(size_t) i];
            }

            if (std::abs (dyn) > 0.05f)
            {
                juce::Path deltaPath;
                for (int i = 0; i < n; ++i)
                {
                    const float x = area.getX() + area.getWidth() * (float) i / (float) (n - 1);
                    const float y = juce::jlimit (area.getY(), area.getBottom(), yForDb (band[(size_t) i]));
                    i == 0 ? deltaPath.startNewSubPath (x, y) : deltaPath.lineTo (x, y);
                }
                for (int i = n - 1; i >= 0; --i)
                {
                    const float x = area.getX() + area.getWidth() * (float) i / (float) (n - 1);
                    const float y = juce::jlimit (area.getY(), area.getBottom(), yForDb (bandDyn[(size_t) i]));
                    deltaPath.lineTo (x, y);
                }
                deltaPath.closeSubPath();
                g.setColour (colour.withAlpha (0.22f));
                g.fillPath (deltaPath);
            }

            for (int i = 0; i < n; ++i)
                totalDyn[(size_t) i] += bandDyn[(size_t) i];
        }
        else if (! p.bypass)
        {
            for (int i = 0; i < n; ++i)
                totalDyn[(size_t) i] += band[(size_t) i];
        }
    }

    if (needPhase && anyActive && ! globallyBypassed)
    {
        juce::Path ph;
        double prevDeg = 0.0;
        for (int i = 0; i < n; ++i)
        {
            const float x = area.getX() + area.getWidth() * (float) i / (float) (n - 1);
            const double deg = std::arg (totalH[(size_t) i]) * (180.0 / std::numbers::pi);
            const float y = yForPhase (deg);
            if (i == 0 || std::abs (deg - prevDeg) > 150.0)
                ph.startNewSubPath (x, y);
            else
                ph.lineTo (x, y);
            prevDeg = deg;
        }
        juce::Path dashed;
        const float dashLengths[] = { 5.0f, 4.0f };
        juce::PathStrokeType (1.35f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded)
            .createDashedStroke (dashed, ph, dashLengths, 2);
        g.setColour (theme::iri2.darker (0.55f).withAlpha (showPhase ? 0.65f : 0.45f));
        g.fillPath (dashed);
    }

    auto curvePath = [&] (const std::vector<double>& db)
    {
        juce::Path c;
        for (int i = 0; i < n; ++i)
        {
            const float x = area.getX() + area.getWidth() * (float) i / (float) (n - 1);
            const float y = juce::jlimit (area.getY() - 2.0f, area.getBottom() + 2.0f, yForDb (db[(size_t) i]));
            i == 0 ? c.startNewSubPath (x, y) : c.lineTo (x, y);
        }
        return c;
    };

    if (globallyBypassed)
    {
        g.setColour (theme::muted.withAlpha (0.45f));
        g.strokePath (curvePath (total), juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    else if (anyDynamic)
    {
        g.setColour (theme::ink.withAlpha (0.28f));
        g.strokePath (curvePath (total), juce::PathStrokeType (1.2f));
        const juce::ColourGradient glow (theme::iri1.withAlpha (0.48f), area.getX(), 0, theme::iri3.withAlpha (0.48f), area.getRight(), 0, false);
        g.setGradientFill (glow);
        g.strokePath (curvePath (totalDyn), juce::PathStrokeType (6.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (theme::ink);
        g.strokePath (curvePath (totalDyn), juce::PathStrokeType (2.25f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    else
    {
        g.setColour (theme::ink);
        g.strokePath (curvePath (total), juce::PathStrokeType (2.25f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
}

void EqGraph::drawNodes (juce::Graphics& g)
{
    const double gScale = currentGainScale();
    for (int b = 0; b < kMaxBands; ++b)
    {
        const auto& p = snapshot[(size_t) b];
        if (! p.used)
            continue;
        const float appear = std::max (0.1f, nodeAppear[(size_t) b].value);
        const float emph = nodeEmphasis[(size_t) b];
        const auto pos = nodePosition (b, p);
        const auto colour = theme::bandColour (b);
        const bool isSel = isBandSelected (b);

        if (p.dynOn && hasGain (p.type) && ! p.bypass)
        {
            const auto rPos = dynRangePosition (p);
            const bool rangeHov = hoveredRangeBand == b || (drag.band == b && drag.rangeHandle);
            g.setColour (colour.withAlpha (0.58f));
            g.drawLine (pos.x, pos.y, rPos.x, rPos.y, 2.0f);

            const float hr = rangeHov ? 5.2f : 3.8f;
            g.setColour (rangeHov ? theme::ink : colour);
            g.fillEllipse (rPos.x - hr, rPos.y - hr, hr * 2.0f, hr * 2.0f);
            g.setColour (theme::raised);
            g.drawEllipse (rPos.x - hr, rPos.y - hr, hr * 2.0f, hr * 2.0f, 1.2f);

            const float yd = yForDb (juce::jlimit ((double) -displayedRangeDb, (double) displayedRangeDb, p.gainDb * gScale + dynVisual[(size_t) b]));
            g.setColour (theme::ink);
            g.fillEllipse (pos.x - 4.0f, yd - 4.0f, 8.0f, 8.0f);
        }

        const float r = (kNodeRadius + 2.4f * emph) * appear;
        const auto circle = juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (pos);
        if (emph > 0.02f && ! p.bypass)
        {
            g.setColour (colour.withAlpha (0.26f * emph));
            g.fillEllipse (circle.expanded (4.0f + 3.5f * emph));
        }
        g.setColour (isSel ? theme::ink : (p.bypass ? theme::paper : theme::raised));
        g.fillEllipse (circle);
        g.setColour (p.bypass ? theme::muted.withAlpha (0.6f) : colour);
        g.drawEllipse (circle, 2.5f);
        g.setColour (isSel ? theme::raised : (p.bypass ? theme::muted : theme::ink));
        g.setFont (theme::font (10.5f * std::min (appear, 1.05f), true));
        g.drawText (juce::String (b + 1), circle, juce::Justification::centred);

        if (p.solo)
        {
            g.setColour (theme::ink);
            g.setFont (theme::font (10.0f, true));
            g.drawText ("SOLO", circle.translated (0.0f, -r - 9.0f).expanded (14.0f, 0.0f), juce::Justification::centred);
        }

        if ((b == hovered || (drag.band == b && drag.moved)) && ! lassoActive)
        {
            juce::String info = params::typeNames()[(int) p.type] + "  " + format::freq (p.freqHz) + " (" + format::note (p.freqHz) + ")";
            if (hasGain (p.type))
                info << "  " << format::gain (p.gainDb);
            if (hasSlope (p.type))
                info << "  " << params::slopeNames()[juce::jlimit (0, 7, p.slopeIndex)];
            else if (usesQ (p))
                info << "  Q " << juce::String (p.q, 2);
            const auto fnt = theme::font (11.5f, true);
            g.setFont (fnt);
            const float w = (float) juce::GlyphArrangement::getStringWidthInt (fnt, info) + 20.0f;
            auto box = juce::Rectangle<float> (w, 24.0f).withCentre (pos.translated (0.0f, r + 19.0f));
            box = box.constrainedWithin (plotArea().reduced (6.0f));
            g.setColour (theme::ink.withAlpha (0.94f));
            g.fillRoundedRectangle (box, 9.0f);
            g.setColour (theme::raised);
            g.drawText (info, box, juce::Justification::centred);
        }
    }
}

void EqGraph::drawCursorReadout (juce::Graphics& g, juce::Rectangle<float> area)
{
    if (cursorAlpha < 0.02f && ! spectrumFrozen)
        return;
    const double hz = freqForX (cursorPos.x);
    const double db = dbForY (cursorPos.y);
    juce::String text;
    if (cursorAlpha >= 0.02f)
    {
        const FilterType suggested = BandAccess::smartTypeFor (hz, db);
        text = format::freq (hz) + zeq::utf8 (" · ") + format::note (hz) + zeq::utf8 (" · ") + format::gain (db);
        if (hovered < 0 && drag.band < 0 && ! lassoActive)
            text << zeq::utf8 ("  [") << params::typeNames()[(int) suggested] << "]";
    }
    if (spectrumFrozen)
        text = (text.isEmpty() ? juce::String() : text + zeq::utf8 ("  ·  ")) + "CONGELADO";

    const auto fnt = theme::font (11.5f, true);
    const float w = (float) juce::GlyphArrangement::getStringWidthInt (fnt, text) + 20.0f;
    const float alpha = spectrumFrozen ? 1.0f : cursorAlpha;
    const juce::Rectangle<float> pill (area.getX() + 12.0f, area.getY() + 12.0f, w, 24.0f);

    g.setColour (theme::surface.withAlpha (0.90f * alpha));
    g.fillRoundedRectangle (pill, 12.0f);
    g.setColour (theme::line.withMultipliedAlpha (alpha));
    g.drawRoundedRectangle (pill, 12.0f, 1.0f);
    g.setColour (theme::ink.withAlpha (0.86f * alpha));
    g.setFont (fnt);
    g.drawText (text, pill, juce::Justification::centred);
}

// ------------------------------------------------------------------------- ratón

void EqGraph::mouseMove (const juce::MouseEvent& e)
{
    mouseInside = true;
    cursorPos = e.position;
    const int h = nodeAt (e.position);
    const int hr = h < 0 ? dynHandleAt (e.position) : -1;
    if (h != hovered || hr != hoveredRangeBand)
    {
        hovered = h;
        hoveredRangeBand = hr;
        setMouseCursor ((h >= 0 || hr >= 0) ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::CrosshairCursor);
    }
    repaint();
}

void EqGraph::mouseExit (const juce::MouseEvent&)
{
    mouseInside = false;
    hovered = -1;
    hoveredRangeBand = -1;
    repaint();
}

void EqGraph::startDraggingSelection (int primaryBand, juce::Point<float> pos, const juce::ModifierKeys& mods)
{
    drag = {};
    drag.band = primaryBand;
    drag.start = pos;
    drag.qDrag = mods.isCommandDown() && mods.isAltDown();
    drag.axisLockMode = mods.isAltDown() && ! mods.isCommandDown() && ! mods.isShiftDown();

    for (int b = 0; b < kMaxBands; ++b)
    {
        if (! snapshot[(size_t) b].used || ! isBandSelected (b))
            continue;
        const auto p = snapshot[(size_t) b];
        drag.startFreq[(size_t) b] = p.freqHz;
        drag.startGain[(size_t) b] = p.gainDb;
        drag.startQ[(size_t) b] = p.q;
        if (drag.qDrag)
        {
            if (usesQ (p))
                access.begin (b, BandParam::Q);
        }
        else
        {
            access.begin (b, BandParam::Freq);
            if (hasGain (p.type))
                access.begin (b, BandParam::Gain);
            else if (usesQ (p))
                access.begin (b, BandParam::Q);
        }
    }

    // Audición automática (botón Audición o Alt+Shift+arrastre): activa Solo temporalmente mientras se arrastra.
    const bool wantListen = (autoListen || (mods.isAltDown() && mods.isShiftDown())) && selectedCount() == 1;
    if (wantListen)
    {
        drag.prevSoloState = snapshot[(size_t) primaryBand].solo;
        if (! drag.prevSoloState)
        {
            drag.tempSoloActive = true;
            access.setSolo (primaryBand, true);
        }
    }
}

void EqGraph::mouseDown (const juce::MouseEvent& e)
{
    const int n = nodeAt (e.position);
    if (e.mods.isPopupMenu())
    {
        if (n >= 0)
        {
            if (! isBandSelected (n))
                setSelected (n);
            showBandMenu (n);
        }
        else
        {
            showEmptySpaceMenu (e.position);
        }
        return;
    }

    if (n < 0)
    {
        const int rh = dynHandleAt (e.position);
        if (rh >= 0)
        {
            setSelected (rh);
            drag = {};
            drag.band = rh;
            drag.rangeHandle = true;
            drag.start = e.position;
            drag.startGain[0] = snapshot[(size_t) rh].gainDb;
            drag.startRange = snapshot[(size_t) rh].rangeDb;
            access.begin (rh, BandParam::Range);
            return;
        }

        // Ctrl + clic en vacío: crea una banda al instante y comienza a arrastrarla en un solo gesto.
        if (e.mods.isCommandDown())
        {
            const double f = freqForX (e.position.x);
            const double gdb = juce::jlimit (-kMaxGainDb, kMaxGainDb, dbForY (e.position.y));
            const FilterType type = BandAccess::smartTypeFor (f, gdb);
            const int created = access.create (type, (float) f, (float) gdb);
            if (created >= 0)
            {
                snapshot[(size_t) created] = access.read (created);
                nodeAppear[(size_t) created].value = 0.35f;
                nodeAppear[(size_t) created].velocity = 4.5f;
                nodeAppear[(size_t) created].target = 1.0f;
                wasUsed[(size_t) created] = true;
                setSelected (created);
                startDraggingSelection (created, e.position, juce::ModifierKeys());
            }
            return;
        }

        // Clic y arrastre en vacío: inicia rectángulo de selección múltiple (lazo).
        if (! e.mods.isShiftDown())
            setSelected (-1);
        lassoActive = true;
        lassoStart = lassoEnd = e.position;
        return;
    }

    // Alt + clic sin Shift/Ctrl sobre un nodo: conmuta Bypass al instante (o inicia arrastre con bloqueo de eje si se mueve).
    if (e.mods.isMiddleButtonDown() || (e.mods.isCommandDown() && ! e.mods.isAltDown()))
    {
        // Ctrl + clic o clic central sobre un nodo: restablece ganancia a 0 dB (y si ya está en 0, Q por defecto).
        setSelected (n);
        const auto p = snapshot[(size_t) n];
        if (hasGain (p.type) && std::abs (p.gainDb) > 0.02)
            access.set (n, BandParam::Gain, 0.0f);
        else if (usesQ (p))
            access.set (n, BandParam::Q, params::defaultQFor (p.type));
        return;
    }

    if (e.mods.isShiftDown() && ! e.mods.isAltDown())
    {
        // Shift + clic sobre nodo: añade o quita de la selección múltiple.
        multiSelected[(size_t) n] = ! multiSelected[(size_t) n];
        if (multiSelected[(size_t) n])
        {
            selected = n;
            if (onSelectionChanged)
                onSelectionChanged (n);
        }
        else if (selected == n)
        {
            selected = -1;
            for (int b = 0; b < kMaxBands; ++b)
                if (multiSelected[(size_t) b]) { selected = b; break; }
            if (onSelectionChanged)
                onSelectionChanged (selected);
        }
        repaint();
        if (! isBandSelected (n))
            return;
    }
    else if (! isBandSelected (n))
    {
        setSelected (n);
    }
    else if (selected != n)
    {
        selected = n;
        if (onSelectionChanged)
            onSelectionChanged (n);
    }

    startDraggingSelection (n, e.position, e.mods);
}

void EqGraph::mouseDrag (const juce::MouseEvent& e)
{
    mouseInside = true;
    cursorPos = e.position;

    if (lassoActive)
    {
        lassoEnd = e.position;
        const auto box = juce::Rectangle<float> (lassoStart, lassoEnd);
        int first = -1;
        for (int b = 0; b < kMaxBands; ++b)
        {
            if (! snapshot[(size_t) b].used)
                continue;
            const bool hit = box.contains (nodePosition (b, snapshot[(size_t) b]));
            multiSelected[(size_t) b] = hit;
            if (hit && first < 0)
                first = b;
        }
        if (first != selected)
        {
            selected = first;
            if (onSelectionChanged)
                onSelectionChanged (selected);
        }
        repaint();
        return;
    }

    if (drag.band < 0)
        return;

    const float k = e.mods.isShiftDown() && ! drag.tempSoloActive ? 0.15f : 1.0f;
    auto delta = (e.position - drag.start) * k;
    if ((e.position - drag.start).getDistanceFromOrigin() > 2.5f)
        drag.moved = true;

    if (drag.axisLockMode)
    {
        if (drag.lockedAxis == 0 && (std::abs (delta.x) > 3.0f || std::abs (delta.y) > 3.0f))
            drag.lockedAxis = std::abs (delta.x) >= std::abs (delta.y) ? 1 : 2;
        if (drag.lockedAxis == 1)
            delta.y = 0.0f;
        else if (drag.lockedAxis == 2)
            delta.x = 0.0f;
    }

    const double gScale = std::max (currentGainScale(), 0.1);

    if (drag.rangeHandle)
    {
        const double startScaled = (drag.startGain[0] + drag.startRange) * gScale;
        const double targetTotal = dbForY (yForDb (startScaled) + delta.y) / gScale;
        const double rng = juce::jlimit (-kMaxGainDb, kMaxGainDb, targetTotal - drag.startGain[0]);
        access.update (drag.band, BandParam::Range, (float) rng);
        return;
    }

    if (drag.qDrag)
    {
        const double qRatio = std::pow (2.0, -delta.y / 36.0);
        for (int b = 0; b < kMaxBands; ++b)
        {
            if (! snapshot[(size_t) b].used || ! isBandSelected (b))
                continue;
            if (usesQ (snapshot[(size_t) b]))
                access.update (b, BandParam::Q, (float) juce::jlimit (kMinQ, kMaxQ, drag.startQ[(size_t) b] * qRatio));
        }
        return;
    }

    const double primaryX0 = xForFreq (drag.startFreq[(size_t) drag.band]);
    const double freqRatio = freqForX ((float) primaryX0 + delta.x) / std::max (drag.startFreq[(size_t) drag.band], 1.0e-3);
    const double dbDelta = (dbForY (yForDb (0.0) + delta.y)) / gScale;

    for (int b = 0; b < kMaxBands; ++b)
    {
        if (! snapshot[(size_t) b].used || ! isBandSelected (b))
            continue;
        const auto p = snapshot[(size_t) b];
        const double f = juce::jlimit (kMinHz, kMaxHz, drag.startFreq[(size_t) b] * freqRatio);
        access.update (b, BandParam::Freq, (float) f);
        if (hasGain (p.type))
        {
            const double gdb = juce::jlimit (-kMaxGainDb, kMaxGainDb, drag.startGain[(size_t) b] + dbDelta);
            access.update (b, BandParam::Gain, (float) gdb);
        }
        else if (usesQ (p))
        {
            const double q = juce::jlimit (kMinQ, kMaxQ, drag.startQ[(size_t) b] * std::pow (2.0, -delta.y / 40.0));
            access.update (b, BandParam::Q, (float) q);
        }
    }
}

void EqGraph::mouseUp (const juce::MouseEvent& e)
{
    if (lassoActive)
    {
        lassoActive = false;
        repaint();
        return;
    }

    if (drag.band < 0)
        return;

    if (drag.tempSoloActive)
        access.setSolo (drag.band, drag.prevSoloState);

    if (drag.rangeHandle)
    {
        access.end (drag.band, BandParam::Range);
        drag = {};
        return;
    }

    for (int b = 0; b < kMaxBands; ++b)
    {
        if (! snapshot[(size_t) b].used || ! isBandSelected (b))
            continue;
        const auto p = snapshot[(size_t) b];
        if (drag.qDrag)
        {
            if (usesQ (p))
                access.end (b, BandParam::Q);
        }
        else
        {
            access.end (b, BandParam::Freq);
            if (hasGain (p.type))
                access.end (b, BandParam::Gain);
            else if (usesQ (p))
                access.end (b, BandParam::Q);
        }
    }

    // Alt + clic sin arrastrar: conmuta Bypass en un solo clic.
    if (! drag.moved && e.mods.isAltDown() && ! e.mods.isCommandDown() && ! e.mods.isShiftDown())
        toggleBypassSelection();

    drag = {};
}

void EqGraph::mouseDoubleClick (const juce::MouseEvent& e)
{
    const int n = nodeAt (e.position);
    if (n >= 0)
    {
        access.set (n, BandParam::Bypass, snapshot[(size_t) n].bypass ? 0.0f : 1.0f);
        return;
    }
    const double f = freqForX (e.position.x);
    const double gdb = juce::jlimit (-kMaxGainDb, kMaxGainDb, dbForY (e.position.y) / std::max (currentGainScale(), 0.1));
    const FilterType type = BandAccess::smartTypeFor (f, gdb);
    const int b = access.create (type, (float) f, (float) gdb);
    if (b >= 0)
    {
        snapshot[(size_t) b] = access.read (b);
        nodeAppear[(size_t) b].value = 0.25f;
        nodeAppear[(size_t) b].velocity = 4.5f;
        nodeAppear[(size_t) b].target = 1.0f;
        wasUsed[(size_t) b] = true;
        setSelected (b);
    }
}

void EqGraph::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (std::abs (w.deltaY) < 1.0e-5f)
        return;
    int n = nodeAt (e.position);
    if (n < 0)
        n = selected;
    if (n < 0 || ! snapshot[(size_t) n].used)
        return;

    const auto& p = snapshot[(size_t) n];
    // Alt + rueda: cambia pendiente (en cortes) o rango dinámico (si Dinámica está activa).
    if (e.mods.isAltDown())
    {
        if (hasSlope (p.type))
        {
            const int step = w.deltaY > 0.0f ? 1 : -1;
            const int next = juce::jlimit (0, (int) kSlopesDbPerOct.size() - 1, p.slopeIndex + step);
            access.set (n, BandParam::Slope, (float) next);
            snapshot[(size_t) n].slopeIndex = next;
            return;
        }
        if (hasGain (p.type))
        {
            if (! p.dynOn)
                access.set (n, BandParam::DynOn, 1.0f);
            const double nextRange = juce::jlimit (-kMaxGainDb, kMaxGainDb, p.rangeDb + (double) w.deltaY * (e.mods.isShiftDown() ? 1.0 : 4.0));
            access.set (n, BandParam::Range, (float) nextRange);
            snapshot[(size_t) n].rangeDb = nextRange;
            return;
        }
    }

    const double factor = std::pow (2.0, (double) w.deltaY * (e.mods.isShiftDown() ? 0.45 : 1.8));
    if (isBandSelected (n) && selectedCount() > 1)
    {
        for (int b = 0; b < kMaxBands; ++b)
            if (snapshot[(size_t) b].used && isBandSelected (b) && usesQ (snapshot[(size_t) b]))
            {
                const double q = juce::jlimit (kMinQ, kMaxQ, snapshot[(size_t) b].q * factor);
                access.set (b, BandParam::Q, (float) q);
                snapshot[(size_t) b].q = q;
            }
        return;
    }

    if (! usesQ (p))
        return;
    const double q = juce::jlimit (kMinQ, kMaxQ, p.q * factor);
    access.set (n, BandParam::Q, (float) q);
    snapshot[(size_t) n].q = q;
}

void EqGraph::showEmptySpaceMenu (juce::Point<float> pos)
{
    const double f = freqForX (pos.x);
    const double gdb = juce::jlimit (-kMaxGainDb, kMaxGainDb, dbForY (pos.y) / std::max (currentGainScale(), 0.1));

    juce::PopupMenu createMenu, menu;
    for (int i = 0; i < params::typeNames().size(); ++i)
        createMenu.addItem (100 + i, params::typeNames()[i]);

    menu.addSectionHeader ("Crear en " + format::freq (f) + " (" + format::note (f) + ")");
    menu.addSubMenu (zeq::utf8 ("Crear banda aquí"), createMenu);
    menu.addSeparator();
    menu.addItem (1, "Seleccionar todas las bandas (Ctrl+A)");
    menu.addItem (2, "Invertir todas las ganancias");
    menu.addItem (3, spectrumFrozen ? "Descongelar espectro (F)" : "Congelar espectro (F)", true, spectrumFrozen);
    menu.addItem (5, "Modo oscuro (M)", true, theme::isDark());
    menu.addSeparator();
    menu.addItem (4, "Eliminar todas las bandas");

    menu.setLookAndFeel (&getLookAndFeel());
    juce::Component::SafePointer<EqGraph> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition(), [safe, f, gdb] (int r)
    {
        if (safe == nullptr || r == 0)
            return;
        if (r >= 100 && r < 100 + params::typeNames().size())
        {
            const auto type = (FilterType) (r - 100);
            const int b = safe->access.create (type, (float) f, (float) gdb);
            if (b >= 0)
                safe->setSelected (b);
        }
        else if (r == 1)
            safe->selectAllBands();
        else if (r == 2)
            safe->access.invertAllGains();
        else if (r == 3)
        {
            if (safe->onToggleFreezeRequested)
                safe->onToggleFreezeRequested();
            else
                safe->setFrozen (! safe->spectrumFrozen);
        }
        else if (r == 5)
        {
            if (safe->onToggleDarkRequested)
                safe->onToggleDarkRequested();
        }
        else if (r == 4)
        {
            safe->access.clearAllBands();
            safe->setSelected (-1);
        }
    });
}

void EqGraph::showBandMenu (int band)
{
    const auto p = snapshot[(size_t) band];
    juce::PopupMenu types, slopes, places, menu;
    for (int i = 0; i < params::typeNames().size(); ++i)
        types.addItem (100 + i, params::typeNames()[i], true, (int) p.type == i);
    for (int i = 0; i < params::slopeNames().size(); ++i)
        slopes.addItem (200 + i, params::slopeNames()[i], true, p.slopeIndex == i);
    for (int i = 0; i < params::placementNames().size(); ++i)
        places.addItem (300 + i, params::placementNames()[i], true, (int) p.placement == i);

    menu.addSectionHeader ("Banda " + juce::String (band + 1) + zeq::utf8 (" · ") + format::freq (p.freqHz));
    menu.addSubMenu ("Tipo (T)", types);
    if (hasSlope (p.type))
        menu.addSubMenu ("Pendiente (Alt+rueda)", slopes);
    menu.addSubMenu (zeq::utf8 ("Estéreo"), places);
    menu.addSeparator();
    menu.addItem (1, "Bypass (B / Alt+clic)", true, p.bypass);
    menu.addItem (2, "Solo (S)", true, p.solo);
    if (hasGain (p.type))
    {
        menu.addItem (3, zeq::utf8 ("Dinámica (D)"), true, p.dynOn);
        menu.addItem (5, "Invertir ganancia");
        menu.addItem (6, "Restablecer a 0 dB (Ctrl+clic)");
    }
    menu.addSeparator();
    menu.addItem (4, "Eliminar banda (Supr)");

    menu.setLookAndFeel (&getLookAndFeel());
    juce::Component::SafePointer<EqGraph> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition(), [safe, band, p] (int r)
    {
        if (safe == nullptr || r == 0)
            return;
        auto& a = safe->access;
        if (r >= 300)
            a.set (band, BandParam::Placement, (float) (r - 300));
        else if (r >= 200)
            a.set (band, BandParam::Slope, (float) (r - 200));
        else if (r >= 100)
            a.set (band, BandParam::Type, (float) (r - 100));
        else if (r == 1)
            safe->toggleBypassSelection();
        else if (r == 2)
            a.setSolo (band, ! p.solo);
        else if (r == 3)
            safe->toggleDynamicSelection();
        else if (r == 5)
            a.set (band, BandParam::Gain, (float) -p.gainDb);
        else if (r == 6)
            a.set (band, BandParam::Gain, 0.0f);
        else if (r == 4)
            safe->deleteSelection();
    });
}
} // namespace zeq::ui
