#include "overlay.h"

#include "gfx.h"

#include "imgui/imgui.h"

#include <dwmapi.h>

#pragma comment(lib, "dwmapi.lib")

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND window, UINT message, WPARAM wparam, LPARAM lparam);

namespace overlay
{
namespace
{
constexpr int kOffScreen = -32000;

struct State
{
    HWND window = nullptr;
    HWND game = nullptr;
    SIZE size{};
    bool parked = true;
    bool interactive = false;
    bool quit = false;
} g;

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

LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    if (ImGui_ImplWin32_WndProcHandler(window, message, wparam, lparam))
        return 1;

    switch (message)
    {
    case WM_SIZE:
        if (wparam != SIZE_MINIMIZED)
            gfx::Resize(LOWORD(lparam), HIWORD(lparam));
        return 0;

    case WM_ACTIVATE:
        // Clicking the taskbar button while the menu is closed should land on the game, not on a window that ignores clicks.
        if (LOWORD(wparam) != WA_INACTIVE && !g.interactive && g.game && IsWindow(g.game))
        {
            SetForegroundWindow(g.game);
            return 0;
        }
        break;

    case WM_SYSCOMMAND:
        // Alt on its own would open the window menu.
        if ((wparam & 0xFFF0) == SC_KEYMENU)
            return 0;
        break;

    case WM_CLOSE:
        g.quit = true;
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}
} // namespace

HWND Create(HINSTANCE instance)
{
    WNDCLASSEXW windowClass{ sizeof(windowClass) };
    windowClass.lpfnWndProc = WindowProc;
    windowClass.hInstance = instance;
    windowClass.hIcon = windowClass.hIconSm = LoadIconW(nullptr, IDI_APPLICATION);
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.lpszClassName = L"DxLayer";
    RegisterClassExW(&windowClass);

    // WS_EX_APPWINDOW keeps the taskbar button once the game's window becomes the owner.
    g.window = CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_APPWINDOW, windowClass.lpszClassName, L"DxLayer", WS_POPUP,
                               kOffScreen, kOffScreen, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN), nullptr, nullptr,
                               instance, nullptr);
    if (!g.window)
        return nullptr;

    const MARGINS glass{ -1 };
    DwmExtendFrameIntoClientArea(g.window, &glass);
    return g.window;
}

bool Cover(HWND game, SIZE pictureSize, float crop[4])
{
    RECT client{}, frame{};
    POINT origin{};
    if (!GetClientRect(game, &client) || !ClientToScreen(game, &origin) ||
        FAILED(DwmGetWindowAttribute(game, DWMWA_EXTENDED_FRAME_BOUNDS, &frame, sizeof(frame))))
        return false;

    const int width = client.right;
    const int height = client.bottom;
    if (width <= 0 || height <= 0)
        return false;

    // Owned windows stay above their owner, which keeps the overlay over the game without being topmost over everything.
    if (g.game != game)
    {
        SetWindowLongPtrW(g.window, GWLP_HWNDPARENT, reinterpret_cast<LONG_PTR>(game));
        g.game = game;
    }

    RECT now{};
    GetWindowRect(g.window, &now);
    if (g.parked || now.left != origin.x || now.top != origin.y || now.right - now.left != width || now.bottom - now.top != height)
    {
        SetWindowPos(g.window, nullptr, origin.x, origin.y, width, height, SWP_NOZORDER | SWP_NOACTIVATE);
        g.parked = false;
    }
    g.size = { width, height };

    // The picture is the whole window frame, title bar included, at one texel per pixel from its top left corner.
    if (pictureSize.cx > 0 && pictureSize.cy > 0)
    {
        crop[0] = static_cast<float>(origin.x - frame.left) / pictureSize.cx;
        crop[1] = static_cast<float>(origin.y - frame.top) / pictureSize.cy;
        crop[2] = crop[0] + static_cast<float>(width) / pictureSize.cx;
        crop[3] = crop[1] + static_cast<float>(height) / pictureSize.cy;
    }
    return true;
}

void Park()
{
    if (g.parked)
        return;
    SetWindowPos(g.window, nullptr, kOffScreen, kOffScreen, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    g.parked = true;
}

SIZE Size()
{
    return g.size;
}

void SetInteractive(bool interactive, HWND game)
{
    g.interactive = interactive;
    const LONG_PTR style = GetWindowLongPtrW(g.window, GWL_EXSTYLE);
    SetWindowLongPtrW(g.window, GWL_EXSTYLE, interactive ? style & ~WS_EX_TRANSPARENT : style | WS_EX_TRANSPARENT);

    if (interactive)
        BringToFront(g.window);
    else if (game && GetForegroundWindow() == g.window)
        SetForegroundWindow(game);
}

bool QuitRequested()
{
    return g.quit;
}
} // namespace overlay
