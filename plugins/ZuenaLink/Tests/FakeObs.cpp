// obs.dll de prueba: exporta las mismas 12 funciones de libobs que usa zuena-link.dll y registra lo que
// recibe. Permite cargar la fuente real como lo hace OBS (LoadLibrary + obs_module_load) en el CI, donde
// no hay OBS ni placa de video. Solo para pruebas: nunca se distribuye.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#define ZLINK_OBS_API __declspec(dllexport)
#include "FakeObsState.h"
#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace
{
FakeObsState state;
}

extern "C"
{
    __declspec(dllexport) FakeObsState *fake_obs_state()
    {
        return &state;
    }

    __declspec(dllexport) void obs_register_source_s(const obs_source_info *info, std::size_t size)
    {
        std::scoped_lock lock(state.mutex);
        state.registeredSize = size;
        std::memset(&state.info, 0, sizeof(state.info));
        std::memcpy(&state.info, info, std::min(size, sizeof(state.info)));
        state.registrations++;
    }

    __declspec(dllexport) void obs_source_output_audio(obs_source_t *source, const obs_source_audio *audio)
    {
        std::scoped_lock lock(state.mutex);
        if (source != state.expectedSource || audio == nullptr || audio->data[0] == nullptr)
        {
            state.badCalls++;
            return;
        }
        if (audio->format != AUDIO_FORMAT_FLOAT || audio->speakers != SPEAKERS_STEREO || audio->samples_per_sec != 48000)
            state.badCalls++;
        const auto *samples = reinterpret_cast<const float *>(audio->data[0]); // formato pedido: float intercalado
        for (std::uint32_t i = 0; i < audio->frames * 2; ++i)
            state.peak = std::max(state.peak, std::abs(samples[i]));
        if (state.nextTimestamp != 0 && audio->timestamp + 1000 < state.nextTimestamp)
            state.timestampRegressions++;
        state.nextTimestamp = audio->timestamp + static_cast<std::uint64_t>(audio->frames * 1e9 / 48000.0);
        state.frames += audio->frames;
        state.calls++;
    }

    __declspec(dllexport) bool obs_get_audio_info(obs_audio_info *oai)
    {
        oai->samples_per_sec = 48000;
        oai->speakers = SPEAKERS_STEREO;
        return true;
    }

    __declspec(dllexport) long long obs_data_get_int(obs_data_t *data, const char *name)
    {
        const auto *d = reinterpret_cast<const FakeData *>(data); // obs_data_t de prueba
        if (std::strcmp(name, "channel") == 0)
            return d->channel;
        if (std::strcmp(name, "buffer") == 0)
            return d->buffer;
        return 0;
    }

    __declspec(dllexport) void obs_data_set_default_int(obs_data_t *data, const char *name, long long val)
    {
        auto *d = reinterpret_cast<FakeData *>(data);
        if (std::strcmp(name, "channel") == 0)
            d->channel = val;
        else if (std::strcmp(name, "buffer") == 0)
            d->buffer = val;
    }

    __declspec(dllexport) obs_properties_t *obs_properties_create(void)
    {
        std::scoped_lock lock(state.mutex);
        state.properties.clear();
        return reinterpret_cast<obs_properties_t *>(&state.properties);
    }

    __declspec(dllexport) obs_property_t *obs_properties_add_list(obs_properties_t *, const char *name, const char *description,
                                                                  enum obs_combo_type type, enum obs_combo_format format)
    {
        std::scoped_lock lock(state.mutex);
        state.properties.push_back({name, description, type == OBS_COMBO_TYPE_LIST && format == OBS_COMBO_FORMAT_INT, {}});
        return reinterpret_cast<obs_property_t *>(state.properties.size()); // índice + 1
    }

    __declspec(dllexport) std::size_t obs_property_list_add_int(obs_property_t *p, const char *name, long long val)
    {
        std::scoped_lock lock(state.mutex);
        auto &prop = state.properties.at(reinterpret_cast<std::size_t>(p) - 1);
        prop.items.push_back({name, val});
        return prop.items.size() - 1;
    }

    __declspec(dllexport) obs_property_t *obs_properties_add_text(obs_properties_t *, const char *name, const char *description,
                                                                  enum obs_text_type type)
    {
        std::scoped_lock lock(state.mutex);
        state.properties.push_back({name, description, type == OBS_TEXT_INFO, {}});
        return reinterpret_cast<obs_property_t *>(state.properties.size());
    }

    __declspec(dllexport) void obs_property_set_long_description(obs_property_t *p, const char *text)
    {
        std::scoped_lock lock(state.mutex);
        state.properties.at(reinterpret_cast<std::size_t>(p) - 1).longDescription = text;
    }

    __declspec(dllexport) std::uint64_t os_gettime_ns(void)
    {
        LARGE_INTEGER f, c;
        QueryPerformanceFrequency(&f);
        QueryPerformanceCounter(&c);
        return static_cast<std::uint64_t>(static_cast<double>(c.QuadPart) * 1e9 / static_cast<double>(f.QuadPart));
    }

    __declspec(dllexport) void blog(int level, const char *format, ...)
    {
        char text[512];
        va_list args;
        va_start(args, format);
        std::vsnprintf(text, sizeof(text), format, args);
        va_end(args);
        std::scoped_lock lock(state.mutex);
        state.log.emplace_back(text);
        std::printf("[obs %d] %s\n", level, text);
    }
}
