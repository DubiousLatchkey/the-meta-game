#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace text_renderer {

// Large enough for text to act as readable physical geometry inside a room.
constexpr int kGlyphScale = 3;
constexpr int kGlyphWidth = 5 * kGlyphScale;
constexpr int kGlyphHeight = 7 * kGlyphScale;
constexpr int kGlyphAdvance = kGlyphWidth + kGlyphScale;

struct Surface {
    std::uint32_t* pixels;
    int width;
    int height;
};

struct GlyphPixel {
    bool occupied = false;
    std::array<int, 3> rgb{};
};
using Glyph = std::array<std::array<GlyphPixel, 5>, 7>;
using GlyphAtlas = std::array<Glyph, 256>;

GlyphAtlas FallbackGlyphAtlas();
void SetGlyphAtlas(const GlyphAtlas& glyphs);
const Glyph& GetGlyph(std::uint8_t value);
Glyph& MutableGlyph(std::uint8_t value);

int MeasureWidth(std::size_t characterCount);

void RenderText(
    Surface surface,
    const std::uint8_t* text,
    std::size_t characterCount,
    int x,
    int y,
    std::uint32_t color = 0x00FFFFFF);

}  // namespace text_renderer
