#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <initguid.h> // define en esta unidad las PROPERTYKEY que siguen
#include <propkeydef.h>
#include <mmdeviceapi.h>
#include <functiondiscoverykeys_devpkey.h>
#include <mmreg.h>
#include <cstring>

#include "DeviceCatalog.h"
#include <algorithm>

namespace zlink
{
namespace
{
template <typename T> struct Com
{
    T *p = nullptr;
    Com() = default;
    Com(const Com &) = delete;
    Com &operator=(const Com &) = delete;
    ~Com()
    {
        if (p != nullptr)
            p->Release();
    }
    T **out() noexcept { return &p; }
    T *operator->() const noexcept { return p; }
    explicit operator bool() const noexcept { return p != nullptr; }
};

std::wstring readString(IPropertyStore *store, const PROPERTYKEY &key)
{
    PROPVARIANT v;
    PropVariantInit(&v);
    std::wstring out;
    if (SUCCEEDED(store->GetValue(key, &v)) && v.vt == VT_LPWSTR && v.pwszVal != nullptr)
        out = v.pwszVal;
    PropVariantClear(&v);
    return out;
}

void readFormat(IPropertyStore *store, Endpoint &e)
{
    PROPVARIANT v;
    PropVariantInit(&v);
    if (SUCCEEDED(store->GetValue(PKEY_AudioEngine_DeviceFormat, &v)) && v.vt == VT_BLOB &&
        v.blob.cbSize >= sizeof(WAVEFORMATEX))
    {
        WAVEFORMATEX fmt{};
        std::memcpy(&fmt, v.blob.pBlobData, sizeof(fmt));
        e.channels = std::max<std::uint32_t>(1, fmt.nChannels);
        e.sampleRate = fmt.nSamplesPerSec;
    }
    PropVariantClear(&v);
}

std::vector<Endpoint> enumerate(IMMDeviceEnumerator *en, EDataFlow flow, const std::wstring &defaultId)
{
    std::vector<Endpoint> list;
    Com<IMMDeviceCollection> all;
    if (FAILED(en->EnumAudioEndpoints(flow, DEVICE_STATE_ACTIVE, all.out())))
        return list;
    UINT count = 0;
    all->GetCount(&count);
    for (UINT i = 0; i < count; ++i)
    {
        Com<IMMDevice> dev;
        Com<IPropertyStore> store;
        LPWSTR id = nullptr;
        if (FAILED(all->Item(i, dev.out())) || FAILED(dev->GetId(&id)) ||
            FAILED(dev->OpenPropertyStore(STGM_READ, store.out())))
        {
            if (id != nullptr)
                CoTaskMemFree(id);
            continue;
        }
        Endpoint e;
        e.id = id;
        CoTaskMemFree(id);
        e.name = toUtf8(readString(store.p, PKEY_Device_FriendlyName));
        e.shortName = toUtf8(readString(store.p, PKEY_Device_DeviceDesc));
        e.adapter = toUtf8(readString(store.p, PKEY_DeviceInterface_FriendlyName));
        if (e.shortName.empty())
            e.shortName = e.name;
        readFormat(store.p, e);
        e.isDefault = e.id == defaultId;
        list.push_back(std::move(e));
    }
    return list;
}
} // namespace

std::string toUtf8(const std::wstring &w)
{
    if (w.empty())
        return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<std::size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}
std::wstring fromUtf8(const std::string &s)
{
    if (s.empty())
        return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

std::vector<Endpoint> listRenderEndpoints()
{
    Com<IMMDeviceEnumerator> en;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
                                reinterpret_cast<void **>(en.out())))) // firma de COM
        return {};
    std::wstring defaultId;
    {
        Com<IMMDevice> def;
        LPWSTR id = nullptr;
        if (SUCCEEDED(en->GetDefaultAudioEndpoint(eRender, eMultimedia, def.out())) && SUCCEEDED(def->GetId(&id)))
        {
            defaultId = id;
            CoTaskMemFree(id);
        }
    }
    auto render = enumerate(en.p, eRender, defaultId);
    const auto capture = enumerate(en.p, eCapture, {});
    for (auto &e : render)
    {
        e.route = classifyRender({e.shortName, e.adapter});
        // Si la misma placa publica una entrada de loopback, el consejo nombra la entrada exacta.
        if (e.route.interfaceHasLoopback || e.route.kind == RouteKind::interfaceVirtual)
            for (const auto &c : capture)
                if (!e.adapter.empty() && c.adapter == e.adapter && isLoopbackCapture({c.shortName, c.adapter}))
                {
                    e.route.obsHint += " En OBS esa entrada se llama «" + c.name + "».";
                    break;
                }
    }
    std::stable_sort(render.begin(), render.end(), [](const Endpoint &a, const Endpoint &b)
                     { return isRecommended(a.route) && !isRecommended(b.route); });
    return render;
}

struct DeviceWatcher::Impl final : IMMNotificationClient
{
    std::atomic<ULONG> refs{1};
    std::atomic<std::uint32_t> generation{0};
    Com<IMMDeviceEnumerator> en;
    bool registered = false;

    void bump() noexcept { generation.fetch_add(1, std::memory_order_release); }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
    ULONG STDMETHODCALLTYPE Release() override { return --refs; } // vida controlada por DeviceWatcher
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **out) override
    {
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IMMNotificationClient))
        {
            *out = static_cast<IMMNotificationClient *>(this);
            AddRef();
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }
    HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(LPCWSTR, DWORD) override { return bump(), S_OK; }
    HRESULT STDMETHODCALLTYPE OnDeviceAdded(LPCWSTR) override { return bump(), S_OK; }
    HRESULT STDMETHODCALLTYPE OnDeviceRemoved(LPCWSTR) override { return bump(), S_OK; }
    HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged(EDataFlow, ERole, LPCWSTR) override { return bump(), S_OK; }
    HRESULT STDMETHODCALLTYPE OnPropertyValueChanged(LPCWSTR, const PROPERTYKEY key) override
    {
        if (key == PKEY_AudioEngine_DeviceFormat || key == PKEY_Device_FriendlyName)
            bump();
        return S_OK;
    }
};

DeviceWatcher::DeviceWatcher() : impl_(std::make_unique<Impl>())
{
    if (SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
                                   reinterpret_cast<void **>(impl_->en.out())))) // firma de COM
        impl_->registered = SUCCEEDED(impl_->en->RegisterEndpointNotificationCallback(impl_.get()));
}
DeviceWatcher::~DeviceWatcher()
{
    if (impl_->registered)
        impl_->en->UnregisterEndpointNotificationCallback(impl_.get());
}
std::uint32_t DeviceWatcher::generation() const noexcept
{
    return impl_->generation.load(std::memory_order_acquire);
}
} // namespace zlink
