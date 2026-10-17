#include "ui.h"

#include "draw.h"
#include "frame_stats.h"
#include "widgets.h"

#include "imgui/imgui.h"
#include "imgui/imgui_impl_dx11.h"
#include "imgui/imgui_impl_win32.h"

#include <algorithm>
#include <cmath>
#include <string>

using draw::Canvas;
using draw::Face;
using draw::Format;
using draw::Hit;
using draw::Type;
namespace pal = draw::palette;
namespace type = draw::type;

namespace ui
{
namespace
{
// Panel layout, in layout units
constexpr float kWidth = 380;
constexpr float kPad = 16;
constexpr float kRight = kWidth - kPad;
constexpr float kHeader = 44;
constexpr float kTabs = 32;
constexpr float kRow = 36;
constexpr float kBodyTop = kHeader + kTabs + 8;
constexpr float kBodyHeight = kRow * 5 + 12;
constexpr float kFooter = 40;
constexpr float kHeight = kBodyTop + kBodyHeight + 8 + kFooter;
constexpr float kRounding = 7;

enum class Tab { Counter, Capture, Keys };

struct KeyCapture
{
    int* target = nullptr;  // the binding being changed
    int previous = 0;
    int waitForRelease = 0; // the key just bound, until it is let go
    bool held[256]{};
};

HWND g_window = nullptr;
std::string g_iniPath;
float g_scale = 1.0f;
Tab g_tab = Tab::Counter;
KeyCapture g_keys;

int Round(float value)
{
    return static_cast<int>(std::lround(value));
}

std::string Utf8(const wchar_t* text)
{
    const int length = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    std::string out(std::max(length - 1, 0), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, -1, out.data(), length, nullptr, nullptr);
    return out;
}

std::string ExeFolder()
{
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring folder = path;
    folder.resize(folder.find_last_of(L'\\') + 1);
    return Utf8(folder.c_str());
}

unsigned PackRgb(const float rgb[3])
{
    unsigned packed = 0;
    for (int i = 0; i < 3; ++i)
        packed = (packed << 8) | static_cast<unsigned>(std::lround(std::clamp(rgb[i], 0.0f, 1.0f) * 255.0f));
    return packed;
}

void UnpackRgb(unsigned packed, float rgb[3])
{
    for (int i = 0; i < 3; ++i)
        rgb[i] = ((packed >> (16 - 8 * i)) & 0xFF) / 255.0f;
}

std::string KeyName(int vk)
{
    switch (vk)
    {
    case VK_END: return "End";
    case VK_HOME: return "Home";
    case VK_INSERT: return "Insert";
    case VK_DELETE: return "Delete";
    case VK_PRIOR: return "PgUp";
    case VK_NEXT: return "PgDn";
    case VK_TAB: return "Tab";
    case VK_CAPITAL: return "Caps";
    case VK_SPACE: return "Space";
    case VK_RETURN: return "Enter";
    case VK_BACK: return "Backspace";
    case VK_PAUSE: return "Pause";
    case VK_SCROLL: return "ScrLk";
    case VK_SNAPSHOT: return "PrtSc";
    case VK_LEFT: return "Left";
    case VK_RIGHT: return "Right";
    case VK_UP: return "Up";
    case VK_DOWN: return "Down";
    }
    if (vk >= VK_F1 && vk <= VK_F24)
        return Format("F%d", vk - VK_F1 + 1);
    if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9)
        return Format("Num %d", vk - VK_NUMPAD0);
    if ((vk >= '0' && vk <= '9') || (vk >= 'A' && vk <= 'Z'))
        return std::string(1, static_cast<char>(vk));

    // Punctuation and the rest, named the way the keyboard layout names them.
    wchar_t name[32]{};
    const UINT scan = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
    if (scan && GetKeyNameTextW(static_cast<LONG>(scan << 16), name, 32) > 0)
        return Utf8(name);
    return Format("Key %d", vk);
}

bool IsModifier(int vk)
{
    switch (vk)
    {
    case VK_SHIFT: case VK_CONTROL: case VK_MENU: case VK_LWIN: case VK_RWIN:
    case VK_LSHIFT: case VK_RSHIFT: case VK_LCONTROL: case VK_RCONTROL: case VK_LMENU: case VK_RMENU:
        return true;
    }
    return false;
}

void ListenFor(int& key)
{
    g_keys.target = &key;
    g_keys.previous = key;
    for (int vk = 0; vk < 256; ++vk)
        g_keys.held[vk] = (GetAsyncKeyState(vk) & 0x8000) != 0;
}

// Takes the next key pressed for the binding being changed. Escape cancels, and Home is left alone because ReShade uses it.
void PollKeyCapture(Settings& settings)
{
    if (g_keys.waitForRelease && !(GetAsyncKeyState(g_keys.waitForRelease) & 0x8000))
        g_keys.waitForRelease = 0;
    if (!g_keys.target)
        return;

    for (int vk = VK_BACK; vk < 256; ++vk)
    {
        const bool down = (GetAsyncKeyState(vk) & 0x8000) != 0;
        const bool pressed = down && !g_keys.held[vk];
        g_keys.held[vk] = down;
        if (!pressed || IsModifier(vk) || vk == VK_HOME)
            continue;

        if (vk != VK_ESCAPE)
        {
            // Giving one action the other's key swaps them rather than leaving both on the same key.
            int& other = g_keys.target == &settings.menuKey ? settings.pauseKey : settings.menuKey;
            if (other == vk)
                other = g_keys.previous;
            *g_keys.target = vk;
        }
        g_keys.target = nullptr;
        g_keys.waitForRelease = vk;
        return;
    }
}

float GraphTop(const float* times, int count)
{
    const float worst = count > 0 ? *std::max_element(times, times + count) : 0.0f;
    return std::max(25.0f, worst * 1.2f);
}

// Frame times as a line with a light fill under it, newest at the right edge.
void FrameGraph(const Canvas& c, float x, float y, float w, float h, const float* times, int count, ImU32 color)
{
    if (count < 2)
        return;

    const float top = GraphTop(times, count);
    ImVec2 points[FrameStats::kHistory];
    const float step = w / (FrameStats::kHistory - 1);
    for (int i = 0; i < count; ++i)
        points[i] = c.At(x + w - (count - 1 - i) * step, y + h - h * std::min(times[i] / top, 1.0f));

    // Anti-aliased fill would leave a faint seam between each strip.
    ImDrawList* list = c.List();
    const ImDrawListFlags flags = list->Flags;
    list->Flags &= ~ImDrawListFlags_AntiAliasedFill;
    const float bottom = c.At(0, y + h).y;
    const ImU32 shade = draw::Fade(color, 0.12f);
    for (int i = 1; i < count; ++i)
        list->AddQuadFilled(points[i - 1], points[i], ImVec2(points[i].x, bottom), ImVec2(points[i - 1].x, bottom), shade);
    list->Flags = flags;

    list->AddPolyline(points, count, color, ImDrawFlags_None, c.Px(1.25f));
}

void DrawCounter(const Settings& settings, const FrameStats& stats, bool live)
{
    constexpr Type kNumber{ Face::Mono, 17 };
    constexpr Type kUnit{ Face::Strong, 10.5f };
    constexpr Type kDetail{ Face::Mono, 11.5f };
    constexpr float kPadX = 10, kPadY = 7, kMargin = 12, kGraphHeight = 24, kDetailedWidth = 132;

    const float* rgb = settings.counterColor;
    const ImU32 ink = ImGui::ColorConvertFloat4ToU32(ImVec4(rgb[0], rgb[1], rgb[2], 1.0f));
    const std::string fps = live ? Format("%d", Round(stats.Fps())) : "--";

    // Sized for three digits, so the box doesn't twitch as the number changes.
    const Canvas measure(nullptr, ImVec2(), g_scale);
    const float numberWidth = std::max(measure.Measure(kNumber, fps), measure.Measure(kNumber, "000"));
    const float lineWidth = numberWidth + 5 + measure.Measure(kUnit, "FPS");
    const bool detailed = settings.detailedCounter;
    const float width = detailed ? std::max(lineWidth, kDetailedWidth) : lineWidth;
    const float height = detailed ? kNumber.size + 3 + kDetail.size + 7 + kGraphHeight : kNumber.size;
    const float boxWidth = width + kPadX * 2;
    const float boxHeight = height + kPadY * 2;

    const ImGuiViewport* view = ImGui::GetMainViewport();
    const Corner corner = settings.counterCorner;
    const bool right = corner == Corner::TopRight || corner == Corner::BottomRight;
    const bool bottom = corner == Corner::BottomLeft || corner == Corner::BottomRight;
    const ImVec2 origin(right ? view->Pos.x + view->Size.x - (boxWidth + kMargin) * g_scale : view->Pos.x + kMargin * g_scale,
                        bottom ? view->Pos.y + view->Size.y - (boxHeight + kMargin) * g_scale : view->Pos.y + kMargin * g_scale);

    const Canvas c(ImGui::GetBackgroundDrawList(), origin, g_scale);
    c.Fill(0, 0, boxWidth, boxHeight, draw::Hex(0x000000, static_cast<unsigned>(settings.counterBackground * 255)), 5);
    c.Text(kPadX + numberWidth - c.Measure(kNumber, fps), kPadY, kNumber, ink, fps);
    c.Text(kPadX + numberWidth + 5, kPadY + c.Ascent(kNumber) - c.Ascent(kUnit), kUnit, draw::Fade(ink, 0.65f), "FPS");
    if (!detailed)
        return;

    const float y = kPadY + kNumber.size + 3;
    const ImU32 soft = draw::Fade(ink, 0.6f);
    c.Text(kPadX, y, kDetail, soft, live ? Format("%.1f ms", stats.FrameMs()) : std::string("-- ms"));
    const std::string low = live && stats.LowFps() > 0 ? Format("1%% low %d", Round(stats.LowFps())) : std::string("1% low --");
    c.Text(kPadX + width - c.Measure(kDetail, low), y, kDetail, soft, low);

    float times[FrameStats::kHistory];
    const int count = live ? stats.History(times) : 0;
    FrameGraph(c, kPadX, y + kDetail.size + 7, width, kGraphHeight, times, count, ink);
}

float Middle(float y, float height)
{
    return y + (kRow - height) / 2;
}

void Label(const Canvas& c, float y, const char* text)
{
    c.TextMiddle(kPad, y, kRow, type::kLabel, pal::kText, text);
}

// A row that works as one big button: hovering lights the whole row, and a click anywhere on it counts.
Hit RowButton(const Canvas& c, const char* id, float y)
{
    const float x = kPad - 8, w = kWidth - kPad * 2 + 16;
    const Hit hit = c.Area(id, x, y + 2, w, kRow - 4);
    if (hit.hovered)
        c.Fill(x, y + 2, w, kRow - 4, hit.held ? pal::kWell : pal::kHover, 5);
    return hit;
}

void CheckRow(const Canvas& c, const char* id, float y, const char* label, bool& value)
{
    const Hit hit = RowButton(c, id, y);
    if (hit.clicked)
        value = !value;
    Label(c, y, label);
    widgets::Checkbox(c, id, kRight - widgets::kCheckSize, Middle(y, widgets::kCheckSize), value, hit.hovered);
}

void InfoRow(const Canvas& c, float y, const char* label, std::string value, Type valueType)
{
    Label(c, y, label);
    const float room = kRight - (kPad + c.Measure(type::kLabel, label) + 20);
    if (c.Measure(valueType, value) > room)
    {
        while (!value.empty() && c.Measure(valueType, value + "…") > room)
            value.pop_back();
        value += "…";
    }
    c.TextRight(kRight, y, kRow, valueType, pal::kMuted, value);
}

bool DrawHeader(const Canvas& c, const Status& status)
{
    // The header is the handle for moving the panel, so a slightly missed click elsewhere never drags it.
    const Hit handle = c.Area("panel.move", 0, 0, kWidth - 44, kHeader, false);
    if (handle.held)
    {
        const ImVec2 delta = ImGui::GetIO().MouseDelta;
        const ImVec2 pos = ImGui::GetWindowPos();
        ImGui::SetWindowPos(ImVec2(pos.x + delta.x, pos.y + delta.y));
    }
    if (handle.hovered || handle.held)
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);

    c.TextMiddle(kPad, 0, kHeader, type::kTitle, pal::kText, "DxLayer");
    const bool live = status.capturing && status.stats->Fps() > 0;
    c.TextRight(kRight - 32, 0, kHeader, type::kMonoSmall, pal::kMuted, live ? Format("%d fps", Round(status.stats->Fps())) : "idle");
    return widgets::CloseButton(c, "panel.close", kRight - 18, (kHeader - 22) / 2);
}

void DrawTabs(const Canvas& c)
{
    constexpr const char* kNames[] = { "Counter", "Capture", "Keys" };
    const float bottom = kHeader + kTabs;
    c.Line(0, bottom - 1, kWidth, bottom - 1, pal::kRule);

    float x = kPad;
    float activeX = kPad, activeWidth = 0;
    for (int i = 0; i < 3; ++i)
    {
        const float w = c.Measure(type::kTab, kNames[i]);
        ImGui::PushID(i);
        const Hit hit = c.Area("panel.tab", x - 8, kHeader, w + 16, kTabs);
        ImGui::PopID();
        if (hit.clicked)
            g_tab = static_cast<Tab>(i);

        const bool active = static_cast<int>(g_tab) == i;
        c.TextMiddle(x, kHeader, kTabs - 2, type::kTab, active ? pal::kText : hit.hovered ? pal::kSoft : pal::kMuted, kNames[i]);
        if (active)
        {
            activeX = x;
            activeWidth = w;
        }
        x += w + 22;
    }

    const float underlineX = draw::Approach(ImGui::GetID("panel.tab.x"), activeX);
    const float underlineWidth = draw::Approach(ImGui::GetID("panel.tab.w"), activeWidth);
    c.Fill(underlineX, bottom - 2, underlineWidth, 2, pal::kAccent, 1);
}

void DrawColors(const Canvas& c, float y, float rgb[3])
{
    constexpr unsigned kPresets[] = { 0xF2F2F3, 0xFCFCC8, 0x7CF29A, 0x6FD6FF, 0xFFC15E, 0xFF7AA8 };
    constexpr int kCount = sizeof(kPresets) / sizeof(kPresets[0]);

    const unsigned current = PackRgb(rgb);
    const float top = Middle(y, widgets::kSwatchSize);
    float x = kRight - widgets::kSwatchSize - widgets::kSwatchPitch * kCount;

    bool preset = false;
    for (unsigned color : kPresets)
    {
        const bool selected = color == current;
        preset |= selected;
        if (widgets::Swatch(c, Format("counter.swatch.%06X", color).c_str(), x, top, draw::Hex(color), selected))
            UnpackRgb(color, rgb);
        x += widgets::kSwatchPitch;
    }

    if (widgets::CustomSwatch(c, "counter.custom", x, top, !preset))
    {
        ImGui::SetNextWindowPos(c.At(kRight, top + widgets::kSwatchSize + 8), ImGuiCond_Always, ImVec2(1.0f, 0.0f));
        ImGui::OpenPopup("counter.color");
    }
    if (ImGui::BeginPopup("counter.color"))
    {
        ImGui::SetNextItemWidth(c.Px(190));
        ImGui::ColorPicker3("##counter.color", rgb,
                            ImGuiColorEditFlags_NoSidePreview | ImGuiColorEditFlags_PickerHueBar | ImGuiColorEditFlags_DisplayHex);
        ImGui::EndPopup();
    }
}

void DrawCounterTab(const Canvas& c, Settings& settings)
{
    float y = kBodyTop;
    CheckRow(c, "counter.show", y, "Show FPS counter", settings.showCounter);
    y += kRow;

    constexpr const char* kStyles[] = { "Compact", "Detailed" };
    int style = settings.detailedCounter ? 1 : 0;
    Label(c, y, "Style");
    if (widgets::Choice(c, "counter.style", kRight - 160, Middle(y, widgets::kChoiceHeight), 160, kStyles, 2, style))
        settings.detailedCounter = style == 1;
    y += kRow;

    int corner = static_cast<int>(settings.counterCorner);
    Label(c, y, "Corner");
    if (widgets::CornerGrid(c, "counter.corner", kRight - widgets::kCornerWidth, Middle(y, widgets::kCornerHeight), corner))
        settings.counterCorner = static_cast<Corner>(corner);
    y += kRow;

    Label(c, y, "Color");
    DrawColors(c, y, settings.counterColor);
    y += kRow;

    constexpr float kValueWidth = 40, kSliderWidth = 136;
    Label(c, y, "Background");
    c.TextRight(kRight, y, kRow, type::kMono, pal::kMuted, Format("%d%%", Round(settings.counterBackground * 100)));
    widgets::Slider(c, "counter.background", kRight - kValueWidth - kSliderWidth, Middle(y, widgets::kChoiceHeight), kSliderWidth,
                    settings.counterBackground);
}

void DrawCaptureTab(const Canvas& c, Settings& settings, const Status& status)
{
    const FrameStats& stats = *status.stats;
    const bool live = status.capturing && stats.Fps() > 0;

    float y = kBodyTop;
    InfoRow(c, y, "Graphics card", status.adapter, type::kLabel);
    y += kRow;
    InfoRow(c, y, "Game area", status.capturing ? Format("%d × %d", status.gameArea.cx, status.gameArea.cy) : "–", type::kMono);
    y += kRow;
    InfoRow(c, y, "Frame rate", live ? Format("%d fps  %.1f ms", Round(stats.Fps()), stats.FrameMs()) : "–", type::kMono);
    y += kRow;

    c.Line(kPad, y + 6, kRight, y + 6, pal::kRule);
    y += 12;
    CheckRow(c, "capture.cursor", y, "Draw the mouse cursor", settings.captureCursor);
    y += kRow;
    CheckRow(c, "capture.close", y, "Close with Roblox", settings.closeWithGame);
}

void KeyRow(const Canvas& c, const char* id, float y, const char* label, int& key)
{
    const bool listening = g_keys.target == &key;
    const Hit hit = RowButton(c, id, y);
    if (hit.clicked)
    {
        if (listening)
            g_keys.target = nullptr;
        else
            ListenFor(key);
    }

    Label(c, y, label);
    const std::string name = g_keys.target == &key ? std::string("…") : KeyName(key);
    widgets::KeyCap(c, kRight - widgets::KeyCapWidth(c, name), Middle(y, widgets::kKeyHeight) - 1, name, g_keys.target == &key);
}

void DrawKeysTab(const Canvas& c, Settings& settings)
{
    float y = kBodyTop;
    KeyRow(c, "keys.menu", y, "Open this panel", settings.menuKey);
    y += kRow;
    KeyRow(c, "keys.pause", y, "Pause the overlay", settings.pauseKey);
    y += kRow;

    Label(c, y, "ReShade menu");
    widgets::KeyCap(c, kRight - widgets::KeyCapWidth(c, "Home"), Middle(y, widgets::kKeyHeight) - 1, "Home", false, true);
    y += kRow + 10;

    if (g_keys.target)
    {
        c.Text(kPad, y, type::kSmall, pal::kSoft, "Press the new key. Esc cancels.");
    }
    else
    {
        c.Text(kPad, y, type::kSmall, pal::kFaint, "Click a row, then press the key you want.");
        c.Text(kPad, y + 19, type::kSmall, pal::kFaint, "Home reaches ReShade while this panel is open.");
    }
}

Action DrawFooter(const Canvas& c, const Status& status)
{
    const float y = kHeight - kFooter;
    c.Line(0, y, kWidth, y, pal::kRule);
    c.Dot(kPad + 3, y + kFooter / 2, 3, status.capturing ? pal::kGood : pal::kWaiting);
    c.TextMiddle(kPad + 13, y, kFooter, type::kSmall, pal::kMuted, status.capturing ? "Capturing Roblox" : "Waiting for Roblox");
    return widgets::TextButton(c, "panel.quit", kRight, y, kFooter, "Quit DxLayer") ? Action::Quit : Action::None;
}

// Brings the panel back inside the game's area if the game window got smaller than where it was left.
void KeepOnScreen()
{
    const ImGuiViewport* view = ImGui::GetMainViewport();
    const ImVec2 pos = ImGui::GetWindowPos();
    const ImVec2 size = ImGui::GetWindowSize();
    const ImVec2 kept(std::clamp(pos.x, view->Pos.x, std::max(view->Pos.x, view->Pos.x + view->Size.x - size.x)),
                      std::clamp(pos.y, view->Pos.y, std::max(view->Pos.y, view->Pos.y + view->Size.y - size.y)));
    if (kept.x != pos.x || kept.y != pos.y)
        ImGui::SetWindowPos(kept);
}

Action DrawPanel(Settings& settings, const Status& status)
{
    constexpr ImGuiWindowFlags kFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoMove |
                                        ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoNav;
    const ImGuiViewport* view = ImGui::GetMainViewport();
    ImGui::SetNextWindowSize(ImVec2(kWidth * g_scale, kHeight * g_scale), ImGuiCond_Always);
    ImGui::SetNextWindowPos(ImVec2(view->Pos.x + 24 * g_scale, view->Pos.y + 24 * g_scale), ImGuiCond_FirstUseEver);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::Begin("DxLayer", nullptr, kFlags);
    ImGui::PopStyleVar(2);
    KeepOnScreen();

    const ImVec2 origin = ImGui::GetWindowPos();
    const Canvas shadow(ImGui::GetBackgroundDrawList(), origin, g_scale);
    shadow.Fill(-6, -2, kWidth + 12, kHeight + 14, draw::Hex(0x000000, 28), kRounding + 6);
    shadow.Fill(-2, 0, kWidth + 4, kHeight + 5, draw::Hex(0x000000, 60), kRounding + 2);

    const Canvas c(ImGui::GetWindowDrawList(), origin, g_scale);
    c.Fill(0, 0, kWidth, kHeight, pal::kPanel, kRounding);

    const bool close = DrawHeader(c, status);
    DrawTabs(c);
    switch (g_tab)
    {
    case Tab::Counter: DrawCounterTab(c, settings); break;
    case Tab::Capture: DrawCaptureTab(c, settings, status); break;
    case Tab::Keys: DrawKeysTab(c, settings); break;
    }
    if (g_tab != Tab::Keys)
        g_keys.target = nullptr;

    Action action = DrawFooter(c, status);
    c.Outline(0, 0, kWidth, kHeight, pal::kBorder, kRounding);
    ImGui::End();

    if (close && action == Action::None)
        action = Action::CloseMenu;
    return action;
}

void ApplyStyle()
{
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowPadding = ImVec2(10, 10);
    style.FramePadding = ImVec2(8, 5);
    style.ItemSpacing = ImVec2(8, 8);
    style.PopupRounding = 6;
    style.FrameRounding = 4;
    style.GrabRounding = 4;
    style.PopupBorderSize = 1;

    const auto set = [&style](ImGuiCol slot, ImU32 color) { style.Colors[slot] = ImGui::ColorConvertU32ToFloat4(color); };
    set(ImGuiCol_Text, pal::kText);
    set(ImGuiCol_TextDisabled, pal::kMuted);
    set(ImGuiCol_WindowBg, pal::kPanel);
    set(ImGuiCol_PopupBg, pal::kPanel);
    set(ImGuiCol_Border, pal::kBorder);
    set(ImGuiCol_FrameBg, pal::kWell);
    set(ImGuiCol_FrameBgHovered, pal::kHover);
    set(ImGuiCol_FrameBgActive, pal::kHover);
    set(ImGuiCol_Button, pal::kKey);
    set(ImGuiCol_ButtonHovered, pal::kHover);
    set(ImGuiCol_ButtonActive, pal::kBorder);
}
} // namespace

bool Init(HWND window, ID3D11Device* device, ID3D11DeviceContext* context)
{
    g_window = window;
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    // The panel's position is remembered beside the exe, wherever DxLayer was started from.
    g_iniPath = ExeFolder() + "imgui.ini";
    ImGui::GetIO().IniFilename = g_iniPath.c_str();

    if (!draw::LoadFonts())
        return false;
    ApplyStyle();
    return ImGui_ImplWin32_Init(window) && ImGui_ImplDX11_Init(device, context);
}

void Shutdown()
{
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
}

Action Draw(Settings& settings, const Status& status, bool menuOpen)
{
    g_scale = GetDpiForWindow(g_window) / 96.0f;
    ImGui::GetStyle().FontScaleDpi = g_scale;

    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    PollKeyCapture(settings);
    if (!menuOpen)
        g_keys.target = nullptr;

    if (settings.showCounter)
        DrawCounter(settings, *status.stats, status.capturing && status.stats->Fps() > 0);

    const Action action = menuOpen ? DrawPanel(settings, status) : Action::None;

    ImGui::Render();
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    return action;
}

bool ListeningForKey()
{
    return g_keys.target != nullptr || g_keys.waitForRelease != 0;
}
} // namespace ui
