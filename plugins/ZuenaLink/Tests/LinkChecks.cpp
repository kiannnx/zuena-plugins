// link_qa: verificación del plugin ZUENA Link como lo usa un DAW.
//   link_qa host <ruta.vst3>   carga el VST3 real: audio intacto (bit a bit), latencia 0, 32/64 bits,
//                              estado guardado, y el audio llega a un receptor igual al de OBS.
//   link_qa editor [carpeta]   auditoría de la interfaz: cada control cambia lo que dice; capturas.
#include "../Source/Processor.h"
#include "../Source/UI/Editor.h"
#include "LinkReceiver.h"
#include <atomic>
#include <cmath>
#include <iostream>
#include <numbers>
#include <thread>

namespace
{
void require(bool value, const char *message)
{
    if (!value)
        throw std::runtime_error(message);
}
void loop(int ms)
{
    juce::MessageManager::getInstance()->runDispatchLoopUntil(ms);
}
juce::AudioProcessorParameter &param(juce::AudioProcessor &plugin, const char *name)
{
    for (auto *p : plugin.getParameters())
        if (p->getName(64) == zlink::utf(name))
            return *p;
    throw std::runtime_error(name);
}
constexpr std::uint32_t kChannel = 7; // Link 8
/** El nivel de envío es lineal en dB entre −60 y +12. */
float sendNormalized(float db)
{
    return (db - zlink::kSendMinDb) / (zlink::kSendMaxDb - zlink::kSendMinDb);
}

/** Procesa `seconds` de un seno a ritmo de tiempo real; verifica que la salida sea igual a la entrada. */
template <typename Sample>
void play(juce::AudioProcessor &p, double rate, int block, double seconds, double freq = 997)
{
    const int channels = p.getTotalNumInputChannels();
    juce::AudioBuffer<Sample> buffer(channels, block), original(channels, block);
    juce::MidiBuffer midi;
    double phase = 0;
    const auto start = std::chrono::steady_clock::now();
    const auto blocks = static_cast<int>(seconds * rate / block);
    for (int b = 0; b < blocks; ++b)
    {
        for (int i = 0; i < block; ++i)
        {
            const auto v = static_cast<Sample>(0.5 * std::sin(phase));
            phase += 2 * std::numbers::pi * freq / rate;
            for (int ch = 0; ch < channels; ++ch)
                buffer.setSample(ch, i, ch == 0 ? v : static_cast<Sample>(-v));
        }
        original.makeCopyOf(buffer, true);
        p.processBlock(buffer, midi);
        for (int ch = 0; ch < channels; ++ch)
            for (int i = 0; i < block; ++i)
                require(buffer.getSample(ch, i) == original.getSample(ch, i), "la pista sale igual que entra (bit a bit)");
        std::this_thread::sleep_until(start + std::chrono::duration<double>((b + 1) * block / rate));
    }
}

std::unique_ptr<juce::AudioPluginInstance> loadPlugin(juce::AudioPluginFormatManager &manager, const juce::String &path)
{
    juce::OwnedArray<juce::PluginDescription> types;
    for (auto *f : manager.getFormats())
        if (f->getName() == "VST3")
            f->findAllTypesForFile(types, path);
    require(types.size() == 1, "VST3 encontrado");
    juce::String error;
    auto p = manager.createPluginInstance(*types[0], 48000, 512, error);
    require(p != nullptr, "VST3 instanciado");
    require(p->getName() == "ZUENA Link", "nombre del producto");
    return p;
}

void host(const juce::String &path)
{
    juce::AudioPluginFormatManager manager;
    juce::addDefaultFormatsToManager(manager);
    auto p = loadPlugin(manager, path);
    require(p->getParameters().size() == 5, "5 parámetros públicos");
    param(*p, "Canal").setValueNotifyingHost(1.0f); // Link 8
    param(*p, "Enviar a OBS").setValueNotifyingHost(1.0f);

    zlink::LinkReceiver rx;
    std::atomic<std::uint64_t> frames{0};
    std::atomic<float> peak{0};
    rx.start(kChannel, zlink::LinkReceiver::Buffer::normal, 48000, nullptr,
             [&](const float *data, std::uint32_t n, std::uint64_t)
             {
                 frames += n;
                 float m = 0;
                 for (std::uint32_t i = 0; i < n * 2; ++i)
                     m = std::max(m, std::abs(data[i]));
                 peak = std::max(peak.load(), m);
             });

    for (const int channels : {1, 2})
        for (const int rate : {44100, 48000, 96000})
        {
            auto layout = p->getBusesLayout();
            const auto bus = channels == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo();
            layout.inputBuses.set(0, bus);
            layout.outputBuses.set(0, bus);
            require(p->setBusesLayout(layout), "bus mono/estéreo");
            const int block = channels == 1 ? 37 : 512;
            p->setRateAndBufferSizeDetails(rate, block);
            p->prepareToPlay(rate, block);
            peak = 0;
            play<float>(*p, rate, block, 1.5);
            require(p->getLatencySamples() == 0, "latencia informada 0");
            const auto st = rx.status();
            std::cout << channels << " canal(es) a " << rate << " Hz: receptor " << static_cast<int>(st.phase) << ", "
                      << st.latencyMs << " ms, " << st.cuts << " cortes, pico " << peak.load() << "\n";
            require(st.phase == zlink::LinkReceiver::Phase::receiving, "el audio del VST3 llega al receptor de OBS");
            require(static_cast<int>(st.sourceRate) == rate, "el receptor ve la frecuencia del proyecto");
            require(peak.load() > 0.45f && peak.load() < 0.55f, "llega la señal completa (pico 0,5)");
            p->releaseResources();
        }

    // Nivel de envío y mono: cambian lo que llega, no la pista.
    param(*p, "Nivel de envío").setValueNotifyingHost(sendNormalized(-6.0f));
    p->prepareToPlay(48000, 512);
    play<float>(*p, 48000, 512, 1.0);
    peak = 0;
    play<float>(*p, 48000, 512, 1.0);
    require(std::abs(peak.load() - 0.5f * std::pow(10.0f, -6.0f / 20.0f)) < 0.02f, "el nivel de envío baja 6 dB lo enviado");
    param(*p, "Nivel de envío").setValueNotifyingHost(sendNormalized(0.0f));
    param(*p, "Envío mono").setValueNotifyingHost(1.0f);
    play<float>(*p, 48000, 512, 1.0);
    peak = 0;
    play<float>(*p, 48000, 512, 1.0);
    require(peak.load() < 0.01f, "mono suma L y R (aquí están invertidos: se anulan)");
    param(*p, "Envío mono").setValueNotifyingHost(0.0f);

    // Apagado: deja de llegar.
    param(*p, "Enviar a OBS").setValueNotifyingHost(0.0f);
    play<float>(*p, 48000, 512, 1.0);
    loop(300);
    require(rx.status().phase != zlink::LinkReceiver::Phase::receiving, "apagado: el receptor deja de recibir");
    param(*p, "Enviar a OBS").setValueNotifyingHost(1.0f);

    // 64 bits.
    p->releaseResources();
    require(p->supportsDoublePrecisionProcessing(), "64 bits anunciado al host");
    p->setProcessingPrecision(juce::AudioProcessor::doublePrecision);
    p->prepareToPlay(48000, 512);
    peak = 0;
    play<double>(*p, 48000, 512, 1.5);
    require(rx.status().phase == zlink::LinkReceiver::Phase::receiving && peak.load() > 0.45f, "64 bits llega al receptor");
    p->releaseResources();

    // Estado: se guarda y se recupera.
    juce::MemoryBlock saved;
    p->getStateInformation(saved);
    require(saved.getSize() > 50, "estado guardado");
    param(*p, "Canal").setValueNotifyingHost(0.0f);
    p->setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
    loop(50);
    require(param(*p, "Canal").getValue() > 0.99f, "el estado recupera el canal");
    rx.stop();
    std::cout << "VST3 OK: audio intacto en 6 combinaciones, latencia 0, 32/64 bits, envío y mono, apagado, estado y llegada al receptor de OBS.\n";
}

template <typename T> void click(T &b)
{
    b.triggerClick();
    loop(60);
}

void editor(const juce::File &out)
{
    zlink::Processor processor;
    processor.setRateAndBufferSizeDetails(48000, 480);
    processor.prepareToPlay(48000, 480);
    const auto folder = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("zuena-link-qa");
    folder.deleteRecursively();
    processor.engine().setRecording({std::filesystem::path(folder.getFullPathName().toWideCharPointer()), zlink::SampleFormat::pcm24});
    auto ed = std::unique_ptr<zlink::Editor>(static_cast<zlink::Editor *>(processor.createEditor()));
    ed->setVisible(true);

    // Un "DAW" en el hilo de mensajes: bloques de 10 ms.
    juce::AudioBuffer<float> buffer(2, 480);
    juce::MidiBuffer midi;
    const auto pump = [&](int ms)
    {
        for (int t = 0; t < ms; t += 10)
        {
            for (int i = 0; i < 480; ++i)
            {
                buffer.setSample(0, i, 0.3f);
                buffer.setSample(1, i, 0.3f);
            }
            processor.processBlock(buffer, midi);
            loop(10);
        }
    };
    const auto value = [&](zlink::Param id) { return processor.value(id); };

    // Interruptores y combos: cada uno cambia su parámetro.
    require(value(zlink::Param::obs) >= 0.5f, "OBS encendido por defecto");
    click(ed->obsSwitch);
    require(value(zlink::Param::obs) < 0.5f, "el interruptor de OBS apaga");
    pump(200);
    ed->refreshNow();
    require(ed->texts().obs == "Apagado", "estado de OBS apagado");
    click(ed->obsSwitch);
    require(value(zlink::Param::obs) >= 0.5f, "el interruptor de OBS enciende");
    ed->channel.setSelectedId(8);
    loop(30);
    require(juce::roundToInt(value(zlink::Param::channel)) == 7, "el menú de canal elige Link 8");
    pump(300);
    ed->refreshNow();
    require(ed->texts().obs.startsWith(zlink::utf("Esperando a OBS")), "sin OBS: dice que espera y cómo agregar la fuente");
    {
        zlink::LinkReceiver rx;
        rx.start(kChannel, zlink::LinkReceiver::Buffer::normal, 48000, nullptr, [](const float *, std::uint32_t, std::uint64_t) {});
        pump(1500);
        ed->refreshNow();
        std::cout << "OBS: " << ed->texts().obs << "\n";
        require(ed->texts().obs.startsWith(zlink::utf("Conectado a OBS")), "con el receptor: dice conectado, latencia y cortes");
        rx.stop();
    }
    ed->send.setValue(-12.0);
    loop(30);
    require(std::abs(value(zlink::Param::send) + 12.0f) < 0.05f, "el deslizador cambia el nivel de envío");
    click(ed->mono);
    require(value(zlink::Param::mono) >= 0.5f, "el botón Mono activa el envío mono");
    click(ed->mono);
    require(value(zlink::Param::mono) < 0.5f, "el botón Mono lo desactiva");
    click(ed->deviceSwitch);
    require(value(zlink::Param::device) >= 0.5f, "el interruptor de dispositivo enciende");
    ed->refreshNow();
    std::cout << "Dispositivo: " << ed->texts().device << " | " << ed->texts().deviceHint << "\n";
    require(ed->texts().device.isNotEmpty(), "el dispositivo informa su estado");
    if (ed->deviceList.getNumItems() > 0)
    {
        // Elegir la primera salida la configura y la abre (con silencio: el envío está a −12 dB de 0,3 → bajo).
        ed->send.setValue(-60.0);
        ed->deviceList.setSelectedItemIndex(0);
        pump(1500);
        ed->refreshNow();
        std::cout << "Dispositivo elegido: " << ed->texts().device << "\n";
        require(!processor.engine().device().id.empty(), "el menú de salida guarda el dispositivo");
        ed->mode.setSelectedId(2);
        loop(30);
        require(processor.engine().device().exclusive, "el menú de modo pasa a exclusivo");
        ed->mode.setSelectedId(1);
        loop(30);
        require(!processor.engine().device().exclusive, "y vuelve a compartido");
    }
    else
        std::cout << "Sin salidas de audio en este equipo: se omite la elección de dispositivo.\n";
    click(ed->deviceSwitch);
    require(value(zlink::Param::device) < 0.5f, "el interruptor de dispositivo apaga");

    // Grabación: formato, grabar y parar.
    ed->format.setSelectedId(3);
    loop(30);
    require(processor.engine().recording().format == zlink::SampleFormat::float32, "el menú de formato elige 32 bits flotante");
    click(ed->record);
    pump(1200);
    ed->refreshNow();
    std::cout << "Grabación: " << ed->texts().record << "\n";
    require(ed->texts().record.startsWith("Grabando"), "el botón de grabar graba y lo muestra");
    click(ed->record);
    pump(300);
    ed->refreshNow();
    require(ed->texts().record.startsWith(zlink::utf("Último archivo")), "al parar muestra el archivo");
    require(folder.getNumberOfChildFiles(juce::File::findFiles, "*.wav") == 1, "queda un WAV en la carpeta");
    require(ed->folder.onClick != nullptr && ed->reveal.onClick != nullptr, "Carpeta y Abrir tienen acción");

    // Tema y capturas.
    if (out != juce::File{})
    {
        out.createDirectory();
        ed->send.setValue(0.0);
        pump(300);
        ed->refreshNow();
        juce::PNGImageFormat png;
        const auto save = [&](const char *name)
        {
            juce::FileOutputStream stream(out.getChildFile(name));
            stream.setPosition(0);
            stream.truncate();
            png.writeImageToStream(ed->createComponentSnapshot(ed->getLocalBounds(), true, 2.0f), stream);
        };
        save("link-claro.png");
        click(ed->theme);
        require(processor.isDark(), "el botón de tema pasa a oscuro");
        pump(100);
        save("link-oscuro.png");
        click(ed->theme);
        require(!processor.isDark(), "y vuelve a claro");
    }
    ed.reset();
    processor.releaseResources();
    folder.deleteRecursively();
    std::cout << "Editor OK: OBS, canal, envío, mono, dispositivo, modo, formato, grabar, tema y estados en vivo.\n";
}
} // namespace

int main(int argc, char **argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    try
    {
        const juce::String mode = argc > 1 ? argv[1] : "";
        if (mode == "host" && argc > 2)
            host(juce::String::fromUTF8(argv[2]));
        else if (mode == "editor")
            editor(argc > 2 ? juce::File(juce::String::fromUTF8(argv[2])) : juce::File{});
        else
        {
            std::cerr << "uso: link_qa host <ZUENA Link.vst3> | editor [carpeta]\n";
            return 2;
        }
        return 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << "FALLA: " << e.what() << "\n";
        return 1;
    }
}
