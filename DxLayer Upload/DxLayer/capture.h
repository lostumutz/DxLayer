#pragma once

#include <d3d11.h>

class FrameStats;

// Windows Graphics Capture of the game's window.
namespace capture
{
bool Init(ID3D11Device* device);

bool Start(HWND window, bool showCursor);
void Stop();
bool Running();
HWND Target();

// Set from the capture's own thread each time the game hands over a frame.
HANDLE FrameEvent();
// Takes the newest frame and drops any older ones still waiting. True if there was a new one.
bool TakeFrame();
ID3D11ShaderResourceView* Picture();
SIZE PictureSize();

void SetCursorCaptured(bool captured);
FrameStats& Stats();
} // namespace capture
