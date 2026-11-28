#pragma once
// Declaraciones mínimas de la API de libobs que usa la fuente «ZUENA Link» (escritas a partir de la
// documentación y de los encabezados públicos de OBS Studio 32.2.2: libobs/obs-source.h, obs.h,
// obs-properties.h, obs-data.h, media-io/audio-io.h, util/base.h, util/platform.h).
// Se declara solo lo necesario para no depender del árbol de OBS al compilar. La estructura
// obs_source_info se registra con el tamaño hasta icon_type (existe desde OBS 26), así una versión
// anterior de OBS no la rechaza por "más grande de lo que soporta".
#include <cstddef>
#include <cstdint>

// La fuente importa estas funciones de obs.dll; el obs.dll de prueba (Tests/FakeObs.cpp) las exporta.
#ifndef ZLINK_OBS_API
#define ZLINK_OBS_API __declspec(dllimport)
#endif

extern "C"
{
    struct obs_module;
    struct obs_source;
    struct obs_data;
    struct obs_properties;
    struct obs_property;
    using obs_module_t = obs_module;
    using obs_source_t = obs_source;
    using obs_data_t = obs_data;
    using obs_properties_t = obs_properties;
    using obs_property_t = obs_property;

    enum obs_source_type
    {
        OBS_SOURCE_TYPE_INPUT,
        OBS_SOURCE_TYPE_FILTER,
        OBS_SOURCE_TYPE_TRANSITION,
        OBS_SOURCE_TYPE_SCENE,
    };
    enum obs_icon_type
    {
        OBS_ICON_TYPE_UNKNOWN,
        OBS_ICON_TYPE_IMAGE,
        OBS_ICON_TYPE_COLOR,
        OBS_ICON_TYPE_SLIDESHOW,
        OBS_ICON_TYPE_AUDIO_INPUT,
        OBS_ICON_TYPE_AUDIO_OUTPUT,
    };
    enum speaker_layout
    {
        SPEAKERS_UNKNOWN,
        SPEAKERS_MONO,
        SPEAKERS_STEREO,
    };
    enum audio_format
    {
        AUDIO_FORMAT_UNKNOWN,
        AUDIO_FORMAT_U8BIT,
        AUDIO_FORMAT_16BIT,
        AUDIO_FORMAT_32BIT,
        AUDIO_FORMAT_FLOAT,
    };
    enum obs_combo_type
    {
        OBS_COMBO_TYPE_INVALID,
        OBS_COMBO_TYPE_EDITABLE,
        OBS_COMBO_TYPE_LIST,
    };
    enum obs_combo_format
    {
        OBS_COMBO_FORMAT_INVALID,
        OBS_COMBO_FORMAT_INT,
    };
    enum obs_text_type
    {
        OBS_TEXT_DEFAULT,
        OBS_TEXT_PASSWORD,
        OBS_TEXT_MULTILINE,
        OBS_TEXT_INFO,
    };

    inline constexpr std::uint32_t OBS_SOURCE_AUDIO = 1u << 1;
    inline constexpr int LOG_ERROR = 100, LOG_WARNING = 200, LOG_INFO = 300;
    inline constexpr std::size_t MAX_AV_PLANES = 8;

    using obs_fn = void (*)(); // campos que no usamos: punteros a función, mismo tamaño

    struct obs_source_info
    {
        const char *id;
        enum obs_source_type type;
        std::uint32_t output_flags;
        const char *(*get_name)(void *type_data);
        void *(*create)(obs_data_t *settings, obs_source_t *source);
        void (*destroy)(void *data);
        std::uint32_t (*get_width)(void *data);
        std::uint32_t (*get_height)(void *data);
        void (*get_defaults)(obs_data_t *settings);
        obs_properties_t *(*get_properties)(void *data);
        void (*update)(void *data, obs_data_t *settings);
        void (*activate)(void *data);
        void (*deactivate)(void *data);
        void (*show)(void *data);
        void (*hide)(void *data);
        obs_fn video_tick;
        obs_fn video_render;
        obs_fn filter_video;
        obs_fn filter_audio;
        obs_fn enum_active_sources;
        obs_fn save;
        obs_fn load;
        obs_fn mouse_click;
        obs_fn mouse_move;
        obs_fn mouse_wheel;
        obs_fn focus;
        obs_fn key_click;
        obs_fn filter_remove;
        void *type_data;
        obs_fn free_type_data;
        obs_fn audio_render;
        obs_fn enum_all_sources;
        obs_fn transition_start;
        obs_fn transition_stop;
        obs_fn get_defaults2;
        obs_fn get_properties2;
        obs_fn audio_mix;
        enum obs_icon_type icon_type;
        // (siguen campos de medios, versión e íconos: no se usan y no se registran)
    };
    inline constexpr std::size_t kSourceInfoSize = offsetof(obs_source_info, icon_type) + sizeof(enum obs_icon_type);

    struct obs_source_audio
    {
        const std::uint8_t *data[MAX_AV_PLANES];
        std::uint32_t frames;
        enum speaker_layout speakers;
        enum audio_format format;
        std::uint32_t samples_per_sec;
        std::uint64_t timestamp;
    };
    struct obs_audio_info
    {
        std::uint32_t samples_per_sec;
        enum speaker_layout speakers;
    };

    ZLINK_OBS_API void obs_register_source_s(const struct obs_source_info *info, std::size_t size);
    ZLINK_OBS_API void obs_source_output_audio(obs_source_t *source, const struct obs_source_audio *audio);
    ZLINK_OBS_API bool obs_get_audio_info(struct obs_audio_info *oai);
    ZLINK_OBS_API long long obs_data_get_int(obs_data_t *data, const char *name);
    ZLINK_OBS_API void obs_data_set_default_int(obs_data_t *data, const char *name, long long val);
    ZLINK_OBS_API obs_properties_t *obs_properties_create(void);
    ZLINK_OBS_API obs_property_t *obs_properties_add_list(obs_properties_t *props, const char *name,
                                                                  const char *description, enum obs_combo_type type,
                                                                  enum obs_combo_format format);
    ZLINK_OBS_API std::size_t obs_property_list_add_int(obs_property_t *p, const char *name, long long val);
    ZLINK_OBS_API obs_property_t *obs_properties_add_text(obs_properties_t *props, const char *name,
                                                                  const char *description, enum obs_text_type type);
    ZLINK_OBS_API void obs_property_set_long_description(obs_property_t *p, const char *long_description);
    ZLINK_OBS_API std::uint64_t os_gettime_ns(void);
    ZLINK_OBS_API void blog(int log_level, const char *format, ...);
}

static_assert(sizeof(void *) == 8, "OBS 28 en adelante es solo de 64 bits");
static_assert(offsetof(obs_source_info, get_name) == 16);
static_assert(offsetof(obs_source_info, type_data) == 16 + 25 * 8);
static_assert(offsetof(obs_source_info, icon_type) == 16 + 34 * 8);
