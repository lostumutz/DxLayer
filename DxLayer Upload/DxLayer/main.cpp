#include "capture.h"
#include "frame_stats.h"
#include "gfx.h"
#include "overlay.h"
#include "settings.h"
#include "ui.h"

#include <windows.h>

#include <winrt/base.h>

namespace
{
constexpr wchar_t kGameTitle[] = L"Roblox";

// Turns "is the key down" into "was it just pressed" for the keys DxLayer listens to.
class KeyWatcher
{
public:
    bool Pressed(int vk)
    {
        const bool down = (GetAsyncKeyState(vk) & 0x8000) != 0;
        const bool pressed = down && !down_[vk & 0xFF];
        down_[vk & 0xFF] = down;
        return pressed;
    }

private:
    bool down_[256]{};
};

bool PumpMessages()
{
    MSG message;
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
    {
        if (message.message == WM_QUIT)
            return false;
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return true;
}

// Minimising the overlay leaves the game showing as it is, without effects, and costs nothing while it stays down.
void TogglePause(HWND window)
{
    ShowWindow(window, IsIconic(window) ? SW_SHOWNOACTIVATE : SW_SHOWMINNOACTIVE);
}
} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int)
{
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    winrt::init_apartment();

    Settings settings = LoadSettings();
    const HWND window = overlay::Create(instance);
    if (!window || !gfx::Init(window) || !capture::Init(gfx::Device()) || !ui::Init(window, gfx::Device(), gfx::Context()))
    {
        MessageBoxW(nullptr, L"DxLayer needs Direct3D 11 and Windows Graphics Capture, and one of them couldn't be started.", L"DxLayer",
                    MB_ICONERROR);
        return 1;
    }
    // Shown once, off screen, so the taskbar button is there from the start.
    ShowWindow(window, SW_SHOWNA);

    KeyWatcher keys;
    bool menuOpen = false;
    bool sawGame = false;
    bool cursorCaptured = settings.captureCursor;
    ULONGLONG retryAt = 0;

    const auto setMenu = [&](bool open, HWND game)
    {
        if (open == menuOpen)
            return;
        menuOpen = open;
        overlay::SetInteractive(open, game);
        if (!open)
            SaveSettings(settings);
    };

    while (!overlay::QuitRequested())
    {
        // Wakes for each frame the game hands over. With the menu open it also wakes often enough to follow the mouse.
        const HANDLE frameEvent = capture::FrameEvent();
        MsgWaitForMultipleObjects(1, &frameEvent, FALSE, menuOpen ? 8 : 50, QS_ALLINPUT);
        if (!PumpMessages())
            break;

        const HWND game = FindWindowW(nullptr, kGameTitle);
        if (!game)
        {
            capture::Stop();
            setMenu(false, nullptr);
            overlay::Park();
            if (sawGame && settings.closeWithGame)
                break;
            continue;
        }
        sawGame = true;

        if (capture::Target() != game && GetTickCount64() >= retryAt)
        {
            capture::Stop();
            if (!capture::Start(game, settings.captureCursor))
                retryAt = GetTickCount64() + 1000;
        }

        const bool newFrame = capture::TakeFrame();
        capture::Stats().Tick(ClockSeconds());

        const HWND front = GetForegroundWindow();
        if ((front != game && front != window) || IsIconic(game))
        {
            setMenu(false, game);
            overlay::Park();
            continue;
        }

        if (!ui::ListeningForKey())
        {
            if (keys.Pressed(settings.menuKey))
                setMenu(!menuOpen, game);
            if (keys.Pressed(settings.pauseKey))
            {
                setMenu(false, game);
                TogglePause(window);
            }
        }

        if (IsIconic(window))
            continue;

        float crop[4] = { 0, 0, 1, 1 };
        if (!overlay::Cover(game, capture::PictureSize(), crop))
            continue;

        // With nothing new from the game and nothing of DxLayer's own on screen, there is nothing to redraw.
        if (!newFrame && !menuOpen && !settings.showCounter)
            continue;

        gfx::DrawFrame(capture::Picture(), crop);
        const ui::Status status{ gfx::AdapterName().c_str(), capture::Running(), overlay::Size(), &capture::Stats() };
        const ui::Action action = ui::Draw(settings, status, menuOpen);
        gfx::Present();

        if (action == ui::Action::Quit)
            break;
        if (action == ui::Action::CloseMenu)
            setMenu(false, game);

        if (settings.captureCursor != cursorCaptured)
        {
            capture::SetCursorCaptured(settings.captureCursor);
            cursorCaptured = settings.captureCursor;
        }
    }

    SaveSettings(settings);
    ui::Shutdown();
    capture::Stop();
    gfx::Shutdown();
    return 0;
}
