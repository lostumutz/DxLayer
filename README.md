# DxLayer

## Introduction

DxLayer is a small Windows overlay that puts ReShade on Roblox without touching the Roblox process. It captures the game window with Windows Graphics Capture (the same API OBS uses for window capture), draws each frame into a Direct3D 11 swapchain of its own, and lets ReShade apply its effects there. The overlay window sits exactly over the game's client area and lets every click through, so you play the game as normal and see it with effects on top.

Nothing is injected, hooked or patched in Roblox. ReShade is loaded into DxLayer's own process, as `dxgi.dll` beside `DxLayer.exe`, the way it would be for any other DirectX 11 program.

> **Note** DxLayer only shows what Roblox draws. Effects that need a depth buffer (SSR, MXAO, depth of field and so on) have no real depth to work with, because the capture holds back buffer only. Effects that work on the final image (colour grading, sharpening, bloom, film grain, LUTs) work as they would anywhere else.

The workflow goes like this: DxLayer starts → looks for the Roblox window → starts a capture session on it → each new frame is drawn into DxLayer's swapchain → ReShade applies its effects when DxLayer presents → the result appears over the game.

DxLayer started as a small personal WGC overlay in June 2026, and was rewritten in October 2026 into what is here. It was developed on a laptop with an RTX 3050 Laptop GPU next to an Intel iGPU (NVIDIA Optimus), which is where the first problem below came from.

## High-Level Overview

### Picking the right graphics card

On a laptop with two GPUs, the default adapter is the built-in Intel or AMD chip. Creating the device on it puts the overlay, and every ReShade effect, on the slow GPU, and ReShade's menu reports the wrong card. DxLayer asks DXGI for the high-performance adapter by name and creates the device on that:

```cpp
// Laptops list the built-in Intel or AMD chip first, so the default adapter is the slow one. Ask for the fast one by name.
ComPtr<IDXGIAdapter1> FastestAdapter()
{
    ComPtr<IDXGIFactory6> factory;
    ComPtr<IDXGIAdapter1> adapter;
    if (SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))))
        factory->EnumAdapterByGpuPreference(0, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&adapter));
    return adapter;
}
```

```cpp
const ComPtr<IDXGIAdapter1> adapter = FastestAdapter();
if (FAILED(D3D11CreateDeviceAndSwapChain(adapter.Get(), adapter ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
                                         nullptr, 0, D3D11_SDK_VERSION, &desc, &g.swapchain, &g.device, nullptr, &g.context)))
    return false;
```

With an explicit adapter the driver type has to be `D3D_DRIVER_TYPE_UNKNOWN`. Passing `HARDWARE` together with an adapter fails.

The swapchain is a flip-model swapchain with at most one frame queued, so what is on screen is never more than a frame behind the game:

```cpp
// With one frame in flight the overlay is never more than a frame behind the game.
dxgi->SetMaximumFrameLatency(1);
```

### Capturing the game

The capture session is set up once per Roblox window. Two settings matter here. On Windows 11 a capture session hands over at most 60 frames a second unless it is told otherwise, which caps the overlay at 60 fps no matter how fast the game runs. The yellow border Windows draws around a captured window can also be turned off, once the app has asked for borderless access:

```cpp
g.session = g.pool.CreateCaptureSession(item);
if (g.borderless)
    g.session.IsBorderRequired(false);
// Left alone, Windows 11 hands over at most 60 frames a second.
if (SessionHas(L"MinUpdateInterval"))
    g.session.MinUpdateInterval(std::chrono::milliseconds(1));
if (SessionHas(L"IsCursorCaptureEnabled"))
    g.session.IsCursorCaptureEnabled(showCursor);
g.session.StartCapture();
```

Each property is checked with `ApiInformation` first, so the same exe runs on Windows 10 and just keeps the older behaviour there.

The frame pool is free-threaded, and its `FrameArrived` event only signals a Win32 event. The main loop sleeps on that event instead of spinning, so DxLayer draws once per game frame and sits idle in between:

```cpp
g.arrived = g.pool.FrameArrived(winrt::auto_revoke, [](auto&&, auto&&) { SetEvent(g.frameEvent); });
```

```cpp
// Wakes for each frame the game hands over. With the menu open it also wakes often enough to follow the mouse.
const HANDLE frameEvent = capture::FrameEvent();
MsgWaitForMultipleObjects(1, &frameEvent, FALSE, menuOpen ? 8 : 50, QS_ALLINPUT);
```

When it wakes it takes the newest frame and drops any older ones still waiting, so a slow frame never builds up a queue. The frame on screen is held until the next one replaces it, so the pool can't write into a buffer that is still being shown:

```cpp
wgc::Direct3D11CaptureFrame newest{ nullptr };
while (wgc::Direct3D11CaptureFrame frame = g.pool.TryGetNextFrame())
{
    newest = frame;
    g.stats.Add(ClockSeconds());
}
if (!newest)
    return false;
```

When the game window is resized, the pool's buffers have to follow. The frame that reports the new size is shown as it is, and the pool is recreated before the next frame is taken.

### Showing it in the right place

The captured picture is the whole window frame, title bar and borders included. The overlay covers only the client area, so each frame works out which part of the picture that is. Because the picture maps one texel to one pixel from its top left corner, the crop is plain arithmetic on the window rectangles:

```cpp
// The picture is the whole window frame, title bar included, at one texel per pixel from its top left corner.
if (pictureSize.cx > 0 && pictureSize.cy > 0)
{
    crop[0] = static_cast<float>(origin.x - frame.left) / pictureSize.cx;
    crop[1] = static_cast<float>(origin.y - frame.top) / pictureSize.cy;
    crop[2] = crop[0] + static_cast<float>(width) / pictureSize.cx;
    crop[3] = crop[1] + static_cast<float>(height) / pictureSize.cy;
}
```

A full-screen triangle draws that part of the picture over the whole backbuffer. The overlay is the same size as the client area, so point sampling keeps it pixel-exact:

```hlsl
float4 ps(float4 position : SV_Position, float2 uv : TEXCOORD) : SV_Target
{
    return float4(picture.Sample(texel, lerp(area.xy, area.zw, uv)).rgb, 1);
}
```

### The overlay window

The window is a borderless popup with three extended styles. `WS_EX_LAYERED | WS_EX_TRANSPARENT` makes it ignore the mouse, so clicks go straight to the game. `WS_EX_APPWINDOW` gives it a taskbar button:

```cpp
// WS_EX_APPWINDOW keeps the taskbar button once the game's window becomes the owner.
g.window = CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_APPWINDOW, windowClass.lpszClassName, L"DxLayer", WS_POPUP,
                           kOffScreen, kOffScreen, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN), nullptr, nullptr,
                           instance, nullptr);
```

The game window is made the overlay's owner. Owned windows always stay above their owner, so the overlay stays over the game without being topmost over everything else on the desktop:

```cpp
// Owned windows stay above their owner, which keeps the overlay over the game without being topmost over everything.
if (g.game != game)
{
    SetWindowLongPtrW(g.window, GWLP_HWNDPARENT, reinterpret_cast<LONG_PTR>(game));
    g.game = game;
}
```

When the game isn't the active window, the overlay is moved off screen rather than hidden. Hiding it would remove its taskbar button. Clicking that button while the menu is closed hands focus straight to the game, since a window that ignores clicks is no use in front.

### The panel and ReShade

Pressing **End** (rebindable) opens DxLayer's panel. The overlay stops ignoring clicks and is brought in front of the game. Windows only lets the foreground app pass the foreground on, so DxLayer briefly attaches to the game's input state for the call:

```cpp
// Windows only lets the foreground app hand the foreground on, so borrow its input state for the call.
void BringToFront(HWND window)
{
    const DWORD front = GetWindowThreadProcessId(GetForegroundWindow(), nullptr);
    const DWORD self = GetCurrentThreadId();
    const bool attach = front != 0 && front != self;
    if (attach)
        AttachThreadInput(front, self, TRUE);
    SetForegroundWindow(window);
    if (attach)
        AttachThreadInput(front, self, FALSE);
}
```

ReShade reads its keys from the window it draws into, which is DxLayer's window, not the game's. That is why **Home** opens ReShade while the panel is open, and why closing the panel hands the keyboard back to the game.

The panel is drawn with Dear ImGui on its own draw lists rather than stock widgets. Every row is one hit area, so clicking a setting's label works as well as clicking its checkbox. Only the header moves the panel, so a slightly missed click never drags it.

### Frame statistics

The counter reads its numbers from the capture: the gaps between frames as Windows hands them over. That is the rate the game is really delivering frames to the screen. The 1% low is the frame rate of the slowest 1% of the last 240 frames:

```cpp
// The "1% low": the frame rate the slowest 1% of recent frames ran at.
float SlowestPercentFps() const
{
    if (count_ < 20)
        return 0.0f;
    std::array<float, kHistory> sorted;
    History(sorted.data());
    const int slowest = count_ / 100;
    std::nth_element(sorted.begin(), sorted.begin() + slowest, sorted.begin() + count_, std::greater<>());
    return sorted[slowest] > 0 ? 1000.0f / sorted[slowest] : 0.0f;
}
```

The summary numbers update twice a second, so they can be read rather than flicker.

## Flowchart

```
┌──────────────────────────────┐
│ DxLayer.exe starts           │
│  (ReShade loads as dxgi.dll) │
└──────────────┬───────────────┘
               │
               v
┌──────────────────────────────────────────────┐
│ Create D3D11 device + flip swapchain         │
│  - high-performance adapter (not the iGPU)   │
│  - max frame latency 1                       │
└──────────────┬───────────────────────────────┘
               │
               v
┌──────────────────────────────────────────────┐
│ Find the "Roblox" window                     │
│  - not found: park off screen, keep looking  │
└──────────────┬───────────────────────────────┘
               │
               v
┌──────────────────────────────────────────────┐
│ Start Windows Graphics Capture on it         │
│  - MinUpdateInterval 1 ms (no 60 fps cap)    │
│  - no yellow border, cursor optional         │
│  - FrameArrived → signal event               │
└──────────────┬───────────────────────────────┘
               │
               v
┌──────────────────────────────────────────────┐
│ Main loop (wakes per game frame)             │
│  - take newest frame, drop older ones        │
│  - game not in front: park, skip drawing     │
│  - move overlay over the client area         │
│  - work out the crop from the window rects   │
└──────────────┬───────────────────────────────┘
               │
               v
┌──────────────────────────────────────────────┐
│ Draw                                         │
│  - full-screen triangle samples the crop     │
│  - ImGui: FPS counter, panel when open       │
└──────────────┬───────────────────────────────┘
               │
               v
┌──────────────────────────────────────────────┐
│ Present()                                    │
│  - ReShade applies its effects here          │
│  - DWM shows the result over the game        │
└──────────────────────────────────────────────┘
```

## Using it

1. Build `DxLayer.exe`, or take it from a release.
2. Run the ReShade installer, point it at `DxLayer.exe`, and pick **DirectX 10/11/12**. This puts ReShade's `dxgi.dll` beside the exe.
3. Pick the shader packs you want in the installer.
4. Start Roblox, then DxLayer. Click into the game and it attaches by itself.

| Key | Does |
|---|---|
| **End** | Opens and closes the panel |
| **Home** | Opens ReShade, while the panel is open |
| **F1** | Pauses the overlay, showing the game without effects |

The panel and pause keys can be changed on the panel's Keys tab. Settings are kept in `settings.ini` beside the exe.

## Building

Open `DxLayer.slnx` in Visual Studio 2026 with the "Desktop development with C++" workload, and build **Release | x64**. The only dependency is Dear ImGui, which is included in `DxLayer/imgui`. Everything else comes with the Windows SDK.

| File | Does |
|---|---|
| `main.cpp` | The main loop: finding the game, keys, when to draw |
| `capture.cpp` | Windows Graphics Capture session and frames |
| `gfx.cpp` | Device, swapchain, and the blit to the backbuffer |
| `overlay.cpp` | The overlay window: placement, click-through, focus |
| `ui.cpp` | The panel and the FPS counter |
| `widgets.cpp`, `draw.cpp` | The panel's controls and drawing helpers |
| `settings.cpp` | `settings.ini` |

## Known limitations

- **Windows 10** can't lift the 60 fps capture cap or hide the yellow capture border; both need Windows 11.
- **No depth.** Effects that need the game's depth buffer don't have one.
- **About a frame of latency.** Capture adds roughly one frame between the game drawing and you seeing it.
- **The panel takes the clicks.** While the panel is open, clicks over the game go to the overlay, not to Roblox.
- **ReShade's key needs the panel.** ReShade only gets its key while the panel is open.
- **Exclusive fullscreen.** Roblox has to be windowed or borderless; capture can't see exclusive fullscreen.

## Disclaimer

DxLayer isn't affiliated with Roblox or ReShade. It doesn't read or change the Roblox process; it only captures the game's window, like a screen recorder. Even so, how any game's rules treat overlays is up to the game, so use it at your own risk.

## Credits and licenses

- [Dear ImGui](https://github.com/ocornut/imgui) by Omar Cornut, MIT License.
- [ReShade](https://github.com/crosire/reshade) by crosire, BSD 3-Clause License. ReShade isn't included in this repository.
- Shader packs belong to their authors and come under their own licenses. Get them through the ReShade installer.

