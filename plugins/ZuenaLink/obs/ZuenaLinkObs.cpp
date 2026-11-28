// Fuente de audio «ZUENA Link» para OBS Studio (zuena-link.dll).
// Recibe por memoria compartida lo que manda el plugin ZUENA Link desde el DAW, sin cable virtual.
// El receptor (LinkReceiver) entrega audio al ritmo del reloj de OBS, con marcas de tiempo continuas,
// y corrige la deriva de reloj con el mismo motor que la salida a dispositivo.
#include "ObsApi.h"
#include "LinkReceiver.h"
#include <algorithm>
#include <cstdio>
#include <memory>
#include <mutex>
#include <string>

namespace
{
obs_module_t *gModule = nullptr;

std::uint64_t obsClock()
{
    return os_gettime_ns();
}

struct LinkSource
{
    obs_source_t *source = nullptr;
    zlink::LinkReceiver receiver;
    std::uint32_t channel = 0;
    zlink::LinkReceiver::Buffer buffer = zlink::LinkReceiver::Buffer::normal;
    bool started = false;

    void apply(obs_data_t *settings)
    {
        const auto ch = static_cast<std::uint32_t>(std::clamp<long long>(obs_data_get_int(settings, "channel"), 0, zlink::kLinkChannels - 1));
        const auto buf = static_cast<zlink::LinkReceiver::Buffer>(std::clamp<long long>(obs_data_get_int(settings, "buffer"), 0, 2));
        if (started && ch == channel && buf == buffer)
            return;
        channel = ch;
        buffer = buf;
        obs_audio_info info{};
        const std::uint32_t rate = obs_get_audio_info(&info) && info.samples_per_sec >= 8000 ? info.samples_per_sec : 48000;
        obs_source_t *src = source;
        receiver.start(channel, buffer, rate, &obsClock,
                       [src, rate](const float *interleaved, std::uint32_t frames, std::uint64_t ts)
                       {
                           obs_source_audio audio{};
                           audio.data[0] = reinterpret_cast<const std::uint8_t *>(interleaved); // API de OBS: bytes
                           audio.frames = frames;
                           audio.speakers = SPEAKERS_STEREO;
                           audio.format = AUDIO_FORMAT_FLOAT;
                           audio.samples_per_sec = rate;
                           audio.timestamp = ts;
                           obs_source_output_audio(src, &audio);
                       });
        started = true;
        blog(LOG_INFO, "[zuena-link] escuchando Link %u a %u Hz (colchón %.0f ms)", channel + 1, rate,
             zlink::LinkReceiver::bufferMs(buffer));
    }
};

const char *getName(void *)
{
    return "ZUENA Link";
}

void *create(obs_data_t *settings, obs_source_t *source)
{
    auto s = std::make_unique<LinkSource>();
    s->source = source;
    s->apply(settings);
    return s.release();
}

void destroy(void *data)
{
    std::unique_ptr<LinkSource> s(static_cast<LinkSource *>(data));
    s->receiver.stop();
}

void update(void *data, obs_data_t *settings)
{
    static_cast<LinkSource *>(data)->apply(settings);
}

void defaults(obs_data_t *settings)
{
    obs_data_set_default_int(settings, "channel", 0);
    obs_data_set_default_int(settings, "buffer", 1);
}

std::string statusText(const LinkSource *s)
{
    if (s == nullptr)
        return "Elegí el mismo canal que en el plugin ZUENA Link del DAW.";
    const auto st = s->receiver.status();
    char text[256];
    switch (st.phase)
    {
    case zlink::LinkReceiver::Phase::receiving:
        std::snprintf(text, sizeof(text), "Recibiendo de %s · %u Hz · %.1f ms · %u cortes",
                      st.host.empty() ? "ZUENA Link" : st.host.c_str(), st.sourceRate, st.latencyMs, st.cuts);
        return text;
    case zlink::LinkReceiver::Phase::paused:
        std::snprintf(text, sizeof(text), "%s está conectado pero no manda audio (apagado, en bypass o exportando).",
                      st.host.empty() ? "El plugin" : st.host.c_str());
        return text;
    case zlink::LinkReceiver::Phase::failed:
        return "No se pudo abrir el canal compartido.";
    case zlink::LinkReceiver::Phase::idle:
        break;
    }
    std::snprintf(text, sizeof(text), "Esperando: poné ZUENA Link en una pista del DAW y elegí Link %u.", s->channel + 1);
    return text;
}

obs_properties_t *properties(void *data)
{
    auto *s = static_cast<LinkSource *>(data);
    obs_properties_t *props = obs_properties_create();
    obs_property_t *ch = obs_properties_add_list(props, "channel", "Canal", OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
    for (std::uint32_t i = 0; i < zlink::kLinkChannels; ++i)
    {
        const std::string name = "Link " + std::to_string(i + 1);
        obs_property_list_add_int(ch, name.c_str(), i);
    }
    obs_property_set_long_description(ch, "El mismo canal que elegiste en el plugin ZUENA Link del DAW.");
    obs_property_t *buf = obs_properties_add_list(props, "buffer", "Colchón", OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
    obs_property_list_add_int(buf, "Mínimo (5 ms)", 0);
    obs_property_list_add_int(buf, "Normal (10 ms)", 1);
    obs_property_list_add_int(buf, "Seguro (25 ms, PC exigida)", 2);
    obs_property_set_long_description(buf, "Margen además del bloque del DAW. Subilo solo si escuchás cortes.");
    const auto status = statusText(s);
    obs_properties_add_text(props, "status", status.c_str(), OBS_TEXT_INFO);
    return props;
}

obs_source_info makeInfo()
{
    obs_source_info info{};
    info.id = "zuena_link_source";
    info.type = OBS_SOURCE_TYPE_INPUT;
    info.output_flags = OBS_SOURCE_AUDIO;
    info.get_name = &getName;
    info.create = &create;
    info.destroy = &destroy;
    info.get_defaults = &defaults;
    info.get_properties = &properties;
    info.update = &update;
    info.icon_type = OBS_ICON_TYPE_AUDIO_OUTPUT;
    return info;
}
} // namespace

// Funciones que OBS busca en todo módulo (OBS_DECLARE_MODULE de obs-module.h).
extern "C"
{
    __declspec(dllexport) void obs_module_set_pointer(obs_module_t *module)
    {
        gModule = module;
    }
    __declspec(dllexport) obs_module_t *obs_current_module(void)
    {
        return gModule;
    }
    __declspec(dllexport) std::uint32_t obs_module_ver(void)
    {
        return (30u << 24) | (0u << 16) | 0u; // API mínima que usamos: OBS 30.0 en adelante
    }
    __declspec(dllexport) const char *obs_module_name(void)
    {
        return "ZUENA Link";
    }
    __declspec(dllexport) const char *obs_module_description(void)
    {
        return "Audio del DAW en OBS por memoria compartida, sin cable virtual (ZUENA Plugins).";
    }
    __declspec(dllexport) bool obs_module_load(void)
    {
        static const obs_source_info info = makeInfo();
        obs_register_source_s(&info, kSourceInfoSize);
        blog(LOG_INFO, "[zuena-link] fuente registrada");
        return true;
    }
}
