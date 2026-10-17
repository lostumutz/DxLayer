#pragma once

#include <windows.h>

// The window the game's picture is shown in. It sits exactly over the game's client area and lets clicks through to the game
// while the menu is closed. While the game isn't in front it is moved off screen rather than hidden, which would take its taskbar
// button away.
namespace overlay
{
HWND Create(HINSTANCE instance);

// Puts the overlay over `game`'s client area and works out which part of the captured picture (`pictureSize`, the game's whole
// window frame) that is. False if there is nothing to cover.
bool Cover(HWND game, SIZE pictureSize, float crop[4]);
void Park();
SIZE Size();

// Interactive while the menu is open: it takes clicks and keys, and is brought in front of the game so they reach it.
void SetInteractive(bool interactive, HWND game);

bool QuitRequested();
} // namespace overlay
