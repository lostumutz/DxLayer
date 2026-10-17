#pragma once

#include <d3d11.h>

#include <string>

// The D3D11 device and the overlay's swapchain. ReShade, loaded as dxgi.dll beside the exe, puts its effects on what is presented.
namespace gfx
{
bool Init(HWND window);
void Shutdown();

ID3D11Device* Device();
ID3D11DeviceContext* Context();
const std::string& AdapterName();

void Resize(UINT width, UINT height);
// Clears the backbuffer and, given a picture, stretches the part of it inside `crop` (u0, v0, u1, v1) over the whole of it.
void DrawFrame(ID3D11ShaderResourceView* picture, const float crop[4]);
void Present();
} // namespace gfx
