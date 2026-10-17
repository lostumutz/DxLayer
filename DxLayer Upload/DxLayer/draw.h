#pragma once

#include "imgui/imgui.h"

#include <string>
#include <string_view>

// Colours, type and a small drawing surface for the panel and the counter. Positions are given in layout units (pixels at 100%
// display scaling) from the canvas origin, and the canvas scales them for the screen.
namespace draw
{
constexpr ImU32 Hex(unsigned rgb, unsigned alpha = 255)
{
    return IM_COL32((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF, alpha);
}

ImU32 Mix(ImU32 from, ImU32 to, float t);
ImU32 Fade(ImU32 color, float alpha);

namespace palette
{
constexpr ImU32 kPanel = Hex(0x0E0E10);
constexpr ImU32 kHover = Hex(0x18181B);
constexpr ImU32 kWell = Hex(0x141417);
constexpr ImU32 kKey = Hex(0x1C1C20);
constexpr ImU32 kBorder = Hex(0x2C2C31);
constexpr ImU32 kBorderHover = Hex(0x45454C);
constexpr ImU32 kRule = Hex(0x1E1E22);

constexpr ImU32 kText = Hex(0xEDEDEF);
constexpr ImU32 kSoft = Hex(0xC2C2C7);
constexpr ImU32 kMuted = Hex(0x8A8A91);
constexpr ImU32 kFaint = Hex(0x5A5A61);

constexpr ImU32 kAccent = Hex(0xF2F2F3);
constexpr ImU32 kInk = Hex(0x0E0E10);
constexpr ImU32 kGood = Hex(0x5FD38A);
constexpr ImU32 kWaiting = Hex(0xE0B25A);
} // namespace palette

enum class Face { Text, Strong, Mono };

struct Type
{
    Face face;
    float size;
};

namespace type
{
constexpr Type kTitle{ Face::Strong, 15 };
constexpr Type kTab{ Face::Strong, 13 };
constexpr Type kLabel{ Face::Text, 14 };
constexpr Type kSmall{ Face::Text, 12.5f };
constexpr Type kMono{ Face::Mono, 13 };
constexpr Type kMonoSmall{ Face::Mono, 12 };
} // namespace type

// Segoe UI and Cascadia Mono from the Windows fonts folder, with fallbacks. Call after ImGui::CreateContext.
bool LoadFonts();
ImFont* Font(Face face);

std::string Format(const char* format, ...);

struct Hit
{
    bool hovered = false;
    bool held = false;
    bool clicked = false;
};

class Canvas
{
public:
    Canvas(ImDrawList* list, ImVec2 origin, float scale) : list_(list), origin_(origin), scale_(scale) {}

    ImVec2 At(float x, float y) const { return ImVec2(origin_.x + x * scale_, origin_.y + y * scale_); }
    float Px(float units) const { return units * scale_; }
    float ToUnitsX(float screenX) const { return (screenX - origin_.x) / scale_; }
    ImDrawList* List() const { return list_; }

    void Fill(float x, float y, float w, float h, ImU32 color, float rounding = 0, ImDrawFlags corners = 0) const;
    void Outline(float x, float y, float w, float h, ImU32 color, float rounding = 0, float thickness = 1) const;
    void Line(float x0, float y0, float x1, float y1, ImU32 color, float thickness = 1) const;
    void Dot(float cx, float cy, float radius, ImU32 color) const;

    void Text(float x, float y, Type type, ImU32 color, std::string_view text) const;
    // Left aligned and centred in a band `height` tall.
    void TextMiddle(float x, float y, float height, Type type, ImU32 color, std::string_view text) const;
    // Right aligned to `right` and centred in a band `height` tall.
    void TextRight(float right, float y, float height, Type type, ImU32 color, std::string_view text) const;
    void TextCentered(float x, float y, float w, float h, Type type, ImU32 color, std::string_view text) const;
    float Measure(Type type, std::string_view text) const;
    // Distance from the top of a line of text to its baseline, for lining up text of different sizes.
    float Ascent(Type type) const;

    // An invisible button over the area; the drawing is up to the caller.
    Hit Area(const char* id, float x, float y, float w, float h, bool pointer = true) const;

private:
    ImDrawList* list_;
    ImVec2 origin_;
    float scale_;
};

// Eases a stored value toward `target` a little each frame, so state changes slide instead of jumping.
float Approach(ImGuiID id, float target, float speed = 18.0f);
} // namespace draw
