// zeq_snapshot — verificación del editor real de ZUENA EQ fuera de pantalla. No se distribuye.
//
//   zeq_snapshot <salida.png>   Abre el editor con bandas y ruido rosa y guarda una captura.
//   zeq_snapshot audit          Recorre todos los controles del editor (EB-01, EB-02, EB-03):
//                               cada control visible cambia un parámetro real, cada parámetro
//                               tiene control, deshacer lo restaura y la automatización se refleja.

#include "../Source/PluginProcessor.h"
#include "../Source/UI/PluginEditor.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <cmath>
#include <iostream>
#include <map>
#include <random>
#include <set>

namespace
{
using P = zeq::params::BandParam;
using zeq::ui::ZuenaEqEditor;

/** Procesador + editor real en una ventana fuera de pantalla. */
struct Session
{
    ZuenaEqProcessor proc;
    std::unique_ptr<ZuenaEqEditor> editor;

    Session()
    {
        proc.setRateAndBufferSizeDetails (48000.0, 512);
        proc.prepareToPlay (48000.0, 512);
    }

    ~Session()
    {
        if (editor != nullptr)
            editor->removeFromDesktop();
        editor.reset();
    }

    void open()
    {
        editor.reset (dynamic_cast<ZuenaEqEditor*> (proc.createEditor()));
        editor->setTopLeftPosition (-6000, -6000);
        editor->addToDesktop (juce::ComponentPeer::windowIsTemporary);
        editor->setVisible (true);
        // Determinista: 100 % salvo ZEQ_SCALE (p. ej. 70); no depende de la pantalla ni de la preferencia guardada.
        const auto envScale = juce::SystemStats::getEnvironmentVariable ("ZEQ_SCALE", "100").getFloatValue() / 100.0f;
        editor->setUiScale (1.0f, false);
        editor->setSize (ZuenaEqEditor::kBaseW, ZuenaEqEditor::kBaseH);
        editor->setUiScale (envScale, false);
        pump();
    }

    /** Procesa mensajes pendientes, vuelca los parámetros al árbol (registro de deshacer) y dibuja un fotograma. */
    void pump (int ms = 5)
    {
        juce::MessageManager::getInstance()->runDispatchLoopUntil (ms);
        proc.getState().copyState();
        editor->frame();
    }

    juce::RangedAudioParameter& param (int band, P p) { return *proc.getState().getParameter (zeq::params::bandId (band, p)); }
    void set (int band, P p, float v) { auto& pr = param (band, p); pr.setValueNotifyingHost (pr.convertTo0to1 (v)); }
    float get (int band, P p) { return proc.getState().getRawParameterValue (zeq::params::bandId (band, p))->load(); }
};

// ------------------------------------------------------------------------------------------- captura

int cmdSnapshot (const juce::File& out)
{
    Session s;
    struct B { zeq::FilterType t; float f, g, q; int slope; bool dyn; };
    const B bands[] = { { zeq::FilterType::LowCut, 32.0f, 0.0f, 0.7071f, 3, false }, { zeq::FilterType::Bell, 110.0f, 3.5f, 0.9f, 1, false },
                        { zeq::FilterType::Bell, 420.0f, -4.0f, 1.8f, 1, true }, { zeq::FilterType::Notch, 3150.0f, 0.0f, 6.0f, 1, false },
                        { zeq::FilterType::HighShelf, 8000.0f, 2.5f, 0.7071f, 1, false } };
    for (int i = 0; i < 5; ++i)
    {
        s.set (i, P::Type, (float) bands[i].t);
        s.set (i, P::Freq, bands[i].f);
        s.set (i, P::Gain, bands[i].g);
        s.set (i, P::Q, bands[i].q);
        s.set (i, P::Slope, (float) bands[i].slope);
        s.set (i, P::DynOn, bands[i].dyn ? 1.0f : 0.0f);
        s.set (i, P::Used, 1.0f);
    }
    s.set (2, P::Threshold, -42.0f);
    s.set (2, P::Range, -6.0f);
    if (auto* qm = s.proc.getState().getParameter (zeq::params::qualityModeId))
        qm->setValueNotifyingHost (1.0f);  // Fase natural 2x

    s.open();
    for (auto* child : s.editor->getContentRoot().getChildren())
    {
        if (auto* chip = dynamic_cast<zeq::ui::BandChip*> (child); chip != nullptr && chip->getName() == "Banda 3")
            chip->triggerClick();
        if (auto* tb = dynamic_cast<juce::TextButton*> (child); tb != nullptr && tb->getButtonText() == "Fase")
            tb->triggerClick();
    }

    // Ruido rosa aproximado (filtro de Paul Kellet) para poblar el analizador.
    std::mt19937 rng (3);
    std::normal_distribution<float> n (0.0f, 0.08f);
    float b0 = 0, b1 = 0, b2 = 0;
    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;
    for (int frame = 0; frame < 120; ++frame)
    {
        for (int k = 0; k < 2; ++k)
        {
            for (int i = 0; i < 512; ++i)
            {
                const float w = n (rng);
                b0 = 0.99765f * b0 + w * 0.0990460f;
                b1 = 0.96300f * b1 + w * 0.2965164f;
                b2 = 0.57000f * b2 + w * 1.0526913f;
                const float pink = (b0 + b1 + b2 + w * 0.1848f) * 0.25f;
                buffer.setSample (0, i, pink);
                buffer.setSample (1, i, pink);
            }
            s.proc.processBlock (buffer, midi);
        }
        s.pump (16);
    }

    // Posiciona el cursor sobre el gráfico para mostrar también la píldora de frecuencia/nota en vivo.
    if (auto* graph = dynamic_cast<zeq::ui::EqGraph*> (s.editor->getContentRoot().getChildComponent (0)))
    {
        const juce::Point<float> p (graph->getWidth() * 0.28f, graph->getHeight() * 0.26f);
        const auto now = juce::Time::getCurrentTime();
        graph->mouseMove (juce::MouseEvent (
            juce::Desktop::getInstance().getMainMouseSource(), p, juce::ModifierKeys(),
            juce::MouseInputSource::defaultPressure, juce::MouseInputSource::defaultOrientation,
            juce::MouseInputSource::defaultRotation, juce::MouseInputSource::defaultTiltX,
            juce::MouseInputSource::defaultTiltY, graph, graph, now, p, now, 0, false));
        for (int k = 0; k < 12; ++k)
            s.pump (16);
    }

    // 2x: la galería del launcher la muestra ampliada y en pantallas de alta densidad (1x se veía pixelada).
    const auto image = s.editor->createComponentSnapshot (s.editor->getLocalBounds(), true, 2.0f);
    out.deleteFile();
    juce::FileOutputStream stream (out);
    const bool ok = stream.openedOk() && juce::PNGImageFormat().writeImageToStream (image, stream);
    std::cout << (ok ? "Captura guardada en " : "No se pudo guardar ") << out.getFullPathName() << std::endl;
    return ok ? 0 : 1;
}

// ------------------------------------------------------------------------------------------- auditoría

int checks = 0, failures = 0;

void check (bool ok, const juce::String& what)
{
    ++checks;
    if (! ok)
    {
        ++failures;
        std::cout << "  FALLA " << what << std::endl;
    }
}

using Values = std::map<juce::String, float>;

Values values (juce::AudioProcessor& p)
{
    Values m;
    for (auto* param : p.getParameters())
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (param))
            m[r->getParameterID()] = r->getValue();
    return m;
}

juce::StringArray changed (const Values& a, const Values& b)
{
    juce::StringArray out;
    for (const auto& [id, v] : a)
        if (std::abs (b.at (id) - v) > 1.0e-6f)
            out.add (id);
    return out;
}

template <typename T>
void collect (juce::Component& root, std::vector<T*>& out)
{
    for (auto* c : root.getChildren())
    {
        if (auto* t = dynamic_cast<T*> (c))
            out.push_back (t);
        collect (*c, out);
    }
}

template <typename T>
T* findFirst (juce::Component& root)
{
    std::vector<T*> all;
    collect (root, all);
    return all.empty() ? nullptr : all.front();
}

juce::MouseEvent mouseEvent (juce::Component& c, juce::Point<float> pos, juce::Point<float> down, int clicks, bool dragged)
{
    const auto now = juce::Time::getCurrentTime();
    return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), pos, juce::ModifierKeys(),
                             juce::MouseInputSource::defaultPressure, juce::MouseInputSource::defaultOrientation, juce::MouseInputSource::defaultRotation,
                             juce::MouseInputSource::defaultTiltX, juce::MouseInputSource::defaultTiltY, &c, &c, now, down, now, clicks, dragged);
}

/** Lo que cambió un control; vacío si no hizo nada. */
struct Action
{
    juce::String name;
    juce::Component* control;
    std::function<void()> run;
};

/** Ejecuta una acción como una transacción de deshacer y devuelve los parámetros que cambió. Después la deshace y comprueba. */
juce::StringArray runAndUndo (Session& s, const Action& a, const juce::String& context)
{
    auto& undo = s.proc.getUndoManager();
    const auto before = values (s.proc);
    undo.beginNewTransaction();
    a.run();
    s.pump();
    const auto diff = changed (before, values (s.proc));
    check (! diff.isEmpty(), "EB-01 " + context + ": '" + a.name + "' no cambia ningún parámetro");

    undo.beginNewTransaction();
    undo.undo();
    s.pump();
    const auto left = changed (before, values (s.proc));
    check (left.isEmpty(), "EB-03 " + context + ": deshacer '" + a.name + "' no restaura " + left.joinIntoString (", "));
    return diff;
}

/** EB-03: un cambio del host (automatización / preset) se refleja en el control. */
void checkReflects (Session& s, juce::Component* control, const juce::String& id, const juce::String& context, std::mt19937& rng)
{
    // Un botón momentáneo (p. ej. "Invertir" ganancia) ejecuta una acción y no tiene estado que reflejar:
    // compararlo contra el parámetro daba falsos EB-03 según el valor aleatorio.
    if (auto* button = dynamic_cast<juce::Button*> (control); button != nullptr && ! button->isToggleable())
        return;

    auto* p = s.proc.getState().getParameter (id);
    const float before = p->getValue();
    std::uniform_real_distribution<float> u (0.05f, 0.95f);
    float target = u (rng);
    if (p->getNumSteps() == 2)
        target = before > 0.5f ? 0.0f : 1.0f;
    p->setValueNotifyingHost (target);
    s.pump();
    const float real = p->convertFrom0to1 (p->getValue());

    bool ok = true;
    if (auto* slider = dynamic_cast<juce::Slider*> (control))
        ok = std::abs (slider->getValue() - real) <= 1.0e-3 * juce::jmax (1.0, std::abs ((double) real));
    else if (auto* combo = dynamic_cast<juce::ComboBox*> (control))
        ok = combo->getSelectedItemIndex() == (int) std::lround (real);
    else if (auto* button = dynamic_cast<juce::Button*> (control))
        ok = button->getToggleState() == (real > 0.5f);
    check (ok, "EB-03 " + context + ": el control de '" + id + "' no refleja un cambio del host");

    p->setValueNotifyingHost (before);
    s.pump();
}

void auditCard (Session& s, int band, const juce::String& config, std::set<juce::String>& covered, std::mt19937& rng)
{
    auto& ed = *s.editor;
    const auto context = "banda " + juce::String (band + 1) + " (" + config + ")";
    const auto prefix = "b" + juce::String (band + 1) + "_";

    zeq::ui::BandChip* chip = nullptr;
    for (auto* c : ed.getContentRoot().getChildren())
        if (auto* bc = dynamic_cast<zeq::ui::BandChip*> (c); bc != nullptr && bc->getName() == "Banda " + juce::String (band + 1))
            chip = bc;
    check (chip != nullptr && chip->isShowing(), "EB-01 " + context + ": la ficha de la barra inferior no se ve");
    if (chip == nullptr)
        return;
    check (ed.getContentRoot().getLocalBounds().contains (chip->getBounds()), "EB-01 " + context + ": la ficha de la barra inferior queda fuera de la ventana");
    chip->triggerClick();
    s.pump();

    auto* card = findFirst<zeq::ui::BandCard> (ed);
    check (card != nullptr && card->isShowing() && card->getBand() == band, "EB-01 " + context + ": la ficha no abre la banda");
    if (card == nullptr || card->getBand() != band)
        return;
    check (ed.getContentRoot().getLocalBounds().contains (card->getBounds()), "EB-01 " + context + ": la ficha flotante queda fuera de la ventana");
    {
        // EB-28: una ficha visible muestra siempre su contenido (listas con texto, perillas con valor).
        std::vector<juce::ComboBox*> shown;
        collect (*card, shown);
        for (auto* cb : shown)
            if (cb->isShowing())
                check (cb->getText().isNotEmpty(), "EB-28 " + context + ": la lista '" + cb->getName() + "' está vacía");
        std::vector<juce::Label*> labels;
        collect (*card, labels);
        int emptyValues = 0;
        for (auto* l : labels)
            if (l->isShowing() && l->isEditable() && l->getText().isEmpty())
                ++emptyValues;
        check (emptyValues == 0, "EB-28 " + context + ": " + juce::String (emptyValues) + " perillas sin valor");
    }

    std::vector<Action> actions;
    juce::Button* remove = nullptr;
    std::vector<juce::Slider*> sliders;
    std::vector<juce::ComboBox*> combos;
    std::vector<juce::Button*> buttons;
    collect (*card, sliders);
    collect (*card, combos);
    collect (*card, buttons);
    for (auto* sl : sliders)
        if (sl->isShowing())
            actions.push_back ({ sl->getName(), sl, [sl]
            {
                const double prop = std::fmod (sl->valueToProportionOfLength (sl->getValue()) + 0.37, 1.0);
                sl->setValue (sl->proportionOfLengthToValue (prop), juce::sendNotificationSync);
            } });
    for (auto* cb : combos)
        if (cb->isShowing())
            actions.push_back ({ cb->getName(), cb, [cb] { cb->setSelectedItemIndex ((cb->getSelectedItemIndex() + 1) % cb->getNumItems(), juce::sendNotificationSync); } });
    for (auto* b : buttons)
    {
        if (! b->isShowing() || dynamic_cast<juce::TextButton*> (b) == nullptr)
            continue;
        if (b->getButtonText() == "Eliminar")
            remove = b;
        else
            actions.push_back ({ b->getButtonText(), b, [b] { b->triggerClick(); } });
    }

    for (const auto& a : actions)
    {
        const auto diff = runAndUndo (s, a, context);
        for (const auto& id : diff)
        {
            check (id.startsWith (prefix), "EB-01 " + context + ": '" + a.name + "' cambia un parámetro de otra banda: " + id);
            if (id.startsWith (prefix))
                covered.insert (id);
        }
        if (diff.size() == 1)
            checkReflects (s, a.control, diff[0], context, rng);
    }

    check (remove != nullptr, "EB-01 " + context + ": falta el botón Eliminar");
    if (remove != nullptr)
    {
        const auto diff = runAndUndo (s, { "Eliminar", remove, [remove] { remove->triggerClick(); } }, context);
        check (diff.contains (prefix + "used"), "EB-01 " + context + ": Eliminar no borra la banda");
        for (const auto& id : diff)
            covered.insert (id);
    }
}

void auditGraph (Session& s, std::set<juce::String>& covered)
{
    auto& ed = *s.editor;
    auto* graph = findFirst<zeq::ui::EqGraph> (ed);
    check (graph != nullptr, "El gráfico existe");
    if (graph == nullptr)
        return;
    auto& undo = s.proc.getUndoManager();

    // Doble clic en vacío: crea una banda Bell donde se hizo clic y la selecciona.
    const juce::Point<float> at (graph->getWidth() * 0.45f, graph->getHeight() * 0.35f);
    auto before = values (s.proc);
    undo.beginNewTransaction();
    graph->mouseDoubleClick (mouseEvent (*graph, at, at, 2, false));
    s.pump();
    auto diff = changed (before, values (s.proc));
    check (diff.contains ("b1_used") && s.get (0, P::Used) > 0.5f, "EB-01 Gráfico: el doble clic no crea la banda 1");
    check (s.get (0, P::Gain) > 1.0f, "EB-01 Gráfico: la banda nueva no toma la ganancia del punto (" + juce::String (s.get (0, P::Gain), 2) + " dB)");
    check (graph->getSelected() == 0, "EB-01 Gráfico: la banda nueva no queda seleccionada");
    for (const auto& id : diff)
        covered.insert (id);

    // Arrastre del nodo: X = frecuencia, Y = ganancia.
    const float f0 = s.get (0, P::Freq), g0 = s.get (0, P::Gain);
    const auto to = at.translated (80.0f, 60.0f);
    undo.beginNewTransaction();
    graph->mouseDown (mouseEvent (*graph, at, at, 1, false));
    graph->mouseDrag (mouseEvent (*graph, to, at, 1, true));
    graph->mouseUp (mouseEvent (*graph, to, at, 1, true));
    s.pump();
    check (s.get (0, P::Freq) > f0 * 1.2f, "EB-01 Gráfico: arrastrar a la derecha no sube la frecuencia");
    check (s.get (0, P::Gain) < g0 - 1.0f, "EB-01 Gráfico: arrastrar hacia abajo no baja la ganancia");

    // Rueda sobre el nodo: Q.
    const float q0 = s.get (0, P::Q);
    undo.beginNewTransaction();
    graph->mouseWheelMove (mouseEvent (*graph, to, to, 0, false), juce::MouseWheelDetails { 0.0f, 0.25f, false, false, false });
    s.pump();
    check (std::abs (s.get (0, P::Q) - q0) > 1.0e-3f, "EB-01 Gráfico: la rueda no cambia la Q");

    // Doble clic en el nodo: bypass.
    undo.beginNewTransaction();
    graph->mouseDoubleClick (mouseEvent (*graph, to, to, 2, false));
    s.pump();
    check (s.get (0, P::Bypass) > 0.5f, "EB-01 Gráfico: el doble clic en el nodo no hace bypass");

    // Teclado: Supr borra la banda seleccionada; Ctrl+Z la recupera; botones Deshacer / Rehacer.
    undo.beginNewTransaction();
    ed.keyPressed (juce::KeyPress (juce::KeyPress::deleteKey));
    s.pump();
    check (s.get (0, P::Used) < 0.5f, "EB-01 Teclado: Supr no borra la banda seleccionada");
    ed.keyPressed (juce::KeyPress ('z', juce::ModifierKeys::commandModifier, 0));
    s.pump();
    check (s.get (0, P::Used) > 0.5f, "EB-01 Teclado: Ctrl+Z no deshace el borrado");

    juce::TextButton* undoButton = nullptr;
    juce::TextButton* redoButton = nullptr;
    for (auto* c : ed.getContentRoot().getChildren())
        if (auto* tb = dynamic_cast<juce::TextButton*> (c))
        {
            if (tb->getButtonText() == "Deshacer")
                undoButton = tb;
            if (tb->getButtonText() == "Rehacer")
                redoButton = tb;
        }
    check (undoButton != nullptr && redoButton != nullptr, "EB-01 Barra: faltan Deshacer / Rehacer");
    if (undoButton != nullptr && redoButton != nullptr)
    {
        check (redoButton->isEnabled(), "EB-01 Barra: Rehacer deshabilitado con algo para rehacer");
        redoButton->triggerClick();
        s.pump();
        check (s.get (0, P::Used) < 0.5f, "EB-01 Barra: Rehacer no rehace el borrado");
        undoButton->triggerClick();
        s.pump();
        check (s.get (0, P::Used) > 0.5f, "EB-01 Barra: Deshacer no deshace el borrado");
    }
    s.set (0, P::Used, 0.0f);
    s.pump();
}

void auditHeader (Session& s, std::set<juce::String>& covered, std::mt19937& rng)
{
    auto& ed = *s.editor;
    for (auto* knob : ed.getContentRoot().getChildren())
        if (auto* k = dynamic_cast<zeq::ui::Knob*> (knob))
            if (auto* sl = findFirst<juce::Slider> (*k))
            {
                const auto diff = runAndUndo (s, { sl->getName(), sl, [sl] { sl->setValue (sl->getValue() + 3.0, juce::sendNotificationSync); } }, "Barra");
                for (const auto& id : diff)
                    covered.insert (id);
                if (diff.size() == 1)
                    checkReflects (s, sl, diff[0], "Barra", rng);
            }

    // Analizador, curva de fase, escala, modo de fase, auto-ganancia, presets y comparación A/B.
    const juce::StringArray analyzer { "Off", "Pre", "Post", "Pre + Post" };
    const juce::StringArray ranges { zeq::utf8 ("±6"), zeq::utf8 ("±12"), zeq::utf8 ("±30") };
    const int rangeDb[] = { 6, 12, 30 };
    juce::TextButton* slotA = nullptr;
    juce::TextButton* slotB = nullptr;
    juce::TextButton* copyAb = nullptr;

    for (auto* c : ed.getContentRoot().getChildren())
    {
        if (auto* cb = dynamic_cast<juce::ComboBox*> (c))
        {
            if (cb->getName() == "Modo de fase")
            {
                const auto diff = runAndUndo (s, { cb->getName(), cb, [cb] { cb->setSelectedItemIndex (1, juce::sendNotificationSync); } }, "Barra");
                for (const auto& id : diff)
                    covered.insert (id);
                if (diff.size() == 1)
                    checkReflects (s, cb, diff[0], "Barra", rng);
            }
            else if (cb->getName() == "Presets")
            {
                const auto before = values (s.proc);
                cb->setSelectedItemIndex (1, juce::sendNotificationSync); // "Voz presente"
                s.pump();
                check (s.get (0, P::Used) > 0.5f && s.get (1, P::Used) > 0.5f, "EB-01 Barra: el selector de Presets no carga las bandas");
                s.proc.getUndoManager().undo();
                s.pump();
                check (changed (before, values (s.proc)).isEmpty(), "EB-03 Barra: Deshacer no revierte la carga de un preset");
            }
        }
        else if (auto* tb = dynamic_cast<juce::TextButton*> (c))
        {
            if (tb->getName() == "Auto ganancia" || tb->getName() == "Bypass global")
            {
                const auto diff = runAndUndo (s, { tb->getName(), tb, [tb] { tb->triggerClick(); } }, "Barra");
                for (const auto& id : diff)
                    covered.insert (id);
                if (diff.size() == 1)
                    checkReflects (s, tb, diff[0], "Barra", rng);
                continue;
            }
            if (tb->getButtonText() == "+ Banda")
            {
                const auto diff = runAndUndo (s, { "+ Banda", tb, [tb] { tb->triggerClick(); } }, "Barra inferior");
                check (! diff.isEmpty(), "EB-01 Barra inferior: + Banda crea una banda");
                continue;
            }
            if (tb->getButtonText() == "Congelar" || tb->getButtonText() == zeq::utf8 ("Audición")
                || tb->getButtonText() == "Oscuro" || tb->getButtonText() == "?")
            {
                const bool prev = tb->getToggleState();
                tb->triggerClick();
                s.pump();
                check (tb->getToggleState() != prev, "EB-01 Barra inferior: '" + tb->getButtonText() + "' alterna su estado");
                tb->triggerClick();
                s.pump();
                continue;
            }
            if (tb->getButtonText() == "Fase")
            {
                const bool prev = (bool) s.proc.getState().state.getProperty ("ui_phase", false);
                tb->triggerClick();
                s.pump();
                check ((bool) s.proc.getState().state.getProperty ("ui_phase", false) != prev && tb->getToggleState() != prev,
                       "EB-01 Barra: el botón Fase no alterna la curva de fase");
                tb->triggerClick();
                s.pump();
                continue;
            }
            if (tb->getButtonText() == "A") { slotA = tb; continue; }
            if (tb->getButtonText() == "B") { slotB = tb; continue; }
            if (tb->getButtonText() == zeq::utf8 ("A→B") || tb->getButtonText() == zeq::utf8 ("B→A")) { copyAb = tb; continue; }

            const int a = analyzer.indexOf (tb->getButtonText());
            const int r = ranges.indexOf (tb->getButtonText());
            if (a < 0 && r < 0)
                continue;
            tb->triggerClick();
            s.pump();
            const auto& tree = s.proc.getState().state;
            if (a >= 0)
                check ((int) tree.getProperty ("ui_analyzer") == a && tb->getToggleState(), "EB-01 Barra: el botón de analizador '" + tb->getButtonText() + "' no cambia el modo");
            else
                check ((int) tree.getProperty ("ui_range") == rangeDb[r] && tb->getToggleState(), "EB-01 Barra: el botón de escala '" + tb->getButtonText() + "' no cambia la escala");
        }
    }

    check (slotA != nullptr && slotB != nullptr && copyAb != nullptr, "EB-01 Barra: faltan los controles A / B / Copiar");
    if (slotA != nullptr && slotB != nullptr && copyAb != nullptr)
    {
        s.set (0, P::Used, 1.0f);
        s.set (0, P::Gain, 7.5f);
        s.pump();
        slotB->triggerClick();
        s.pump();
        check (s.get (0, P::Used) < 0.5f && slotB->getToggleState(), "EB-01 Barra: cambiar a ranura B no carga estado limpio");
        slotA->triggerClick();
        s.pump();
        check (s.get (0, P::Used) > 0.5f && std::abs (s.get (0, P::Gain) - 7.5f) < 0.05f, "EB-01 Barra: volver a ranura A no recupera la banda");
        copyAb->triggerClick();
        slotB->triggerClick();
        s.pump();
        check (s.get (0, P::Used) > 0.5f && std::abs (s.get (0, P::Gain) - 7.5f) < 0.05f, "EB-01 Barra: A→B no copia el estado a B");
        slotA->triggerClick();
        s.set (0, P::Used, 0.0f);
        s.pump();
    }
}

/** Tamaño de la interfaz: cada escala respeta los límites, el contenido entra entero y nada se pisa. */
void auditScale (Session& s)
{
    auto& ed = *s.editor;
    auto& root = ed.getContentRoot();
    const juce::String ctx = zeq::utf8 ("Tamaño");
    const float initial = ed.getUiScale(); // se restaura al final (la auditoría puede correr a otra escala)

    // Escala inicial según la pantalla (notebooks con escalado de Windows incluidas).
    struct Fit { int w, h; float expected; };
    for (const auto& f : { Fit { 1280, 672, 0.7f }, Fit { 1536, 824, 0.9f }, Fit { 1920, 1040, 1.0f }, Fit { 2560, 1400, 1.0f }, Fit { 800, 480, 0.7f } })
        check (std::abs (ZuenaEqEditor::fitScale ({ 0, 0, f.w, f.h }) - f.expected) < 0.001f,
               ctx + ": pantalla " + juce::String (f.w) + "x" + juce::String (f.h) + " no elige " + juce::String (f.expected));

    juce::TextButton* sizeBtn = nullptr;
    for (auto* c : root.getChildren())
        if (auto* tb = dynamic_cast<juce::TextButton*> (c); tb != nullptr && tb->getName() == ctx)
            sizeBtn = tb;
    check (sizeBtn != nullptr && sizeBtn->isShowing(), "EB-01 " + ctx + ": falta el botón de tamaño");

    for (const float scale : ZuenaEqEditor::kUiScales)
    {
        ed.setUiScale (1.0f, false);
        ed.setSize (ZuenaEqEditor::kBaseW, ZuenaEqEditor::kBaseH);
        ed.setUiScale (scale, false);
        s.pump();
        const auto pct = juce::String (juce::roundToInt (scale * 100.0f)) + " %";
        const auto c = ctx + " " + pct;
        check (std::abs (ed.getUiScale() - scale) < 0.001f, c + ": no aplica la escala");
        check (ed.getWidth() == juce::roundToInt (ZuenaEqEditor::kBaseW * scale) && ed.getHeight() == juce::roundToInt (ZuenaEqEditor::kBaseH * scale),
               c + ": la ventana mide " + juce::String (ed.getWidth()) + "x" + juce::String (ed.getHeight()));
        if (auto* con = ed.getConstrainer())
            check (con->getMinimumWidth() == juce::roundToInt (ZuenaEqEditor::kMinW * scale), c + ": el mínimo no acompaña la escala");
        // El contenido lógico cubre la ventana entera.
        check (std::abs ((float) root.getWidth() * scale - (float) ed.getWidth()) <= scale + 0.5f
                   && std::abs ((float) root.getHeight() * scale - (float) ed.getHeight()) <= scale + 0.5f,
               c + ": el contenido no cubre la ventana");
        if (sizeBtn != nullptr)
            check (sizeBtn->getButtonText() == pct, c + ": el botón muestra '" + sizeBtn->getButtonText() + "'");

        // Ningún control queda fuera ni se pisa con otro (barras superior e inferior).
        std::vector<juce::Component*> controls;
        for (auto* ch : root.getChildren())
            if (ch->isVisible() && dynamic_cast<zeq::ui::EqGraph*> (ch) == nullptr && dynamic_cast<zeq::ui::BandCard*> (ch) == nullptr
                && dynamic_cast<zeq::ui::ShortcutsOverlay*> (ch) == nullptr && dynamic_cast<juce::Label*> (ch) == nullptr)
                controls.push_back (ch);
        for (auto* ch : controls)
            check (root.getLocalBounds().contains (ch->getBounds()), c + ": '" + ch->getName() + "' queda fuera");
        for (size_t i = 0; i < controls.size(); ++i)
            for (size_t j = i + 1; j < controls.size(); ++j)
                check (! controls[i]->getBounds().intersects (controls[j]->getBounds()),
                       c + ": se pisan '" + controls[i]->getName() + "' y '" + controls[j]->getName() + "'");
    }
    ed.setUiScale (1.0f, false);
    ed.setSize (ZuenaEqEditor::kBaseW, ZuenaEqEditor::kBaseH);
    ed.setUiScale (initial, false);
    s.pump();
}

// ------------------------------------------------------------------------------------------- rendimiento

// Contador de redibujos del gráfico; las versiones sin él (antes de 0.3.4) redibujaban en cada fotograma.
template <typename G>
auto tickRepaints (G& g, int) -> decltype ((long long) g.getTickRepaints()) { return (long long) g.getTickRepaints(); }
template <typename G>
long long tickRepaints (G&, long) { return -1; }

/** Mide el costo real: un fotograma de interfaz (frame + pintado completo) y el audio por bloque. */
int cmdBench()
{
    Session s;
    // Escenario exigente pero realista: 12 bandas, cortes de 48/96 dB/oct, varias dinámicas, ruido rosa.
    for (int i = 0; i < 12; ++i)
    {
        const auto type = i == 0 ? zeq::FilterType::LowCut : i == 11 ? zeq::FilterType::HighCut : (i % 3 == 0 ? zeq::FilterType::LowShelf : zeq::FilterType::Bell);
        s.set (i, P::Type, (float) type);
        s.set (i, P::Freq, 30.0f * std::pow (1.8f, (float) i));
        s.set (i, P::Gain, (i % 2 == 0 ? 3.0f : -4.0f));
        s.set (i, P::Q, 1.2f);
        s.set (i, P::Slope, i == 0 || i == 11 ? 7.0f : 3.0f);
        s.set (i, P::DynOn, i % 4 == 1 ? 1.0f : 0.0f);
        s.set (i, P::Used, 1.0f);
    }
    s.open();

    std::mt19937 rng (7);
    std::normal_distribution<float> n (0.0f, 0.1f);
    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;
    auto fill = [&]
    {
        for (int i = 0; i < 512; ++i)
        {
            const float v = n (rng);
            buffer.setSample (0, i, v);
            buffer.setSample (1, i, v * 0.9f);
        }
    };

    // Audio: segundos de proceso por segundo de audio (factor de tiempo real), en los dos modos de fase.
    for (const float mode : { 0.0f, 1.0f })
    {
        if (auto* qm = s.proc.getState().getParameter (zeq::params::qualityModeId))
            qm->setValueNotifyingHost (mode);
        for (int k = 0; k < 40; ++k) { fill(); s.proc.processBlock (buffer, midi); }
        const int blocks = 2000; // ≈ 21 s de audio a 48 kHz
        const double t0 = juce::Time::getMillisecondCounterHiRes();
        for (int k = 0; k < blocks; ++k) { fill(); s.proc.processBlock (buffer, midi); }
        const double ms = juce::Time::getMillisecondCounterHiRes() - t0;
        const double audioMs = blocks * 512.0 / 48.0;
        std::cout << "Audio " << (mode > 0.5f ? "Fase natural" : "Latencia cero") << ": "
                  << juce::String (100.0 * ms / audioMs, 2) << " % de un núcleo ("
                  << juce::String (ms * 1000.0 / blocks, 1) << " us por bloque de 512)" << std::endl;
    }

    // Interfaz: frame() + pintado completo de la ventana, con audio corriendo y una banda seleccionada.
    for (auto* child : s.editor->getContentRoot().getChildren())
        if (auto* chip = dynamic_cast<zeq::ui::BandChip*> (child); chip != nullptr && chip->getName() == "Banda 2")
            chip->triggerClick();
    juce::Image canvas (juce::Image::ARGB, s.editor->getWidth(), s.editor->getHeight(), true);
    auto frameOnce = [&] (bool withAudio)
    {
        if (withAudio)
            for (int k = 0; k < 2; ++k) { fill(); s.proc.processBlock (buffer, midi); }
        s.editor->frame();
        // Lo que se repinta en cada fotograma real: la zona del gráfico (con el fondo y la ficha que la tapan).
        juce::Graphics g (canvas);
        if (auto* graph = findFirst<zeq::ui::EqGraph> (s.editor->getContentRoot()))
            g.reduceClipRegion (s.editor->getLocalArea (graph, graph->getLocalBounds()));
        s.editor->paintEntireComponent (g, true);
    };
    for (int k = 0; k < 30; ++k) frameOnce (true);
    for (const bool withAudio : { true, false })
    {
        const int frames = 240;
        double frameMs = 0.0;
        for (int k = 0; k < frames; ++k)
        {
            const double t0 = juce::Time::getMillisecondCounterHiRes();
            frameOnce (withAudio);
            frameMs += juce::Time::getMillisecondCounterHiRes() - t0;
            if (withAudio) { fill(); s.proc.processBlock (buffer, midi); }
        }
        if (withAudio)
        {
            // Desglose: lógica del fotograma, gráfico (curvas + espectro) y resto de la ventana.
            auto* graph = findFirst<zeq::ui::EqGraph> (s.editor->getContentRoot());
            juce::Image gImg (juce::Image::ARGB, graph->getWidth(), graph->getHeight(), true);
            double tFrame = 0, tGraph = 0;
            for (int k = 0; k < 120; ++k)
            {
                fill(); s.proc.processBlock (buffer, midi);
                double t0 = juce::Time::getMillisecondCounterHiRes();
                s.editor->frame();
                tFrame += juce::Time::getMillisecondCounterHiRes() - t0;
                t0 = juce::Time::getMillisecondCounterHiRes();
                juce::Graphics gg (gImg);
                graph->paintEntireComponent (gg, true);
                tGraph += juce::Time::getMillisecondCounterHiRes() - t0;
            }
            std::cout << "  desglose: frame() " << juce::String (tFrame / 120, 2) << " ms, pintar gráfico "
                      << juce::String (tGraph / 120, 2) << " ms" << std::endl;
        }
        const double per = frameMs / frames;
        std::cout << "Interfaz " << (withAudio ? "con audio" : "sin audio (quieta)") << ": " << juce::String (per, 2)
                  << " ms por fotograma -> " << juce::String (per * 60.0 / 10.0, 1) << " % de un núcleo a 60 fps, "
                  << juce::String (per * 144.0 / 10.0, 1) << " % a 144 fps" << std::endl;
    }

    // Reposo: sin audio ni mouse, ¿cuántos fotogramas piden redibujar? (antes: todos)
    if (auto* graph = findFirst<zeq::ui::EqGraph> (s.editor->getContentRoot()))
    {
        for (int k = 0; k < 60; ++k) { s.editor->frame(); juce::Thread::sleep (16); } // asienta animaciones
        const auto before = tickRepaints (*graph, 0);
        for (int k = 0; k < 120; ++k) { s.editor->frame(); juce::Thread::sleep (16); }
        if (before < 0)
            std::cout << "Reposo: 120 de 120 fotogramas redibujan el gráfico (versión sin control de redibujo)" << std::endl;
        else
            std::cout << "Reposo: " << (tickRepaints (*graph, 0) - before) << " de 120 fotogramas redibujan el gráfico" << std::endl;
    }
    return 0;
}

int cmdAudit()
{
    Session s;
    s.open();
    std::mt19937 rng (5);
    std::set<juce::String> covered;

    // EB-28: al abrir sin banda seleccionada la ficha flotante no debe verse (antes salía vacía).
    if (auto* card = findFirst<zeq::ui::BandCard> (*s.editor))
        check (! card->isVisible() && card->getBand() < 0, "EB-28 Al abrir: la ficha de banda se ve sin banda seleccionada");

    auditScale (s);
    auditGraph (s, covered);
    auditHeader (s, covered, rng);

    for (int b = 0; b < zeq::kMaxBands; ++b)
    {
        // Configuración A: Bell con dinámica (todos los controles de ganancia y dinámica visibles).
        s.set (b, P::Type, (float) zeq::FilterType::Bell);
        s.set (b, P::Freq, 40.0f * (float) (b + 1));
        s.set (b, P::Gain, 3.0f);
        s.set (b, P::Q, 1.0f);
        s.set (b, P::DynOn, 1.0f);
        s.set (b, P::Used, 1.0f);
        s.pump();
        auditCard (s, b, "Bell + dinámica", covered, rng);

        // Configuración B: corte de 12 dB/oct (pendiente visible).
        s.set (b, P::Type, (float) zeq::FilterType::LowCut);
        s.set (b, P::Slope, 1.0f);
        s.set (b, P::DynOn, 0.0f);
        s.pump();
        auditCard (s, b, "Low Cut", covered, rng);
    }

    // EB-02: todos los parámetros del plugin tienen al menos un control que los cambia.
    juce::StringArray missing;
    int total = 0;
    for (auto* param : s.proc.getParameters())
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (param))
        {
            ++total;
            if (covered.count (r->getParameterID()) == 0)
                missing.add (r->getParameterID());
        }
    check (missing.isEmpty(), "EB-02 Parámetros sin control en la UI: " + missing.joinIntoString (", "));

    std::cout << "Parámetros con control: " << (total - missing.size()) << " / " << total << std::endl;
    std::cout << "Comprobaciones: " << checks << ", fallas: " << failures << std::endl;
    std::cout << (failures == 0 ? "AUDITORÍA UI OK" : "AUDITORÍA UI CON FALLAS") << std::endl;
    return failures == 0 ? 0 : 1;
}
} // namespace

int main (int argc, char* argv[])
{
    // Si la herramienta se cuelga, imprime la pila de llamadas (con símbolos del .pdb junto al .exe).
    juce::SystemStats::setApplicationCrashHandler ([] (void*)
    {
        std::cerr << "\nCUELGUE - pila de llamadas:\n" << juce::SystemStats::getStackBacktrace() << std::endl;
        std::cerr.flush();
    });
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::String arg = argc > 1 ? juce::String (argv[1]) : juce::String();
    if (arg == "audit")
        return cmdAudit();
    if (arg == "bench")
        return cmdBench();
    if (arg.isNotEmpty())
        return cmdSnapshot (juce::File (arg));
    std::cout << "Uso: zeq_snapshot <salida.png> | audit" << std::endl;
    return 2;
}
