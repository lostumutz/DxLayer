#include "capture.h"

#include "frame_stats.h"

#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>
#include <winrt/Windows.Foundation.Metadata.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#include <winrt/Windows.Security.Authorization.AppCapabilityAccess.h>

#include <atomic>
#include <chrono>

#pragma comment(lib, "windowsapp.lib")

namespace wgc = winrt::Windows::Graphics::Capture;
using winrt::Windows::Foundation::Metadata::ApiInformation;
using winrt::Windows::Graphics::SizeInt32;
using winrt::Windows::Graphics::DirectX::DirectXPixelFormat;
using winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice;
using winrt::Windows::Security::Authorization::AppCapabilityAccess::AppCapabilityAccessStatus;

namespace capture
{
namespace
{
constexpr DirectXPixelFormat kFormat = DirectXPixelFormat::B8G8R8A8UIntNormalized;
constexpr int kBuffers = 2;

struct State
{
    winrt::com_ptr<ID3D11Device> device;
    IDirect3DDevice captureDevice{ nullptr };
    HANDLE frameEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    std::atomic<bool> borderless = false;

    HWND target = nullptr;
    wgc::GraphicsCaptureItem item{ nullptr };
    wgc::Direct3D11CaptureFramePool pool{ nullptr };
    wgc::GraphicsCaptureSession session{ nullptr };
    wgc::Direct3D11CaptureFramePool::FrameArrived_revoker arrived;
    SizeInt32 poolSize{};
    bool resizePending = false;

    // The frame on screen is held until the next one replaces it, so the pool can't write into it while it is still shown.
    wgc::Direct3D11CaptureFrame current{ nullptr };
    winrt::com_ptr<ID3D11ShaderResourceView> picture;
    SIZE pictureSize{};

    FrameStats stats;
} g;

bool SessionHas(const wchar_t* property)
{
    return ApiInformation::IsPropertyPresent(L"Windows.Graphics.Capture.GraphicsCaptureSession", property);
}
} // namespace

bool Init(ID3D11Device* device)
{
    try
    {
        if (!wgc::GraphicsCaptureSession::IsSupported())
            return false;

        g.device.copy_from(device);
        const auto dxgi = g.device.as<IDXGIDevice>();
        winrt::com_ptr<::IInspectable> wrapped;
        winrt::check_hresult(CreateDirect3D11DeviceFromDXGIDevice(dxgi.get(), wrapped.put()));
        g.captureDevice = wrapped.as<IDirect3DDevice>();

        // Windows 11 leaves the yellow border off the captured window once it has been asked to.
        if (SessionHas(L"IsBorderRequired"))
        {
            using winrt::Windows::Foundation::AsyncStatus;
            wgc::GraphicsCaptureAccess::RequestAccessAsync(wgc::GraphicsCaptureAccessKind::Borderless)
                .Completed([](const auto& request, AsyncStatus status)
                           { g.borderless = status == AsyncStatus::Completed && request.GetResults() == AppCapabilityAccessStatus::Allowed; });
        }
        return true;
    }
    catch (const winrt::hresult_error&)
    {
        return false;
    }
}

bool Start(HWND window, bool showCursor)
{
    try
    {
        const auto interop = winrt::get_activation_factory<wgc::GraphicsCaptureItem, IGraphicsCaptureItemInterop>();
        wgc::GraphicsCaptureItem item{ nullptr };
        winrt::check_hresult(interop->CreateForWindow(window, winrt::guid_of<wgc::GraphicsCaptureItem>(), winrt::put_abi(item)));

        g.poolSize = item.Size();
        g.pool = wgc::Direct3D11CaptureFramePool::CreateFreeThreaded(g.captureDevice, kFormat, kBuffers, g.poolSize);
        g.arrived = g.pool.FrameArrived(winrt::auto_revoke, [](auto&&, auto&&) { SetEvent(g.frameEvent); });

        g.session = g.pool.CreateCaptureSession(item);
        if (g.borderless)
            g.session.IsBorderRequired(false);
        // Left alone, Windows 11 hands over at most 60 frames a second.
        if (SessionHas(L"MinUpdateInterval"))
            g.session.MinUpdateInterval(std::chrono::milliseconds(1));
        if (SessionHas(L"IsCursorCaptureEnabled"))
            g.session.IsCursorCaptureEnabled(showCursor);
        g.session.StartCapture();

        g.item = item;
        g.target = window;
        g.stats.Reset();
        return true;
    }
    catch (const winrt::hresult_error&)
    {
        Stop();
        return false;
    }
}

void Stop()
{
    g.arrived.revoke();
    try
    {
        if (g.session)
            g.session.Close();
        if (g.pool)
            g.pool.Close();
    }
    catch (const winrt::hresult_error&)
    {
    }

    g.session = nullptr;
    g.pool = nullptr;
    g.item = nullptr;
    g.current = nullptr;
    g.picture = nullptr;
    g.pictureSize = {};
    g.resizePending = false;
    g.target = nullptr;
}

bool Running()
{
    return g.session != nullptr;
}

HWND Target()
{
    return g.target;
}

HANDLE FrameEvent()
{
    return g.frameEvent;
}

bool TakeFrame()
{
    if (!g.pool)
        return false;

    try
    {
        if (g.resizePending)
        {
            g.current = nullptr;
            g.pool.Recreate(g.captureDevice, kFormat, kBuffers, g.poolSize);
            g.resizePending = false;
        }

        wgc::Direct3D11CaptureFrame newest{ nullptr };
        while (wgc::Direct3D11CaptureFrame frame = g.pool.TryGetNextFrame())
        {
            newest = frame;
            g.stats.Add(ClockSeconds());
        }
        if (!newest)
            return false;

        // The pool's buffers have to follow the window's size. This frame is shown as it is; the next comes from resized buffers.
        const SizeInt32 size = newest.ContentSize();
        if (size.Width != g.poolSize.Width || size.Height != g.poolSize.Height)
        {
            g.poolSize = size;
            g.resizePending = true;
        }

        const auto access = newest.Surface().as<::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();
        winrt::com_ptr<ID3D11Texture2D> texture;
        winrt::check_hresult(access->GetInterface(IID_PPV_ARGS(texture.put())));

        D3D11_TEXTURE2D_DESC desc{};
        texture->GetDesc(&desc);
        winrt::com_ptr<ID3D11ShaderResourceView> picture;
        winrt::check_hresult(g.device->CreateShaderResourceView(texture.get(), nullptr, picture.put()));

        g.current = newest;
        g.picture = picture;
        g.pictureSize = { static_cast<LONG>(desc.Width), static_cast<LONG>(desc.Height) };
        return true;
    }
    catch (const winrt::hresult_error&)
    {
        Stop();
        return false;
    }
}

ID3D11ShaderResourceView* Picture()
{
    return g.picture.get();
}

SIZE PictureSize()
{
    return g.pictureSize;
}

void SetCursorCaptured(bool captured)
{
    if (!g.session || !SessionHas(L"IsCursorCaptureEnabled"))
        return;
    try
    {
        g.session.IsCursorCaptureEnabled(captured);
    }
    catch (const winrt::hresult_error&)
    {
    }
}

FrameStats& Stats()
{
    return g.stats;
}
} // namespace capture
