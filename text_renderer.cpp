#include "text_renderer.h"

#include <algorithm>

namespace text_renderer {
namespace {

GlyphAtlas glyphAtlas = FallbackGlyphAtlas();

void DrawRectangle(
    Surface surface, int x, int y, int width, int height,
    std::uint32_t color) {
    const int left = std::max(0, x);
    const int top = std::max(0, y);
    const int right = std::min(surface.width, x + width);
    const int bottom = std::min(surface.height, y + height);
    for (int pixelY = top; pixelY < bottom; ++pixelY) {
        std::uint32_t* row = surface.pixels + pixelY * surface.width;
        for (int pixelX = left; pixelX < right; ++pixelX)
            row[pixelX] = color;
    }
}

std::uint32_t TintedColor(
    const GlyphPixel& pixel, std::uint32_t tint) {
    const int red =
        pixel.rgb[0] * static_cast<int>((tint >> 16) & 0xFF) / 255;
    const int green =
        pixel.rgb[1] * static_cast<int>((tint >> 8) & 0xFF) / 255;
    const int blue =
        pixel.rgb[2] * static_cast<int>(tint & 0xFF) / 255;
    return static_cast<std::uint32_t>(
        (red << 16) | (green << 8) | blue);
}

void DrawGlyph(
    Surface surface, std::uint8_t value, int x, int y,
    std::uint32_t color) {
    const Glyph& glyph = glyphAtlas[value];
    for (int row = 0; row < 7; ++row)
        for (int column = 0; column < 5; ++column) {
            const GlyphPixel& pixel = glyph[row][column];
            if (!pixel.occupied) continue;
            DrawRectangle(
                surface, x + column * kGlyphScale,
                y + row * kGlyphScale, kGlyphScale, kGlyphScale,
                TintedColor(pixel, color));
        }
}

}  // namespace

GlyphAtlas FallbackGlyphAtlas() {
    GlyphAtlas result{};
    auto setRows = [&](int value, const std::array<int, 7>& rows) {
        for (int row = 0; row < 7; ++row)
            for (int column = 0; column < 5; ++column)
                if ((rows[row] & (1 << (4 - column))) != 0) {
                    result[value][row][column].occupied = true;
                    result[value][row][column].rgb = {255, 255, 255};
                }
    };
    const std::array<int, 7> missing{
        0x1F, 0x11, 0x15, 0x15, 0x15, 0x11, 0x1F};
    for (int value = 0; value < 256; ++value)
        setRows(value, missing);
    result[' '] = {};
    for (int value = 0; value <= 31; ++value) {
        std::array<int, 7> rows{};
        rows[0] = rows[6] = 0x1F;
        for (int row = 0; row < 5; ++row) {
            const int bit = 4 - row;
            rows[row + 1] =
                0x11 | ((value & (1 << bit)) != 0 ? 0x04 : 0);
        }
        result[value] = {};
        setRows(value, rows);
    }
    return result;
}

void SetGlyphAtlas(const GlyphAtlas& glyphs) {
    glyphAtlas = glyphs;
}

const Glyph& GetGlyph(std::uint8_t value) {
    return glyphAtlas[value];
}

Glyph& MutableGlyph(std::uint8_t value) {
    return glyphAtlas[value];
}

int MeasureWidth(std::size_t characterCount) {
    if (characterCount == 0) return 0;
    return static_cast<int>(characterCount) * kGlyphAdvance -
           kGlyphScale;
}

void RenderText(
    Surface surface, const std::uint8_t* text,
    std::size_t characterCount, int x, int y,
    std::uint32_t color) {
    if (!surface.pixels || !text) return;
    for (std::size_t index = 0; index < characterCount; ++index)
        DrawGlyph(
            surface, text[index],
            x + static_cast<int>(index) * kGlyphAdvance, y, color);
}

}  // namespace text_renderer
