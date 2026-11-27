// zeq_host — host VST3 mínimo para verificar plugins (no se distribuye).
//
//   zeq_host validate <plugin.vst3>           Carga el VST3 compilado como un DAW y verifica comportamiento y estado.
//   zeq_host params <plugin.vst3> [salida]    Lista los parámetros públicos de un VST3 (nombre, valor y texto).
//   zeq_host response <plugin.vst3> <ajustes.txt> <salida.csv>
//                                              Mide la respuesta en frecuencia de cualquier VST3 (caja negra):
//                                              cada línea de ajustes es "Nombre del parámetro=texto o valor normalizado".

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <cmath>
#include <iostream>
#include <numbers>
#include <random>

namespace
{
int failures = 0;

void check (bool ok, const juce::String& what)
{
    std::cout << (ok ? "  OK    " : "  FALLA ") << what << std::endl;
    if (! ok)
        ++failures;
}

std::unique_ptr<juce::AudioPluginInstance> loadPlugin (const juce::String& path, double sr, int block)
{
    juce::AudioPluginFormatManager manager;
    juce::addDefaultFormatsToManager (manager);
    juce::OwnedArray<juce::PluginDescription> types;
    for (auto* format : manager.getFormats())
        if (format->getName() == "VST3")
            format->findAllTypesForFile (types, path);
    if (types.isEmpty())
    {
        std::cout << "No se encontró ningún plugin VST3 en " << path << std::endl;
        return nullptr;
    }
    juce::String error;
    auto instance = manager.createPluginInstance (*types[0], sr, block, error);
    if (instance == nullptr)
        std::cout << "No se pudo cargar: " << error << std::endl;
    return instance;
}

juce::AudioProcessorParameter* findParam (juce::AudioProcessor& p, const juce::String& name)
{
    for (auto* param : p.getParameters())
        if (param->getName (256) == name)
            return param;
    return nullptr;
}

bool applySetting (juce::AudioProcessor& p, const juce::String& line)
{
    const auto name = line.upToFirstOccurrenceOf ("=", false, false).trim();
    const auto value = line.fromFirstOccurrenceOf ("=", false, false).trim();
    auto* param = findParam (p, name);
    if (param == nullptr)
    {
        std::cout << "Parámetro no encontrado: " << name << std::endl;
        return false;
    }
    const bool numeric = value.containsOnly ("0123456789.-") && value.isNotEmpty();
    const float v = numeric ? value.getFloatValue() : param->getValueForText (value);
    param->setValueNotifyingHost (v);
    std::cout << "  " << name << " = " << param->getText (param->getValue(), 64) << std::endl;
    return true;
}

/** Respuesta en frecuencia por impulso (DFT directa en las frecuencias pedidas). */
std::vector<double> measureResponse (juce::AudioProcessor& p, double sr, const std::vector<double>& freqs, int length = 1 << 15)
{
    const int block = 512;
    juce::AudioBuffer<float> buffer (2, block);
    juce::MidiBuffer midi;
    std::vector<double> out ((size_t) length);
    // Primero, unos bloques de silencio para que se asienten los parámetros suavizados.
    for (int i = 0; i < 64; ++i)
    {
        buffer.clear();
        p.processBlock (buffer, midi);
    }
    for (int start = 0; start < length; start += block)
    {
        buffer.clear();
        if (start == 0)
        {
            buffer.setSample (0, 0, 1.0f);
            buffer.setSample (1, 0, 1.0f);
        }
        p.processBlock (buffer, midi);
        for (int i = 0; i < block && start + i < length; ++i)
            out[(size_t) (start + i)] = buffer.getSample (0, i);
    }
    std::vector<double> db;
    for (double f : freqs)
    {
        const double w = 2.0 * std::numbers::pi * f / sr;
        double re = 0.0, im = 0.0;
        for (int n = 0; n < length; ++n)
        {
            re += out[(size_t) n] * std::cos (w * n);
            im -= out[(size_t) n] * std::sin (w * n);
        }
        db.push_back (20.0 * std::log10 (std::max (std::sqrt (re * re + im * im), 1.0e-12)));
    }
    return db;
}

// ------------------------------------------------------------------------------------------------

int cmdValidate (const juce::String& path)
{
    const double sr = 48000.0;
    std::cout << "Validando " << path << std::endl;
    auto plugin = loadPlugin (path, sr, 512);
    if (plugin == nullptr)
        return 1;
    plugin->enableAllBuses();
    plugin->prepareToPlay (sr, 512);

    check (plugin->getName() == "ZUENA EQ", "Nombre: " + plugin->getName());
    check (plugin->getLatencySamples() == 0, "Latencia cero");
    // 24 bandas x 15 + 5 globales (salida, escala, modo de fase, auto-ganancia, bypass global) = 365, más el bypass del wrapper VST3.
    check (plugin->getParameters().size() == 24 * 15 + 5 + 1, "Parámetros: " + juce::String (plugin->getParameters().size()) + " (365 + bypass del host)");
    check (plugin->getTotalNumInputChannels() == 2 && plugin->getTotalNumOutputChannels() == 2, "Estéreo de entrada y salida");

    const std::vector<double> freqs { 50.0, 200.0, 1000.0, 5000.0, 15000.0 };
    const auto flat = measureResponse (*plugin, sr, freqs);
    bool transparent = true;
    for (double d : flat)
        transparent = transparent && std::abs (d) < 0.01;
    check (transparent, "Sin bandas: respuesta plana (±0.01 dB)");

    for (const char* s : { "Banda 1 activa=1", "Banda 1 frecuencia=0.5752", "Banda 1 ganancia=0.6", "Banda 1 Q=0.5" })
        applySetting (*plugin, s);
    const auto bell = measureResponse (*plugin, sr, { 1000.0, 20.0 });
    check (std::abs (bell[0] - 6.0) < 0.15, "Bell +6 dB a 1 kHz medida en el host: " + juce::String (bell[0], 2) + " dB");
    check (std::abs (bell[1]) < 0.1, "Bell: 20 Hz sin cambio: " + juce::String (bell[1], 2) + " dB");

    juce::MemoryBlock state;
    plugin->getStateInformation (state);
    auto copy = loadPlugin (path, sr, 512);
    copy->prepareToPlay (sr, 512);
    copy->setStateInformation (state.getData(), (int) state.getSize());
    bool same = true;
    for (int i = 0; i < plugin->getParameters().size(); ++i)
        same = same && std::abs (plugin->getParameters()[i]->getValue() - copy->getParameters()[i]->getValue()) < 1.0e-6f;
    check (same, "Guardar y cargar estado conserva los 365 parámetros");

    {
        std::unique_ptr<juce::AudioProcessorEditor> ed (plugin->createEditorAndMakeActive());
        // Desde 0.3.3 el editor elige su tamaño según la pantalla (70–100 % de 1360×780): mínimo 1320×700 al 70 %.
        check (ed != nullptr && ed->getWidth() >= 924 && ed->getHeight() >= 490, "El editor se crea ("+ (ed ? juce::String (ed->getWidth()) + "x" + juce::String (ed->getHeight()) : "nulo") + ")");
        juce::MessageManager::getInstance()->runDispatchLoopUntil (200);
    }

    std::mt19937 rng (11);
    std::uniform_real_distribution<float> u (0.0f, 1.0f);
    juce::AudioBuffer<float> buffer (2, 256);
    juce::MidiBuffer midi;
    bool finite = true;
    float peak = 0.0f;
    for (int block = 0; block < 2000 && finite; ++block)
    {
        for (int k = 0; k < 20; ++k)
            plugin->getParameters()[(int) (u (rng) * 360.0f)]->setValue (u (rng));
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < 256; ++i)
                buffer.setSample (c, i, (u (rng) - 0.5f) * 0.2f);
        plugin->processBlock (buffer, midi);
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < 256; ++i)
            {
                finite = finite && std::isfinite (buffer.getSample (c, i));
                peak = std::max (peak, std::abs (buffer.getSample (c, i)));
            }
    }
    check (finite, "Automatización aleatoria (2000 bloques, 20 parámetros por bloque): sin NaN/Inf. Pico " + juce::String (peak, 2));

    plugin->releaseResources();
    std::cout << (failures == 0 ? "VALIDACIÓN OK" : "VALIDACIÓN CON FALLAS: " + juce::String (failures)) << std::endl;
    return failures == 0 ? 0 : 1;
}

int cmdParams (const juce::String& path, const juce::String& outPath)
{
    auto plugin = loadPlugin (path, 48000.0, 512);
    if (plugin == nullptr)
        return 1;
    juce::String text;
    text << plugin->getName() << " — " << plugin->getParameters().size() << " parámetros\n";
    for (auto* p : plugin->getParameters())
        text << p->getParameterIndex() << "\t" << p->getName (256) << "\t" << juce::String (p->getValue(), 6) << "\t" << p->getText (p->getValue(), 64)
             << "\t" << p->getLabel() << "\t pasos=" << p->getNumSteps() << "\n";
    if (outPath.isNotEmpty())
        juce::File (outPath).replaceWithText (text);
    else
        std::cout << text;
    return 0;
}

int cmdResponse (const juce::String& path, const juce::File& settings, const juce::File& csv)
{
    const double sr = 48000.0;
    auto plugin = loadPlugin (path, sr, 512);
    if (plugin == nullptr)
        return 1;
    plugin->enableAllBuses();
    plugin->prepareToPlay (sr, 512);
    for (const auto& line : juce::StringArray::fromLines (settings.loadFileAsString()))
        if (line.trim().isNotEmpty() && ! line.startsWith ("#"))
            if (! applySetting (*plugin, line))
                return 1;
    std::vector<double> freqs;
    for (int i = 0; i <= 240; ++i)
        freqs.push_back (10.0 * std::pow (2200.0, i / 240.0));
    const auto db = measureResponse (*plugin, sr, freqs);
    juce::String text ("hz,db\n");
    for (size_t i = 0; i < freqs.size(); ++i)
        text << juce::String (freqs[i], 2) << "," << juce::String (db[i], 4) << "\n";
    csv.replaceWithText (text);
    std::cout << "Respuesta guardada en " << csv.getFullPathName() << std::endl;
    plugin->releaseResources();
    return 0;
}
} // namespace

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::StringArray args (argv + 1, argc - 1);
    const auto cmd = args[0];
    if (cmd == "validate" && args.size() >= 2)
        return cmdValidate (args[1]);
    if (cmd == "params" && args.size() >= 2)
        return cmdParams (args[1], args[2]);
    if (cmd == "response" && args.size() >= 4)
        return cmdResponse (args[1], juce::File (args[2]), juce::File (args[3]));
    std::cout << "Uso: zeq_host validate <vst3> | params <vst3> [txt] | response <vst3> <ajustes.txt> <csv>" << std::endl;
    return 2;
}
