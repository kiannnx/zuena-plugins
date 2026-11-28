// link_obs_sim: carga la zuena-link.dll real como lo hace OBS, contra un obs.dll de prueba (FakeObs.cpp),
// y verifica de punta a punta: funciones del módulo, registro de la fuente (estructura y tamaño),
// valores por defecto, propiedades, audio entregado (formato, nivel, marcas de tiempo) y cambio de canal.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "FakeObsState.h"
#include "LinkEngine.h"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <numbers>
#include <stdexcept>
#include <string>
#include <thread>

extern "C" __declspec(dllimport) FakeObsState *fake_obs_state();

namespace
{
void require(bool ok, const char *what)
{
    if (!ok)
        throw std::runtime_error(what);
}

std::string statusText(FakeObsState &st)
{
    std::scoped_lock lock(st.mutex);
    for (const auto &p : st.properties)
        if (p.name == "status")
            return p.description;
    return {};
}

/** Un DAW mínimo: bloques de 512 a 48 kHz, a ritmo real, con un seno de 0,5. */
class Daw
{
  public:
    explicit Daw(zlink::LinkEngine &e, std::uint32_t channel) : engine(e)
    {
        controls.channel = channel;
        engine.prepare(48000, 512);
        thread = std::thread([this] { run(); });
    }
    ~Daw()
    {
        quit = true;
        thread.join();
    }

  private:
    zlink::LinkEngine &engine;
    zlink::LinkControls controls;
    std::atomic<bool> quit{false};
    std::thread thread;
    void run()
    {
        HANDLE timer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
        float l[512], r[512];
        double phase = 0;
        const auto start = std::chrono::steady_clock::now();
        for (std::uint64_t b = 0; !quit; ++b)
        {
            for (int i = 0; i < 512; ++i)
            {
                l[i] = static_cast<float>(0.5 * std::sin(phase));
                r[i] = l[i];
                phase += 2 * std::numbers::pi * 440 / 48000;
            }
            engine.process(l, r, controls, false);
            // Temporizador de alta resolución: sleep_until tiene pasos de 15,6 ms en Windows.
            const auto wait = start + std::chrono::duration<double>((b + 1) * 512 / 48000.0) - std::chrono::steady_clock::now();
            if (wait > std::chrono::microseconds(0))
            {
                LARGE_INTEGER due;
                due.QuadPart = -static_cast<LONGLONG>(std::chrono::duration_cast<std::chrono::nanoseconds>(wait).count() / 100);
                SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE);
                WaitForSingleObject(timer, 1000);
            }
        }
        CloseHandle(timer);
    }
};
} // namespace

int main()
{
    try
    {
        FakeObsState &st = *fake_obs_state();
        wchar_t self[MAX_PATH];
        GetModuleFileNameW(nullptr, self, MAX_PATH);
        std::wstring dir(self);
        dir.resize(dir.find_last_of(L'\\') + 1);
        HMODULE module = LoadLibraryW((dir + L"zuena-link.dll").c_str());
        require(module != nullptr, "zuena-link.dll se carga y resuelve sus importaciones de obs.dll");

        using SetPointer = void (*)(obs_module_t *);
        using Current = obs_module_t *(*)();
        using Ver = std::uint32_t (*)();
        using Load = bool (*)();
        using Text = const char *(*)();
        const auto setPointer = reinterpret_cast<SetPointer>(GetProcAddress(module, "obs_module_set_pointer"));
        const auto current = reinterpret_cast<Current>(GetProcAddress(module, "obs_current_module"));
        const auto ver = reinterpret_cast<Ver>(GetProcAddress(module, "obs_module_ver"));
        const auto load = reinterpret_cast<Load>(GetProcAddress(module, "obs_module_load"));
        const auto name = reinterpret_cast<Text>(GetProcAddress(module, "obs_module_name"));
        const auto description = reinterpret_cast<Text>(GetProcAddress(module, "obs_module_description"));
        require(setPointer && current && ver && load && name && description, "exporta las funciones que OBS busca");
        // OBS rechaza un módulo compilado con una API más nueva que la suya: la nuestra es 30.0.
        require((ver() >> 24) == 30, "versión de API 30");
        auto *fakeModule = reinterpret_cast<obs_module_t *>(static_cast<std::uintptr_t>(0x1234));
        setPointer(fakeModule);
        require(current() == fakeModule, "obs_current_module devuelve el puntero de OBS");
        require(std::string(name()) == "ZUENA Link", "nombre del módulo");
        require(load(), "obs_module_load");

        obs_source_info info{};
        {
            std::scoped_lock lock(st.mutex);
            require(st.registrations == 1, "registra una fuente");
            require(st.registeredSize == kSourceInfoSize, "tamaño registrado hasta icon_type (compatible con OBS 26+)");
            info = st.info;
        }
        require(std::string(info.id) == "zuena_link_source", "id de la fuente");
        require(info.type == OBS_SOURCE_TYPE_INPUT && (info.output_flags & OBS_SOURCE_AUDIO), "fuente de entrada de audio");
        require(info.get_name && info.create && info.destroy && info.get_defaults && info.get_properties && info.update,
                "funciones de la fuente");
        require(std::string(info.get_name(nullptr)) == "ZUENA Link", "nombre visible");
        require(info.icon_type == OBS_ICON_TYPE_AUDIO_OUTPUT, "ícono de audio");

        FakeData settings;
        info.get_defaults(reinterpret_cast<obs_data_t *>(&settings));
        require(settings.channel == 0 && settings.buffer == 1, "por defecto: Link 1, colchón normal");
        settings.channel = 7; // Link 8
        auto *source = reinterpret_cast<obs_source_t *>(static_cast<std::uintptr_t>(0x5555));
        st.expectedSource = source;
        void *data = info.create(reinterpret_cast<obs_data_t *>(&settings), source);
        require(data != nullptr, "crea la fuente");

        info.get_properties(data);
        {
            std::scoped_lock lock(st.mutex);
            require(st.properties.size() == 3, "propiedades: canal, colchón y estado");
            require(st.properties[0].name == "channel" && st.properties[0].wellFormed && st.properties[0].items.size() == 8,
                    "canal: lista de Link 1 a Link 8");
            require(st.properties[0].items[7].first == "Link 8" && st.properties[0].items[7].second == 7, "Link 8 = 7");
            require(st.properties[1].name == "buffer" && st.properties[1].items.size() == 3, "colchón: tres opciones");
            require(!st.properties[0].longDescription.empty(), "el canal explica qué elegir");
        }
        require(statusText(st).rfind("Esperando", 0) == 0, "sin DAW: dice que espera y qué hacer");

        {
            zlink::LinkEngine engine;
            Daw daw(engine, 7);
            std::this_thread::sleep_for(std::chrono::milliseconds(800));
            std::uint64_t startFrames;
            {
                std::scoped_lock lock(st.mutex);
                startFrames = st.frames;
                st.peak = 0;
            }
            std::this_thread::sleep_for(std::chrono::seconds(3));
            std::uint64_t frames, bad, regressions;
            float peak;
            {
                std::scoped_lock lock(st.mutex);
                frames = st.frames - startFrames;
                bad = st.badCalls;
                regressions = st.timestampRegressions;
                peak = st.peak;
            }
            info.get_properties(data);
            const auto status = statusText(st);
            std::printf("audio: %llu cuadros en 3 s, pico %.3f, estado «%s»\n", static_cast<unsigned long long>(frames), peak,
                        status.c_str());
            require(bad == 0, "formato pedido: float estéreo intercalado a 48 kHz");
            require(regressions == 0, "marcas de tiempo continuas");
            require(frames > 3 * 48000 * 0.95 && frames < 3 * 48000 * 1.05, "entrega 48 000 cuadros por segundo");
            require(peak > 0.48f && peak < 0.52f, "llega la señal completa");
            require(status.rfind("Recibiendo de", 0) == 0, "el estado dice de dónde recibe, latencia y cortes");
            require(status.find("0 cortes") != std::string::npos, "sin cortes");
            require(engine.snapshot().obs == zlink::ObsState::connected, "el plugin ve a OBS conectado");

            // Cambiar de canal en OBS: deja de recibir de Link 8.
            settings.channel = 6;
            info.update(data, reinterpret_cast<obs_data_t *>(&settings));
            std::this_thread::sleep_for(std::chrono::milliseconds(1800));
            info.get_properties(data);
            require(statusText(st).rfind("Esperando", 0) == 0, "en otro canal vuelve a esperar");
            require(engine.snapshot().obs == zlink::ObsState::waiting, "el plugin ve que OBS ya no lo escucha");
        }
        info.destroy(data);
        FreeLibrary(module);
        std::printf("OBS (simulado) OK: módulo, registro, propiedades, audio a 48 kHz sin cortes y cambio de canal.\n");
        return 0;
    }
    catch (const std::exception &e)
    {
        std::fprintf(stderr, "FALLA: %s\n", e.what());
        return 1;
    }
}
