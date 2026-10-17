#pragma once

#include "draw.h"

#include <string_view>

// The panel's controls. Positions are top left corners in layout units; the ones that edit a value return true when it changed,
// and the buttons return true when clicked.
namespace widgets
{
constexpr float kCheckSize = 16;
constexpr float kChoiceHeight = 26;
constexpr float kSwatchSize = 16;
constexpr float kSwatchPitch = 23;
constexpr float kCornerWidth = 34;
constexpr float kCornerHeight = 24;
constexpr float kKeyHeight = 24;

// Drawn only: the row it sits in takes the click, so the label is clickable too.
void Checkbox(const draw::Canvas& c, const char* id, float x, float y, bool on, bool hovered);

float KeyCapWidth(const draw::Canvas& c, std::string_view label);
// A key drawn as a key. `listening` makes it pulse while it waits for a new one.
void KeyCap(const draw::Canvas& c, float x, float y, std::string_view label, bool listening = false, bool dim = false);

bool Choice(const draw::Canvas& c, const char* id, float x, float y, float w, const char* const* labels, int count, int& index);
// `value` runs from 0 to 1.
bool Slider(const draw::Canvas& c, const char* id, float x, float y, float w, float& value);
// Corners numbered top left, top right, bottom left, bottom right.
bool CornerGrid(const draw::Canvas& c, const char* id, float x, float y, int& corner);

bool Swatch(const draw::Canvas& c, const char* id, float x, float y, ImU32 color, bool selected);
bool CustomSwatch(const draw::Canvas& c, const char* id, float x, float y, bool selected);
bool CloseButton(const draw::Canvas& c, const char* id, float x, float y);
// Text that acts as a button, right aligned to `right` and centred in a band `height` tall.
bool TextButton(const draw::Canvas& c, const char* id, float right, float y, float height, std::string_view text);
} // namespace widgets
