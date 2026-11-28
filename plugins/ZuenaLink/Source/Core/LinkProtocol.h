#pragma once
// Formato de la memoria compartida entre el plugin (escritor) y la fuente de OBS (lectores).
// Un bloque por canal "Link N": encabezado de 4 KiB + anillo estéreo de float.
// Todo campo que cruza procesos es atómico y sin bloqueos; ningún lado espera al otro.
// Si este formato cambia, se sube kProtocolVersion: el nombre del objeto lo incluye y dos versiones
// distintas simplemente no se ven (nunca se interpreta mal la memoria del otro).
#include "BroadcastRing.h"
#include <array>
#include <cstddef>
#include <cstdint>

namespace zlink
{
inline constexpr std::uint32_t kMagic = 0x4B4E4C5Au; // "ZLNK"
inline constexpr std::uint32_t kProtocolVersion = 1;
inline constexpr std::uint32_t kLinkChannels = 8;     // Link 1 … Link 8
inline constexpr std::uint32_t kSharedCapacityFrames = 1u << 16; // 1,36 s a 48 kHz, 0,34 s a 192 kHz
inline constexpr std::size_t kHeaderBytes = 4096;
inline constexpr std::size_t kSharedBytes = kHeaderBytes + std::size_t{kSharedCapacityFrames} * kRingChannels * sizeof(float);
inline constexpr std::uint64_t kOwnerStaleMs = 1000;  // sin latido durante este tiempo, el canal se libera
inline constexpr std::uint64_t kReaderStaleMs = 1500; // sin lector durante este tiempo: "Esperando a OBS"
inline constexpr std::size_t kNameBytes = 64;

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4324) // relleno buscado: el cursor va en su propia línea de caché
#endif
struct SharedHeader
{
    std::atomic<std::uint32_t> magic;    // se publica último: el resto ya está inicializado
    std::atomic<std::uint32_t> version;
    std::atomic<std::uint32_t> capacityFrames;
    std::atomic<std::uint32_t> channels;

    // Dueño del canal (una sola instancia del plugin escribe).
    std::atomic<std::uint64_t> ownerToken;  // 0 = libre
    std::atomic<std::uint64_t> ownerBeatMs; // GetTickCount64 del hilo de servicio del dueño
    std::atomic<std::uint32_t> ownerPid;
    std::atomic<std::uint32_t> sampleRate;
    std::atomic<std::uint32_t> epoch;       // cambia al cambiar de dueño o de frecuencia: el lector se reinicia
    std::atomic<std::uint32_t> blockFrames; // bloque más grande que manda el DAW (define el colchón mínimo)
    std::atomic<std::uint32_t> streaming;   // 1 = audio en vivo; 0 = apagado, en pausa o exportando
    std::atomic<std::uint32_t> nameSeq;     // par = nombre estable (secuencia de escritura del nombre)

    // Otra instancia que quiere el mismo canal (para avisar en las dos).
    std::atomic<std::uint64_t> contenderToken;
    std::atomic<std::uint64_t> contenderBeatMs;

    // Lo que informa el lector (OBS) para que el plugin lo muestre.
    std::atomic<std::uint64_t> readerBeatMs;
    std::atomic<std::uint32_t> readerLatencyUs;
    std::atomic<std::uint32_t> readerCuts;
    std::atomic<std::uint32_t> readerRate;
    std::atomic<std::uint32_t> readerPid;

    std::array<std::atomic<char>, kNameBytes> ownerName; // "FL Studio", "Ableton Live"… (informativo)

    alignas(64) RingCursor cursor;
};
#ifdef _MSC_VER
#pragma warning(pop)
#endif
static_assert(sizeof(SharedHeader) <= kHeaderBytes);
static_assert(std::atomic<std::uint32_t>::is_always_lock_free && std::atomic<char>::is_always_lock_free);
static_assert(isRingCapacity(kSharedCapacityFrames));

inline float *sharedSamples(void *base) noexcept
{
    return reinterpret_cast<float *>(static_cast<std::byte *>(base) + kHeaderBytes); // diseño fijo del bloque
}

/** El nombre del objeto de Windows para el canal `index` (0…7). */
inline void sharedObjectName(std::uint32_t index, wchar_t (&out)[48]) noexcept
{
    const wchar_t *prefix = L"Local\\ZUENA.Link.v1.";
    std::size_t i = 0;
    for (; prefix[i] != 0; ++i)
        out[i] = prefix[i];
    out[i++] = static_cast<wchar_t>(L'1' + (index % kLinkChannels));
    out[i] = 0;
}

/** Copia el nombre del host con una secuencia (par = estable) para que el lector nunca lea a medias. */
inline void writeOwnerName(SharedHeader &h, const char *name) noexcept
{
    const auto seq = h.nameSeq.load(std::memory_order_relaxed);
    h.nameSeq.store(seq + 1, std::memory_order_release);
    std::size_t i = 0;
    for (; name != nullptr && name[i] != 0 && i + 1 < kNameBytes; ++i)
        h.ownerName[i].store(name[i], std::memory_order_relaxed);
    for (; i < kNameBytes; ++i)
        h.ownerName[i].store(0, std::memory_order_relaxed);
    h.nameSeq.store(seq + 2, std::memory_order_release);
}
inline bool readOwnerName(const SharedHeader &h, std::array<char, kNameBytes> &out) noexcept
{
    for (int attempt = 0; attempt < 4; ++attempt)
    {
        const auto before = h.nameSeq.load(std::memory_order_acquire);
        if (before % 2 != 0)
            continue;
        for (std::size_t i = 0; i < kNameBytes; ++i)
            out[i] = h.ownerName[i].load(std::memory_order_relaxed);
        out[kNameBytes - 1] = 0;
        if (h.nameSeq.load(std::memory_order_acquire) == before)
            return true;
    }
    out[0] = 0;
    return false;
}
/** Inicializa un bloque recién creado (todo en cero). El último paso publica la marca. */
inline void initializeHeader(SharedHeader &h) noexcept
{
    h.version.store(kProtocolVersion, std::memory_order_relaxed);
    h.capacityFrames.store(kSharedCapacityFrames, std::memory_order_relaxed);
    h.channels.store(kRingChannels, std::memory_order_relaxed);
    h.magic.store(kMagic, std::memory_order_release);
}
inline bool headerReady(const SharedHeader &h) noexcept
{
    return h.magic.load(std::memory_order_acquire) == kMagic &&
           h.version.load(std::memory_order_relaxed) == kProtocolVersion &&
           h.capacityFrames.load(std::memory_order_relaxed) == kSharedCapacityFrames &&
           h.channels.load(std::memory_order_relaxed) == kRingChannels;
}

enum class Claim : std::uint8_t
{
    owned,     // esta instancia escribe el canal
    acquired,  // lo acaba de tomar (canal libre o dueño anterior sin latido)
    busy       // otra instancia viva lo usa
};

/**
 * Lo llama el hilo de servicio del plugin cada pocos milisegundos (nunca el hilo de audio).
 * Toma el canal si está libre o abandonado, renueva el latido si ya es suyo, o se anota como
 * aspirante para que el dueño muestre el aviso de canal compartido.
 */
inline Claim claimChannel(SharedHeader &h, std::uint64_t token, std::uint32_t pid, std::uint64_t nowMs) noexcept
{
    auto current = h.ownerToken.load(std::memory_order_acquire);
    if (current == token)
    {
        h.ownerBeatMs.store(nowMs, std::memory_order_release);
        return Claim::owned;
    }
    const auto beat = h.ownerBeatMs.load(std::memory_order_acquire);
    const bool abandoned = current != 0 && nowMs > beat && nowMs - beat > kOwnerStaleMs;
    if ((current == 0 || abandoned) && h.ownerToken.compare_exchange_strong(current, token, std::memory_order_acq_rel))
    {
        h.ownerBeatMs.store(nowMs, std::memory_order_release);
        h.ownerPid.store(pid, std::memory_order_relaxed);
        h.streaming.store(0, std::memory_order_relaxed);
        h.epoch.fetch_add(1, std::memory_order_acq_rel);
        return Claim::acquired;
    }
    h.contenderToken.store(token, std::memory_order_relaxed);
    h.contenderBeatMs.store(nowMs, std::memory_order_release);
    return Claim::busy;
}

/** Suelta el canal solo si sigue siendo de esta instancia. */
inline void releaseChannel(SharedHeader &h, std::uint64_t token) noexcept
{
    auto expected = token;
    if (h.ownerToken.load(std::memory_order_acquire) == token)
    {
        h.streaming.store(0, std::memory_order_release);
        h.ownerToken.compare_exchange_strong(expected, 0, std::memory_order_acq_rel);
    }
}

/** ¿Hay otra instancia viva pidiendo este canal? (lo consulta el dueño). */
inline bool hasContender(const SharedHeader &h, std::uint64_t token, std::uint64_t nowMs) noexcept
{
    const auto other = h.contenderToken.load(std::memory_order_relaxed);
    const auto beat = h.contenderBeatMs.load(std::memory_order_acquire);
    return other != 0 && other != token && nowMs >= beat && nowMs - beat < 500;
}

/** El canal está libre para esta instancia (o es suyo). */
inline bool channelAvailable(const SharedHeader &h, std::uint64_t token, std::uint64_t nowMs) noexcept
{
    const auto owner = h.ownerToken.load(std::memory_order_acquire);
    const auto beat = h.ownerBeatMs.load(std::memory_order_acquire);
    return owner == 0 || owner == token || (nowMs > beat && nowMs - beat > kOwnerStaleMs);
}

inline bool readerAlive(const SharedHeader &h, std::uint64_t nowMs) noexcept
{
    const auto beat = h.readerBeatMs.load(std::memory_order_acquire);
    return beat != 0 && nowMs >= beat && nowMs - beat < kReaderStaleMs;
}
inline bool ownerAlive(const SharedHeader &h, std::uint64_t nowMs) noexcept
{
    const auto beat = h.ownerBeatMs.load(std::memory_order_acquire);
    return h.ownerToken.load(std::memory_order_acquire) != 0 && nowMs >= beat && nowMs - beat <= kOwnerStaleMs;
}
} // namespace zlink
