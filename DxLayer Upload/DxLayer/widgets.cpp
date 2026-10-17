#include "widgets.h"

#include <algorithm>
#include <cmath>

using draw::Canvas;
using draw::Hit;
namespace pal = draw::palette;
namespace type = draw::type;

namespace widgets
{
namespace
{
// An animation value belonging to the control `id`.
float Animated(const char* id, const char* name, float target)
{
    ImGui::PushID(id);
    const float value = draw::Approach(ImGui::GetID(name), target);
    ImGui::PopID();
    return value;
}

void SelectionFrame(const Canvas& c, float x, float y, float size, bool selected, bool hovered)
{
    if (selected || hovered)
        c.Outline(x - 3, y - 3, size + 6, size + 6, selected ? pal::kAccent : pal::kBorderHover, 5, 1.5f);
}
} // namespace

void Checkbox(const Canvas& c, const char* id, float x, float y, bool on, bool hovered)
{
    const float t = Animated(id, "on", on ? 1.0f : 0.0f);
    c.Fill(x, y, kCheckSize, kCheckSize, draw::Mix(pal::kWell, pal::kAccent, t), 4);
    c.Outline(x, y, kCheckSize, kCheckSize, draw::Mix(hovered ? pal::kBorderHover : pal::kBorder, pal::kAccent, t), 4);
    if (t <= 0.01f)
        return;

    const ImVec2 tick[] = { c.At(x + 4, y + 8.5f), c.At(x + 7, y + 11.5f), c.At(x + 12.5f, y + 5) };
    c.List()->AddPolyline(tick, 3, draw::Fade(pal::kInk, t), ImDrawFlags_None, c.Px(1.8f));
}

float KeyCapWidth(const Canvas& c, std::string_view label)
{
    return std::max(34.0f, c.Measure(type::kMonoSmall, label) + 18);
}

void KeyCap(const Canvas& c, float x, float y, std::string_view label, bool listening, bool dim)
{
    const float w = KeyCapWidth(c, label);
    const float h = kKeyHeight;
    c.Fill(x, y + 2, w, h, draw::Hex(0x060607), 4);
    c.Fill(x, y, w, h, listening ? pal::kHover : pal::kKey, 4);
    if (listening)
    {
        const float pulse = 0.5f + 0.5f * std::sin(static_cast<float>(ImGui::GetTime()) * 6.0f);
        c.Outline(x, y, w, h, draw::Mix(pal::kBorder, pal::kAccent, pulse), 4);
    }
    else
    {
        c.Outline(x, y, w, h, pal::kBorder, 4);
    }
    c.TextCentered(x, y, w, h, type::kMonoSmall, dim ? pal::kMuted : pal::kText, label);
}

bool Choice(const Canvas& c, const char* id, float x, float y, float w, const char* const* labels, int count, int& index)
{
    const float h = kChoiceHeight;
    const float cell = w / count;
    c.Fill(x, y, w, h, pal::kWell, 5);
    c.Outline(x, y, w, h, pal::kBorder, 5);

    const float at = Animated(id, "at", static_cast<float>(index));
    c.Fill(x + 2 + at * cell, y + 2, cell - 4, h - 4, pal::kAccent, 4);

    bool changed = false;
    ImGui::PushID(id);
    for (int i = 0; i < count; ++i)
    {
        ImGui::PushID(i);
        const Hit hit = c.Area("cell", x + i * cell, y, cell, h);
        ImGui::PopID();
        if (hit.clicked && index != i)
        {
            index = i;
            changed = true;
        }
        // The label sits on the white highlight once it has slid most of the way over.
        const float covered = 1.0f - std::min(1.0f, std::fabs(at - i));
        const ImU32 idle = hit.hovered ? pal::kText : pal::kMuted;
        c.TextCentered(x + i * cell, y, cell, h, type::kTab, draw::Mix(idle, pal::kInk, covered), labels[i]);
    }
    ImGui::PopID();
    return changed;
}

bool Slider(const Canvas& c, const char* id, float x, float y, float w, float& value)
{
    constexpr float kKnob = 6;
    const float h = kChoiceHeight;
    const float left = x + kKnob;
    const float right = x + w - kKnob;

    const Hit hit = c.Area(id, x, y, w, h);
    bool changed = false;
    if (hit.held)
    {
        const float mouse = c.ToUnitsX(ImGui::GetIO().MousePos.x);
        const float picked = std::round(std::clamp((mouse - left) / (right - left), 0.0f, 1.0f) * 100.0f) / 100.0f;
        changed = picked != value;
        value = picked;
    }

    const float mid = y + h / 2;
    const float knob = left + (right - left) * value;
    c.Fill(left, mid - 1, right - left, 2, pal::kBorder, 1);
    c.Fill(left, mid - 1, knob - left, 2, pal::kAccent, 1);
    c.Dot(knob, mid, hit.hovered || hit.held ? kKnob + 1 : kKnob, pal::kAccent);
    return changed;
}

bool CornerGrid(const Canvas& c, const char* id, float x, float y, int& corner)
{
    constexpr float kGap = 4;
    constexpr float kCellWidth = (kCornerWidth - kGap) / 2;
    constexpr float kCellHeight = (kCornerHeight - kGap) / 2;

    bool changed = false;
    ImGui::PushID(id);
    for (int i = 0; i < 4; ++i)
    {
        const float cx = x + (i % 2) * (kCellWidth + kGap);
        const float cy = y + (i / 2) * (kCellHeight + kGap);
        ImGui::PushID(i);
        const Hit hit = c.Area("corner", cx - 1, cy - 1, kCellWidth + 2, kCellHeight + 2);
        ImGui::PopID();
        if (hit.clicked && corner != i)
        {
            corner = i;
            changed = true;
        }

        if (i == corner)
        {
            c.Fill(cx, cy, kCellWidth, kCellHeight, pal::kAccent, 2.5f);
        }
        else
        {
            c.Fill(cx, cy, kCellWidth, kCellHeight, pal::kWell, 2.5f);
            c.Outline(cx, cy, kCellWidth, kCellHeight, hit.hovered ? pal::kBorderHover : pal::kBorder, 2.5f);
        }
    }
    ImGui::PopID();
    return changed;
}

bool Swatch(const Canvas& c, const char* id, float x, float y, ImU32 color, bool selected)
{
    const Hit hit = c.Area(id, x - 3, y - 3, kSwatchSize + 6, kSwatchSize + 6);
    SelectionFrame(c, x, y, kSwatchSize, selected, hit.hovered);
    c.Fill(x, y, kSwatchSize, kSwatchSize, color, 3);
    return hit.clicked;
}

bool CustomSwatch(const Canvas& c, const char* id, float x, float y, bool selected)
{
    // Four hues in one square: the way to a colour of your own.
    const Hit hit = c.Area(id, x - 3, y - 3, kSwatchSize + 6, kSwatchSize + 6);
    SelectionFrame(c, x, y, kSwatchSize, selected, hit.hovered);
    const float half = kSwatchSize / 2;
    c.Fill(x, y, half, half, draw::Hex(0xFF6B6B), 3, ImDrawFlags_RoundCornersTopLeft);
    c.Fill(x + half, y, half, half, draw::Hex(0xFFC15E), 3, ImDrawFlags_RoundCornersTopRight);
    c.Fill(x, y + half, half, half, draw::Hex(0x8F8CFF), 3, ImDrawFlags_RoundCornersBottomLeft);
    c.Fill(x + half, y + half, half, half, draw::Hex(0x6FD6FF), 3, ImDrawFlags_RoundCornersBottomRight);
    return hit.clicked;
}

bool CloseButton(const Canvas& c, const char* id, float x, float y)
{
    constexpr float kSize = 22, kInset = 7;
    const Hit hit = c.Area(id, x, y, kSize, kSize);
    if (hit.hovered)
        c.Fill(x, y, kSize, kSize, pal::kHover, 4);

    const ImU32 ink = hit.hovered ? pal::kText : pal::kMuted;
    ImDrawList* list = c.List();
    list->AddLine(c.At(x + kInset, y + kInset), c.At(x + kSize - kInset, y + kSize - kInset), ink, c.Px(1.4f));
    list->AddLine(c.At(x + kSize - kInset, y + kInset), c.At(x + kInset, y + kSize - kInset), ink, c.Px(1.4f));
    return hit.clicked;
}

bool TextButton(const Canvas& c, const char* id, float right, float y, float height, std::string_view text)
{
    constexpr float kPad = 8;
    const float w = c.Measure(type::kSmall, text);
    const Hit hit = c.Area(id, right - w - kPad, y + 4, w + kPad * 2, height - 8);
    if (hit.hovered)
        c.Fill(right - w - kPad, y + 6, w + kPad * 2, height - 12, pal::kHover, 4);
    c.TextRight(right, y, height, type::kSmall, hit.hovered ? pal::kText : pal::kMuted, text);
    return hit.clicked;
}
} // namespace widgets
