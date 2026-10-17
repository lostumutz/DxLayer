#pragma once

#include "settings.h"

#include <d3d11.h>

class FrameStats;

// The menu and the FPS counter, drawn with Dear ImGui over the game's picture.
namespace ui
{
bool Init(HWND window, ID3D11Device* device, ID3D11DeviceContext* context);
void Shutdown();

struct Status
{
    const char* adapter;
    bool capturing;
    SIZE gameArea;
    const FrameStats* stats;
};

enum class Action { None, CloseMenu, Quit };

// Draws the counter, and the menu when `menuOpen`, into the current render target. Settings are edited in place.
Action Draw(Settings& settings, const Status& status, bool menuOpen);

// True while a key binding is waiting for its new key (and until that key is let go), so the key doesn't also do its old job.
bool ListeningForKey();
} // namespace ui
