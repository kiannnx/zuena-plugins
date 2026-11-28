#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <knownfolders.h>
#include <shlobj.h>

#include "Recorder.h"
#include <cerrno>
#include <ctime>

namespace zlink
{
namespace
{
constexpr std::size_t kChunkFrames = 8192;
} // namespace

Recorder::Recorder() : chunk_(kChunkFrames * 2), mono_(kChunkFrames), bytes_(kChunkFrames * 2 * 4) {}

Recorder::~Recorder()
{
    stop();
}

std::filesystem::path defaultRecordingFolder()
{
    PWSTR music = nullptr;
    std::filesystem::path base;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Music, KF_FLAG_CREATE, nullptr, &music)) && music != nullptr)
        base = music;
    if (music != nullptr)
        CoTaskMemFree(music);
    if (base.empty())
        base = std::filesystem::temp_directory_path();
    return base / L"ZUENA Link";
}

std::filesystem::path nextRecordingPath(const std::filesystem::path &folder)
{
    const auto now = std::time(nullptr);
    std::tm local{};
    localtime_s(&local, &now);
    wchar_t stamp[64];
    std::wcsftime(stamp, 64, L"ZUENA Link %Y-%m-%d %H-%M-%S", &local);
    auto path = folder / (std::wstring(stamp) + L".wav");
    for (int n = 2; std::filesystem::exists(path) && n < 1000; ++n)
        path = folder / (std::wstring(stamp) + L" (" + std::to_wstring(n) + L").wav");
    return path;
}

bool Recorder::start(const std::filesystem::path &folder, const WavLayout &layout, const RingCursor *cursor,
                     const float *data, std::uint32_t capacity)
{
    stop();
    std::error_code ec;
    std::filesystem::create_directories(folder, ec);
    const auto path = nextRecordingPath(folder);
    std::FILE *f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"w+bN") != 0 || f == nullptr)
    {
        std::scoped_lock lock(mutex_);
        status_ = {};
        status_.error = "No se puede crear el archivo en la carpeta elegida.";
        return false;
    }
    std::setvbuf(f, nullptr, _IOFBF, 1 << 20);
    file_ = f;
    layout_ = layout;
    encoder_ = SampleEncoder(layout.format);
    reader_ = RingReader(cursor, data, capacity);
    frames_ = lastHeaderFrames_ = 0;
    {
        std::scoped_lock lock(mutex_);
        status_ = {};
        status_.recording = true;
        status_.file = path;
    }
    if (!writeHeader())
    {
        fail("No se puede escribir en el disco.");
        return false;
    }
    return true;
}

bool Recorder::writeHeader()
{
    const auto header = wavHeader(layout_, frames_);
    const auto end = _ftelli64(file_);
    if (_fseeki64(file_, 0, SEEK_SET) != 0 || std::fwrite(header.data(), 1, header.size(), file_) != header.size())
        return false;
    if (end > static_cast<long long>(header.size()) && _fseeki64(file_, end, SEEK_SET) != 0)
        return false;
    return std::fflush(file_) == 0;
}

void Recorder::fail(const std::string &message)
{
    if (file_ != nullptr)
    {
        writeHeader(); // lo grabado hasta acá queda reproducible
        std::fclose(file_);
        file_ = nullptr;
    }
    std::scoped_lock lock(mutex_);
    status_.recording = false;
    status_.error = message;
}

void Recorder::drain()
{
    if (file_ == nullptr)
        return;
    std::uint32_t cuts = 0;
    for (int guard = 0; guard < 256; ++guard)
    {
        const auto r = reader_.read(chunk_);
        if (r.overrun)
            ++cuts;
        if (r.frames == 0)
            break;
        std::span<const float> samples{chunk_.data(), static_cast<std::size_t>(r.frames) * 2};
        if (layout_.channels == 1)
        {
            for (std::uint32_t i = 0; i < r.frames; ++i)
                mono_[i] = chunk_[2 * i];
            samples = {mono_.data(), r.frames};
        }
        const auto n = encoder_.encode(samples, bytes_);
        if (std::fwrite(bytes_.data(), 1, n, file_) != n)
        {
            fail(errno == ENOSPC ? "Se detuvo la grabación: no hay más espacio en el disco."
                                 : "Se detuvo la grabación: no se pudo escribir en el disco.");
            return;
        }
        frames_ += r.frames;
        if (r.frames < kChunkFrames)
            break;
    }
    if (frames_ - lastHeaderFrames_ >= layout_.sampleRate)
    {
        lastHeaderFrames_ = frames_;
        if (!writeHeader())
        {
            fail("Se detuvo la grabación: no se pudo escribir en el disco.");
            return;
        }
    }
    std::scoped_lock lock(mutex_);
    status_.cuts += cuts;
    status_.seconds = static_cast<double>(frames_) / layout_.sampleRate;
    status_.bytes = wavHeaderBytes(layout_) + frames_ * bytesPerSample(layout_.format) * layout_.channels;
    status_.clipped = encoder_.clipped();
}

void Recorder::stop(const std::string &reason)
{
    if (file_ == nullptr)
        return;
    drain();
    if (file_ == nullptr)
        return; // drain() ya cerró por un error
    // Relleno a tamaño par, como pide RIFF.
    const auto dataBytes = frames_ * bytesPerSample(layout_.format) * layout_.channels;
    if (dataBytes & 1)
        std::fputc(0, file_);
    const bool ok = writeHeader();
    std::fclose(file_);
    file_ = nullptr;
    std::scoped_lock lock(mutex_);
    status_.recording = false;
    status_.error = ok ? reason : "El archivo puede estar incompleto: falló la última escritura.";
}

Recorder::Status Recorder::status() const
{
    std::scoped_lock lock(mutex_);
    return status_;
}
} // namespace zlink
