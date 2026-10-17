#include "draw.h"

#include <windows.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdarg>
#include <cstdio>

namespace draw
{
namespace
{
ImFont* g_text = nullptr;
ImFont* g_strong = nullptr;
ImFont* g_mono = nullptr;

// The first of `names` found in the Windows fonts folder.
ImFont* LoadFirst(std::initializer_list<const char*> names)
{
    char windows[MAX_PATH]{};
    GetWindowsDirectoryA(windows, MAX_PATH);
    for (const char* name : names)
    {
        const std::string path = std::string(windows) + "\\Fonts\\" + name;
        if (GetFileAttributesA(path.c_str()) != INVALID_FILE_ATTRIBUTES)
            return ImGui::GetIO().Fonts->AddFontFromFileTTF(path.c_str(), 14.0f);
    }
    return nullptr;
}

ImVec2 Snap(ImVec2 p)
{
    return ImVec2(std::floor(p.x + 0.5f), std::floor(p.y + 0.5f));
}
} // namespace

ImU32 Mix(ImU32 from, ImU32 to, float t)
{
    t = std::clamp(t, 0.0f, 1.0f);
    const ImVec4 a = ImGui::ColorConvertU32ToFloat4(from);
    const ImVec4 b = ImGui::ColorConvertU32ToFloat4(to);
    return ImGui::ColorConvertFloat4ToU32(ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t));
}

ImU32 Fade(ImU32 color, float alpha)
{
    const unsigned a = (color >> IM_COL32_A_SHIFT) & 0xFF;
    const unsigned faded = static_cast<unsigned>(a * std::clamp(alpha, 0.0f, 1.0f) + 0.5f);
    return (color & ~IM_COL32_A_MASK) | (faded << IM_COL32_A_SHIFT);
}

bool LoadFonts()
{
    g_text = LoadFirst({ "segoeui.ttf" });
    if (!g_text)
        g_text = ImGui::GetIO().Fonts->AddFontDefault();
    g_strong = LoadFirst({ "seguisb.ttf" });
    if (!g_strong)
        g_strong = g_text;
    g_mono = LoadFirst({ "CascadiaMono.ttf", "consola.ttf" });
    if (!g_mono)
        g_mono = g_text;
    return g_text != nullptr;
}

ImFont* Font(Face face)
{
    switch (face)
    {
    case Face::Strong: return g_strong;
    case Face::Mono: return g_mono;
    default: return g_text;
    }
}

std::string Format(const char* format, ...)
{
    char buffer[256];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    return buffer;
}

void Canvas::Fill(float x, float y, float w, float h, ImU32 color, float rounding, ImDrawFlags corners) const
{
    list_->AddRectFilled(Snap(At(x, y)), Snap(At(x + w, y + h)), color, Px(rounding), corners);
}

void Canvas::Outline(float x, float y, float w, float h, ImU32 color, float rounding, float thickness) const
{
    list_->AddRect(Snap(At(x, y)), Snap(At(x + w, y + h)), color, Px(rounding), 0, Px(thickness));
}

void Canvas::Line(float x0, float y0, float x1, float y1, ImU32 color, float thickness) const
{
    // Half a pixel in, so a one pixel line covers one row of pixels instead of half of two.
    const ImVec2 a = Snap(At(x0, y0)), b = Snap(At(x1, y1));
    list_->AddLine(ImVec2(a.x + 0.5f, a.y + 0.5f), ImVec2(b.x + 0.5f, b.y + 0.5f), color, std::max(1.0f, Px(thickness)));
}

void Canvas::Dot(float cx, float cy, float radius, ImU32 color) const
{
    list_->AddCircleFilled(At(cx, cy), Px(radius), color, 0);
}

void Canvas::Text(float x, float y, Type type, ImU32 color, std::string_view text) const
{
    list_->AddText(Font(type.face), Px(type.size), Snap(At(x, y)), color, text.data(), text.data() + text.size());
}

void Canvas::TextMiddle(float x, float y, float height, Type type, ImU32 color, std::string_view text) const
{
    Text(x, y + (height - type.size) / 2, type, color, text);
}

void Canvas::TextRight(float right, float y, float height, Type type, ImU32 color, std::string_view text) const
{
    TextMiddle(right - Measure(type, text), y, height, type, color, text);
}

void Canvas::TextCentered(float x, float y, float w, float h, Type type, ImU32 color, std::string_view text) const
{
    Text(x + (w - Measure(type, text)) / 2, y + (h - type.size) / 2, type, color, text);
}

float Canvas::Measure(Type type, std::string_view text) const
{
    const ImVec2 size = Font(type.face)->CalcTextSizeA(Px(type.size), FLT_MAX, 0.0f, text.data(), text.data() + text.size());
    return size.x / scale_;
}

float Canvas::Ascent(Type type) const
{
    return Font(type.face)->GetFontBaked(Px(type.size))->Ascent / scale_;
}

Hit Canvas::Area(const char* id, float x, float y, float w, float h, bool pointer) const
{
    ImGui::SetCursorScreenPos(At(x, y));
    Hit hit;
    hit.clicked = ImGui::InvisibleButton(id, ImVec2(std::max(Px(w), 1.0f), std::max(Px(h), 1.0f)));
    hit.hovered = ImGui::IsItemHovered();
    hit.held = ImGui::IsItemActive();
    if (pointer && hit.hovered)
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    return hit;
}

float Approach(ImGuiID id, float target, float speed)
{
    float* value = ImGui::GetStateStorage()->GetFloatRef(id, target);
    *value += (target - *value) * std::min(1.0f, ImGui::GetIO().DeltaTime * speed);
    if (std::fabs(target - *value) < 0.002f)
        *value = target;
    return *value;
}
} // namespace draw
