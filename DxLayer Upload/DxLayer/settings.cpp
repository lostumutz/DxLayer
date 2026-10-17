#include "settings.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace
{
constexpr wchar_t kSection[] = L"DxLayer";

std::wstring IniPath()
{
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring file = path;
    file.resize(file.find_last_of(L'\\') + 1);
    return file + L"settings.ini";
}

int ReadInt(const std::wstring& ini, const wchar_t* key, int fallback)
{
    return static_cast<int>(GetPrivateProfileIntW(kSection, key, fallback, ini.c_str()));
}

void WriteInt(const std::wstring& ini, const wchar_t* key, int value)
{
    WritePrivateProfileStringW(kSection, key, std::to_wstring(value).c_str(), ini.c_str());
}

int Percent(float fraction)
{
    return static_cast<int>(std::lround(fraction * 100.0f));
}
} // namespace

Settings LoadSettings()
{
    const std::wstring ini = IniPath();
    Settings s;

    s.showCounter = ReadInt(ini, L"ShowCounter", s.showCounter) != 0;
    s.detailedCounter = ReadInt(ini, L"DetailedCounter", s.detailedCounter) != 0;
    s.counterCorner = static_cast<Corner>(std::clamp(ReadInt(ini, L"CounterCorner", static_cast<int>(s.counterCorner)), 0, 3));
    s.counterBackground = std::clamp(ReadInt(ini, L"CounterBackground", Percent(s.counterBackground)), 0, 100) / 100.0f;
    s.captureCursor = ReadInt(ini, L"CaptureCursor", s.captureCursor) != 0;
    s.closeWithGame = ReadInt(ini, L"CloseWithGame", s.closeWithGame) != 0;
    s.menuKey = std::clamp(ReadInt(ini, L"MenuKey", s.menuKey), 1, 254);
    s.pauseKey = std::clamp(ReadInt(ini, L"PauseKey", s.pauseKey), 1, 254);

    wchar_t hex[16]{};
    GetPrivateProfileStringW(kSection, L"CounterColor", L"", hex, 16, ini.c_str());
    unsigned rgb = 0;
    if (swscanf_s(hex, L"#%6x", &rgb) == 1)
    {
        for (int i = 0; i < 3; ++i)
            s.counterColor[i] = ((rgb >> (16 - 8 * i)) & 0xFF) / 255.0f;
    }
    return s;
}

void SaveSettings(const Settings& s)
{
    const std::wstring ini = IniPath();

    WriteInt(ini, L"ShowCounter", s.showCounter);
    WriteInt(ini, L"DetailedCounter", s.detailedCounter);
    WriteInt(ini, L"CounterCorner", static_cast<int>(s.counterCorner));
    WriteInt(ini, L"CounterBackground", Percent(s.counterBackground));
    WriteInt(ini, L"CaptureCursor", s.captureCursor);
    WriteInt(ini, L"CloseWithGame", s.closeWithGame);
    WriteInt(ini, L"MenuKey", s.menuKey);
    WriteInt(ini, L"PauseKey", s.pauseKey);

    unsigned rgb = 0;
    for (float channel : s.counterColor)
        rgb = (rgb << 8) | static_cast<unsigned>(std::lround(std::clamp(channel, 0.0f, 1.0f) * 255.0f));
    wchar_t hex[16];
    swprintf_s(hex, L"#%06X", rgb);
    WritePrivateProfileStringW(kSection, L"CounterColor", hex, ini.c_str());
}
