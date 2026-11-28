#include "SharedChannel.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <sddl.h>
#include <array>
#include <string>
#include <thread>

namespace zlink
{
namespace
{
// Acceso total para SYSTEM, administradores, el creador y usuarios interactivos;
// etiqueta de integridad baja para que un proceso aislado (integridad baja) también pueda escribir.
constexpr wchar_t kSddl[] = L"D:P(A;;GA;;;SY)(A;;GA;;;BA)(A;;GA;;;OW)(A;;GA;;;IU)S:(ML;;NW;;;LW)";

std::string utf8(const std::wstring &w)
{
    if (w.empty())
        return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<std::size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}
} // namespace

SharedChannel::~SharedChannel()
{
    close();
}

bool SharedChannel::open(std::uint32_t index)
{
    close();
    index_ = index % kLinkChannels;
    wchar_t name[48];
    sharedObjectName(index_, name);

    PSECURITY_DESCRIPTOR sd = nullptr;
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, FALSE};
    if (ConvertStringSecurityDescriptorToSecurityDescriptorW(kSddl, SDDL_REVISION_1, &sd, nullptr))
        sa.lpSecurityDescriptor = sd;
    constexpr auto size = static_cast<unsigned long long>(kSharedBytes);
    HANDLE m = CreateFileMappingW(INVALID_HANDLE_VALUE, sa.lpSecurityDescriptor ? &sa : nullptr, PAGE_READWRITE,
                                  static_cast<DWORD>(size >> 32), static_cast<DWORD>(size & 0xFFFFFFFFu), name);
    bool existed = GetLastError() == ERROR_ALREADY_EXISTS;
    if (sd != nullptr)
        LocalFree(sd);
    if (m == nullptr)
    {
        // Creado por otro proceso con permisos que no nos dejan crear: se intenta abrir.
        m = OpenFileMappingW(FILE_MAP_READ | FILE_MAP_WRITE, FALSE, name);
        existed = m != nullptr;
    }
    if (m == nullptr)
    {
        // Último recurso: el descriptor propio no se aceptó; se crea con la seguridad por defecto.
        m = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, static_cast<DWORD>(size >> 32),
                               static_cast<DWORD>(size & 0xFFFFFFFFu), name);
        existed = GetLastError() == ERROR_ALREADY_EXISTS;
        if (m == nullptr)
            return false;
    }
    void *v = MapViewOfFile(m, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, kSharedBytes);
    if (v == nullptr)
    {
        CloseHandle(m);
        return false;
    }
    mapping_ = m;
    view_ = v;
    // La memoria nueva de un mapeo de archivo de paginación llega en cero: los atómicos quedan válidos.
    header_ = static_cast<SharedHeader *>(v);
    samples_ = sharedSamples(v);
    if (!existed)
        initializeHeader(*header_);
    // Quien lo creó puede estar publicando la marca en este mismo instante.
    for (int i = 0; i < 20 && !headerReady(*header_); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    if (!headerReady(*header_))
    {
        close();
        return false;
    }
    return true;
}

void SharedChannel::close() noexcept
{
    if (view_ != nullptr)
        UnmapViewOfFile(view_);
    if (mapping_ != nullptr)
        CloseHandle(mapping_);
    view_ = mapping_ = nullptr;
    header_ = nullptr;
    samples_ = nullptr;
}

std::uint64_t nowMs() noexcept
{
    return GetTickCount64();
}

std::uint32_t currentPid() noexcept
{
    return GetCurrentProcessId();
}

const char *hostDisplayName() noexcept
{
    static const std::string name = []
    {
        std::wstring path(MAX_PATH, L'\0');
        const auto n = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        path.resize(n);
        auto file = path.substr(path.find_last_of(L"\\/") + 1);
        if (const auto dot = file.find_last_of(L'.'); dot != std::wstring::npos)
            file.resize(dot);
        std::wstring lower = file;
        for (auto &c : lower)
            c = static_cast<wchar_t>(towlower(c));
        struct Known
        {
            const wchar_t *exe;
            const char *name;
        };
        static constexpr std::array<Known, 15> known{{{L"fl64", "FL Studio"},
                                                      {L"fl", "FL Studio"},
                                                      {L"ableton live", "Ableton Live"},
                                                      {L"reaper", "REAPER"},
                                                      {L"cubase", "Cubase"},
                                                      {L"nuendo", "Nuendo"},
                                                      {L"studio one", "Studio One"},
                                                      {L"bitwig studio", "Bitwig Studio"},
                                                      {L"bitwigpluginhost", "Bitwig Studio"},
                                                      {L"cakewalk", "Cakewalk"},
                                                      {L"reason", "Reason"},
                                                      {L"ilbridge", "FL Studio"},
                                                      {L"audiogridder", "AudioGridder"},
                                                      {L"luma_qa", "ZUENA QA"},
                                                      {L"link_qa", "ZUENA QA"}}};
        for (const auto &k : known)
            if (lower.rfind(k.exe, 0) == 0)
                return std::string(k.name);
        return utf8(file);
    }();
    return name.c_str();
}
} // namespace zlink
