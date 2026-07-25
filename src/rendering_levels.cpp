#include <algorithm>
#include <cmath>
#include <cstdint>

#include "audio_level.h"
#include "glyph_level.h"
#include "rendering_internal.h"
#include "text_renderer.h"

namespace game {

void DrawAudioWaveform(const AudioPanel& panel) {
    const Rect view{
        CameraX(), CameraY(),
        static_cast<float>(buffer.width),
        static_cast<float>(buffer.height)};
    if (!Overlaps(panel.rect, view)) return;
    const std::size_t pixelCount =
        (AudioSampleCount(panel.sound) + 1) / 2;
    if (pixelCount == 0) return;
    const float viewBegin = panel.vertical ? CameraY() : CameraX();
    const float viewEnd = viewBegin +
        static_cast<float>(panel.vertical ? buffer.height : buffer.width);
    const float panelBegin = panel.vertical ? panel.rect.y : panel.rect.x;
    const std::size_t first = static_cast<std::size_t>(std::clamp(
        std::floor(viewBegin - panelBegin) - 2.0f,
        0.0f, static_cast<float>(pixelCount - 1)));
    const std::size_t last = static_cast<std::size_t>(std::clamp(
        std::ceil(viewEnd - panelBegin) + 2.0f,
        0.0f, static_cast<float>(pixelCount - 1)));
    if (panel.representation == AudioRepresentation::Grayscale) {
        for (std::size_t pixel = first; pixel <= last; ++pixel) {
            const std::uint32_t value =
                AudioPixelAverage(panel.sound, pixel);
            const std::uint32_t color =
                (value << 16) | (value << 8) | value;
            DrawWorldRect(
                {panel.rect.x + static_cast<float>(pixel),
                 panel.rect.y, 1.0f, panel.rect.height},
                color);
        }
        return;
    }
    for (std::size_t pixel = first; pixel <= last; ++pixel) {
        if (panel.vertical) {
            const float x = panel.rect.x +
                (255.0f - AudioPixelAverage(panel.sound, pixel));
            const float previousX = pixel > 0
                ? panel.rect.x +
                    (255.0f - AudioPixelAverage(panel.sound, pixel - 1))
                : x;
            DrawWorldRect(
                {std::min(x, previousX) - 1.0f,
                 panel.rect.y + static_cast<float>(pixel) - 1.0f,
                 std::abs(x - previousX) + 3.0f, 2.0f},
                0x0000E8FF);
            continue;
        }
        const float y = AudioWaveY(panel, pixel);
        const float previousY = pixel > 0
            ? AudioWaveY(panel, pixel - 1) : y;
        DrawWorldRect(
            {panel.rect.x + static_cast<float>(pixel) - 1.0f,
             std::min(y, previousY) - 1.0f,
             2.0f, std::abs(y - previousY) + 3.0f},
            0x0000E8FF);
    }
}

void DrawAudioPanels() {
    for (const AudioPanel& panel : AudioPanels())
        DrawAudioWaveform(panel);
}

void DrawGlyphArena() {
    const LevelRegion* region = CurrentLevelRegion();
    if (!region) return;
    DrawWorldRect(
        {region->x, region->y, region->width, region->height},
        0x00070B11);
    constexpr float border = 12.0f;
    DrawWorldRect(
        {region->x, region->y, region->width, border},
        0x00304858);
    DrawWorldRect(
        {region->x, region->y + region->height - border,
         region->width, border},
        0x00304858);
    DrawWorldRect(
        {region->x, region->y, border, region->height},
        0x00304858);
    DrawWorldRect(
        {region->x + region->width - border, region->y,
         border, region->height},
        0x00304858);
    for (const GlyphPlacement& placement : GlyphPlacements()) {
        const text_renderer::Glyph& glyph =
            text_renderer::GetGlyph(placement.character);
        const bool isNear = GlyphPlacementIsNear(placement);
        for (int row = 0; row < 7; ++row)
            for (int column = 0; column < 5; ++column) {
                const text_renderer::GlyphPixel& pixel =
                    glyph[row][column];
                if (!pixel.occupied) continue;
                if (!isNear) {
                    DrawWorldRect(
                        GlyphPixelRect(placement, row, column),
                        static_cast<std::uint32_t>(
                            (pixel.rgb[0] << 16) |
                            (pixel.rgb[1] << 8) | pixel.rgb[2]));
                    continue;
                }
                for (int channel = 0; channel < 3; ++channel)
                    DrawWorldRect(
                        GlyphPixelRect(
                            placement, row, column, channel),
                        ChannelColor(channel, pixel.rgb[channel]));
            }
    }
}

}  // namespace game
