#pragma once

#include <windows.h>

enum class Corner { TopLeft, TopRight, BottomLeft, BottomRight };

struct Settings
{
    bool showCounter = false;
    bool detailedCounter = true;
    Corner counterCorner = Corner::TopRight;
    float counterColor[3] = { 252 / 255.0f, 252 / 255.0f, 200 / 255.0f };
    float counterBackground = 0.55f;

    bool captureCursor = false;
    bool closeWithGame = false;

    int menuKey = VK_END;
    int pauseKey = VK_F1;
};

// Kept in settings.ini beside the exe.
Settings LoadSettings();
void SaveSettings(const Settings& settings);
