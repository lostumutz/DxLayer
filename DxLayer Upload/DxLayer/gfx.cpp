#include "gfx.h"

#include <d3dcompiler.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <algorithm>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "dxgi.lib")

using Microsoft::WRL::ComPtr;

namespace gfx
{
namespace
{
constexpr char kBlit[] = R"(
cbuffer Crop : register(b0) { float4 area; };
Texture2D picture : register(t0);
SamplerState texel : register(s0);

void vs(uint id : SV_VertexID, out float4 position : SV_Position, out float2 uv : TEXCOORD)
{
    uv = float2((id << 1) & 2, id & 2);
    position = float4(uv * float2(2, -2) + float2(-1, 1), 0, 1);
}

float4 ps(float4 position : SV_Position, float2 uv : TEXCOORD) : SV_Target
{
    return float4(picture.Sample(texel, lerp(area.xy, area.zw, uv)).rgb, 1);
}
)";

struct State
{
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<IDXGISwapChain> swapchain;
    ComPtr<ID3D11RenderTargetView> target;
    ComPtr<ID3D11VertexShader> vs;
    ComPtr<ID3D11PixelShader> ps;
    ComPtr<ID3D11SamplerState> sampler;
    ComPtr<ID3D11Buffer> crop;
    UINT width = 1;
    UINT height = 1;
    std::string adapterName = "Unknown graphics card";
} g;

// Laptops list the built-in Intel or AMD chip first, so the default adapter is the slow one. Ask for the fast one by name.
ComPtr<IDXGIAdapter1> FastestAdapter()
{
    ComPtr<IDXGIFactory6> factory;
    ComPtr<IDXGIAdapter1> adapter;
    if (SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))))
        factory->EnumAdapterByGpuPreference(0, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&adapter));
    return adapter;
}

std::string Utf8(const wchar_t* text)
{
    const int length = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    std::string out(std::max(length - 1, 0), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, -1, out.data(), length, nullptr, nullptr);
    return out;
}

void CreateTarget()
{
    ComPtr<ID3D11Texture2D> backbuffer;
    if (SUCCEEDED(g.swapchain->GetBuffer(0, IID_PPV_ARGS(&backbuffer))))
        g.device->CreateRenderTargetView(backbuffer.Get(), nullptr, &g.target);
}

bool CreatePipeline()
{
    ComPtr<ID3DBlob> vsCode, psCode;
    if (FAILED(D3DCompile(kBlit, sizeof(kBlit) - 1, "blit", nullptr, nullptr, "vs", "vs_5_0", 0, 0, &vsCode, nullptr)) ||
        FAILED(D3DCompile(kBlit, sizeof(kBlit) - 1, "blit", nullptr, nullptr, "ps", "ps_5_0", 0, 0, &psCode, nullptr)))
        return false;

    if (FAILED(g.device->CreateVertexShader(vsCode->GetBufferPointer(), vsCode->GetBufferSize(), nullptr, &g.vs)) ||
        FAILED(g.device->CreatePixelShader(psCode->GetBufferPointer(), psCode->GetBufferSize(), nullptr, &g.ps)))
        return false;

    // The overlay is the same size as the game, so the picture maps one texel to one pixel; point sampling keeps it exact.
    D3D11_SAMPLER_DESC sampler{};
    sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler.MaxLOD = D3D11_FLOAT32_MAX;

    D3D11_BUFFER_DESC crop{};
    crop.ByteWidth = 16;
    crop.Usage = D3D11_USAGE_DEFAULT;
    crop.BindFlags = D3D11_BIND_CONSTANT_BUFFER;

    return SUCCEEDED(g.device->CreateSamplerState(&sampler, &g.sampler)) && SUCCEEDED(g.device->CreateBuffer(&crop, nullptr, &g.crop));
}
} // namespace

bool Init(HWND window)
{
    DXGI_SWAP_CHAIN_DESC desc{};
    desc.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2;
    desc.OutputWindow = window;
    desc.Windowed = TRUE;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

    const ComPtr<IDXGIAdapter1> adapter = FastestAdapter();
    if (FAILED(D3D11CreateDeviceAndSwapChain(adapter.Get(), adapter ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
                                             nullptr, 0, D3D11_SDK_VERSION, &desc, &g.swapchain, &g.device, nullptr, &g.context)))
        return false;

    ComPtr<IDXGIDevice1> dxgi;
    if (SUCCEEDED(g.device.As(&dxgi)))
    {
        // With one frame in flight the overlay is never more than a frame behind the game.
        dxgi->SetMaximumFrameLatency(1);

        ComPtr<IDXGIAdapter> used;
        DXGI_ADAPTER_DESC info{};
        if (SUCCEEDED(dxgi->GetAdapter(&used)) && SUCCEEDED(used->GetDesc(&info)))
            g.adapterName = Utf8(info.Description);
    }

    DXGI_SWAP_CHAIN_DESC actual{};
    g.swapchain->GetDesc(&actual);
    g.width = actual.BufferDesc.Width;
    g.height = actual.BufferDesc.Height;

    CreateTarget();
    return g.target && CreatePipeline();
}

void Shutdown()
{
    if (g.context)
        g.context->ClearState();
    g = State();
}

ID3D11Device* Device()
{
    return g.device.Get();
}

ID3D11DeviceContext* Context()
{
    return g.context.Get();
}

const std::string& AdapterName()
{
    return g.adapterName;
}

void Resize(UINT width, UINT height)
{
    if (!g.swapchain || width == 0 || height == 0 || (width == g.width && height == g.height))
        return;

    g.context->OMSetRenderTargets(0, nullptr, nullptr);
    g.target.Reset();
    g.swapchain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0);
    g.width = width;
    g.height = height;
    CreateTarget();
}

void DrawFrame(ID3D11ShaderResourceView* picture, const float crop[4])
{
    const float clear[4] = {};
    g.context->ClearRenderTargetView(g.target.Get(), clear);
    g.context->OMSetRenderTargets(1, g.target.GetAddressOf(), nullptr);
    if (!picture)
        return;

    const D3D11_VIEWPORT viewport{ 0, 0, static_cast<float>(g.width), static_cast<float>(g.height), 0, 1 };
    g.context->UpdateSubresource(g.crop.Get(), 0, nullptr, crop, 0, 0);
    g.context->RSSetViewports(1, &viewport);
    g.context->IASetInputLayout(nullptr);
    g.context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    g.context->VSSetShader(g.vs.Get(), nullptr, 0);
    g.context->PSSetShader(g.ps.Get(), nullptr, 0);
    g.context->PSSetConstantBuffers(0, 1, g.crop.GetAddressOf());
    g.context->PSSetSamplers(0, 1, g.sampler.GetAddressOf());
    g.context->PSSetShaderResources(0, 1, &picture);
    g.context->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);
    g.context->Draw(3, 0);

    ID3D11ShaderResourceView* none = nullptr;
    g.context->PSSetShaderResources(0, 1, &none);
}

void Present()
{
    g.swapchain->Present(0, 0);
}
} // namespace gfx
