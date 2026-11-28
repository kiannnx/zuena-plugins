#include "WavFormat.h"
#include <bit>
#include <cmath>
#include <cstring>
#include <limits>

namespace zlink
{
namespace
{
constexpr std::uint64_t kMax32 = 0xFFFFFFFFull;

void put32(std::vector<std::uint8_t> &v, std::uint32_t x)
{
    for (int i = 0; i < 4; ++i)
        v.push_back(static_cast<std::uint8_t>(x >> (8 * i)));
}
void put16(std::vector<std::uint8_t> &v, std::uint16_t x)
{
    v.push_back(static_cast<std::uint8_t>(x));
    v.push_back(static_cast<std::uint8_t>(x >> 8));
}
void put64(std::vector<std::uint8_t> &v, std::uint64_t x)
{
    put32(v, static_cast<std::uint32_t>(x));
    put32(v, static_cast<std::uint32_t>(x >> 32));
}
void tag(std::vector<std::uint8_t> &v, const char (&t)[5])
{
    v.insert(v.end(), t, t + 4);
}
std::uint32_t get32(std::span<const std::uint8_t> s, std::size_t at)
{
    return static_cast<std::uint32_t>(s[at]) | (static_cast<std::uint32_t>(s[at + 1]) << 8) |
           (static_cast<std::uint32_t>(s[at + 2]) << 16) | (static_cast<std::uint32_t>(s[at + 3]) << 24);
}
std::uint16_t get16(std::span<const std::uint8_t> s, std::size_t at)
{
    return static_cast<std::uint16_t>(s[at] | (s[at + 1] << 8));
}
std::uint64_t get64(std::span<const std::uint8_t> s, std::size_t at)
{
    return get32(s, at) | (static_cast<std::uint64_t>(get32(s, at + 4)) << 32);
}
bool isTag(std::span<const std::uint8_t> s, std::size_t at, const char (&t)[5])
{
    return at + 4 <= s.size() && std::memcmp(s.data() + at, t, 4) == 0;
}
} // namespace

std::size_t wavHeaderBytes(const WavLayout &l) noexcept
{
    return l.format == SampleFormat::float32 ? 94 : 80;
}

std::vector<std::uint8_t> wavHeader(const WavLayout &l, std::uint64_t frames)
{
    const bool isFloat = l.format == SampleFormat::float32;
    const std::uint32_t bps = bytesPerSample(l.format);
    const std::uint32_t blockAlign = bps * l.channels;
    const std::uint64_t dataBytes = frames * blockAlign;
    const std::size_t header = wavHeaderBytes(l);
    const std::uint64_t riffBytes = header - 8 + dataBytes + (dataBytes & 1); // relleno a par
    const bool rf64 = riffBytes > kMax32;

    std::vector<std::uint8_t> v;
    v.reserve(header);
    tag(v, rf64 ? "RF64" : "RIFF");
    put32(v, rf64 ? static_cast<std::uint32_t>(kMax32) : static_cast<std::uint32_t>(riffBytes));
    tag(v, "WAVE");
    // Lugar reservado para ds64: mientras el archivo es chico es un JUNK que todos ignoran.
    tag(v, rf64 ? "ds64" : "JUNK");
    put32(v, 28);
    put64(v, rf64 ? riffBytes : 0);
    put64(v, rf64 ? dataBytes : 0);
    put64(v, rf64 ? frames : 0);
    put32(v, 0);
    tag(v, "fmt ");
    put32(v, isFloat ? 18 : 16);
    put16(v, isFloat ? 3 : 1); // WAVE_FORMAT_IEEE_FLOAT / WAVE_FORMAT_PCM
    put16(v, l.channels);
    put32(v, l.sampleRate);
    put32(v, l.sampleRate * blockAlign);
    put16(v, static_cast<std::uint16_t>(blockAlign));
    put16(v, static_cast<std::uint16_t>(bps * 8));
    if (isFloat)
    {
        put16(v, 0);
        tag(v, "fact");
        put32(v, 4);
        put32(v, frames > kMax32 || rf64 ? static_cast<std::uint32_t>(kMax32) : static_cast<std::uint32_t>(frames));
    }
    tag(v, "data");
    put32(v, rf64 || dataBytes > kMax32 ? static_cast<std::uint32_t>(kMax32) : static_cast<std::uint32_t>(dataBytes));
    return v;
}

float SampleEncoder::dither() noexcept
{
    // Dos uniformes independientes (xorshift32) → densidad triangular de ±1 LSB.
    auto next = [this]
    {
        seed_ ^= seed_ << 13;
        seed_ ^= seed_ >> 17;
        seed_ ^= seed_ << 5;
        return static_cast<float>(seed_) * (1.0f / 4294967296.0f);
    };
    return next() - next();
}

std::size_t SampleEncoder::encode(std::span<const float> in, std::span<std::uint8_t> out) noexcept
{
    const auto bps = bytesPerSample(format_);
    const auto n = std::min(in.size(), out.size() / bps);
    std::uint8_t *o = out.data();
    for (std::size_t i = 0; i < n; ++i)
    {
        float x = in[i];
        if (!std::isfinite(x))
            x = 0.0f;
        if (format_ == SampleFormat::float32)
        {
            const auto bits = std::bit_cast<std::uint32_t>(x);
            for (int b = 0; b < 4; ++b)
                *o++ = static_cast<std::uint8_t>(bits >> (8 * b));
            continue;
        }
        const double scale = format_ == SampleFormat::pcm16 ? 32767.0 : 8388607.0;
        const double lo = format_ == SampleFormat::pcm16 ? -32768.0 : -8388608.0;
        double v = x * scale + (format_ == SampleFormat::pcm16 ? dither() : 0.0f);
        v = std::nearbyint(v);
        if (v > scale || v < lo)
        {
            ++clipped_;
            v = v > scale ? scale : lo;
        }
        const auto q = static_cast<std::int32_t>(v);
        const auto u = static_cast<std::uint32_t>(q);
        *o++ = static_cast<std::uint8_t>(u);
        *o++ = static_cast<std::uint8_t>(u >> 8);
        if (format_ == SampleFormat::pcm24)
            *o++ = static_cast<std::uint8_t>(u >> 16);
    }
    return n * bps;
}

bool parseWav(std::span<const std::uint8_t> s, WavContents &out)
{
    out = {};
    if (s.size() < 12 || !(isTag(s, 0, "RIFF") || isTag(s, 0, "RF64")) || !isTag(s, 8, "WAVE"))
        return false;
    out.rf64 = isTag(s, 0, "RF64");
    std::uint64_t ds64Data = 0;
    std::size_t at = 12, dataAt = 0;
    std::uint64_t dataBytes = 0;
    std::uint16_t formatTag = 0, bits = 0;
    while (at + 8 <= s.size())
    {
        const auto size = get32(s, at + 4);
        if (isTag(s, at, "ds64") && size >= 24)
            ds64Data = get64(s, at + 16);
        else if (isTag(s, at, "fmt ") && size >= 16)
        {
            formatTag = get16(s, at + 8);
            out.layout.channels = get16(s, at + 10);
            out.layout.sampleRate = get32(s, at + 12);
            bits = get16(s, at + 22);
        }
        else if (isTag(s, at, "data"))
        {
            dataAt = at + 8;
            dataBytes = (size == kMax32 && out.rf64) ? ds64Data : size;
            break;
        }
        at += 8 + size + (size & 1);
    }
    if (dataAt == 0 || out.layout.channels == 0)
        return false;
    if (formatTag == 3 && bits == 32)
        out.layout.format = SampleFormat::float32;
    else if (formatTag == 1 && bits == 24)
        out.layout.format = SampleFormat::pcm24;
    else if (formatTag == 1 && bits == 16)
        out.layout.format = SampleFormat::pcm16;
    else
        return false;
    const auto bps = bytesPerSample(out.layout.format);
    dataBytes = std::min<std::uint64_t>(dataBytes, s.size() - dataAt);
    const auto samples = static_cast<std::size_t>(dataBytes / bps);
    out.frames = samples / out.layout.channels;
    out.samples.resize(samples);
    for (std::size_t i = 0; i < samples; ++i)
    {
        const auto p = dataAt + i * bps;
        if (out.layout.format == SampleFormat::float32)
            out.samples[i] = std::bit_cast<float>(get32(s, p));
        else if (out.layout.format == SampleFormat::pcm24)
        {
            auto v = static_cast<std::int32_t>(s[p] | (s[p + 1] << 8) | (s[p + 2] << 16));
            if (v & 0x800000)
                v -= 0x1000000;
            out.samples[i] = static_cast<float>(v / 8388607.0);
        }
        else
            out.samples[i] = static_cast<float>(static_cast<std::int16_t>(get16(s, p)) / 32767.0);
    }
    return true;
}
} // namespace zlink
