#include "glyph_level.h"

#include <algorithm>
#include <array>
#include <cmath>

#include "text_renderer.h"

namespace game {
namespace {

std::vector<GlyphPlacement> placements;

void AddEdge(
    int& character, int count, float startX, float startY,
    float stepX, float stepY, float facing) {
    for (int index = 0; index < count && character <= 126; ++index) {
        placements.push_back(
            {static_cast<std::uint8_t>(character),
             startX + stepX * (index + 0.5f),
             startY + stepY * (index + 0.5f), facing});
        ++character;
    }
}

void WorldToLocal(
    const GlyphPlacement& placement, float x, float y,
    float& localX, float& localY) {
    const float dx = x - placement.centerX;
    const float dy = y - placement.centerY;
    const float cosine = std::cos(placement.facing);
    const float sine = std::sin(placement.facing);
    localX = cosine * dx + sine * dy +
             5.0f * kArenaGlyphPixelScale * 0.5f;
    localY = -sine * dx + cosine * dy +
             7.0f * kArenaGlyphPixelScale * 0.5f;
}

bool DamagePixel(
    std::uint8_t character, int row, int column,
    int channel, int damage) {
    text_renderer::GlyphPixel& pixel =
        text_renderer::MutableGlyph(character)[row][column];
    if (!pixel.occupied) return false;
    const int previous = pixel.rgb[channel];
    pixel.rgb[channel] =
        std::max(0, pixel.rgb[channel] - damage);
    return pixel.rgb[channel] != previous;
}

}  // namespace

void BuildGlyphLevel() {
    placements.clear();
    const LevelRegion* region = CurrentLevelRegion();
    if (!region || region->map != "glyph") return;
    constexpr float inset = 24.0f;
    const float glyphHeight = 7.0f * kArenaGlyphPixelScale;
    const float cornerClearance = inset + glyphHeight;
    const float horizontalLength =
        region->width - cornerClearance * 2.0f;
    const float verticalLength =
        region->height - cornerClearance * 2.0f;
    int character = 33;
    AddEdge(
        character, 23, region->x + cornerClearance,
        region->y + inset + glyphHeight * 0.5f,
        horizontalLength / 23.0f, 0, kPi);
    AddEdge(
        character, 24,
        region->x + region->width - inset - glyphHeight * 0.5f,
        region->y + cornerClearance,
        0, verticalLength / 24.0f, -kPi * 0.5f);
    AddEdge(
        character, 23, region->x + region->width - cornerClearance,
        region->y + region->height - inset - glyphHeight * 0.5f,
        -horizontalLength / 23.0f, 0, 0);
    AddEdge(
        character, 24, region->x + inset + glyphHeight * 0.5f,
        region->y + region->height - cornerClearance,
        0, -verticalLength / 24.0f,
        kPi * 0.5f);
}

const std::vector<GlyphPlacement>& GlyphPlacements() {
    return placements;
}

Rect GlyphPixelRect(
    const GlyphPlacement& placement, int row, int column,
    int channel) {
    const float scale = static_cast<float>(kArenaGlyphPixelScale);
    float localLeft = column * scale - 5.0f * scale * 0.5f;
    float localTop = row * scale - 7.0f * scale * 0.5f;
    float localWidth = scale;
    if (channel >= 0) {
        localLeft += channel * scale / 3.0f;
        localWidth = scale / 3.0f;
    }
    const std::array<std::pair<float, float>, 4> corners{{
        {localLeft, localTop},
        {localLeft + localWidth, localTop},
        {localLeft, localTop + scale},
        {localLeft + localWidth, localTop + scale},
    }};
    const float cosine = std::cos(placement.facing);
    const float sine = std::sin(placement.facing);
    float left = 1.0e30f, top = 1.0e30f;
    float right = -1.0e30f, bottom = -1.0e30f;
    for (const auto& [x, y] : corners) {
        const float worldX =
            placement.centerX + cosine * x - sine * y;
        const float worldY =
            placement.centerY + sine * x + cosine * y;
        left = std::min(left, worldX);
        top = std::min(top, worldY);
        right = std::max(right, worldX);
        bottom = std::max(bottom, worldY);
    }
    return {left, top, right - left, bottom - top};
}

bool GlyphPlacementIsNear(const GlyphPlacement& placement) {
    const float dx =
        placement.centerX - (playerX + kPlayerSize * 0.5f);
    const float dy =
        placement.centerY - (playerY + kPlayerSize * 0.5f);
    const float distance = std::max(buffer.width, buffer.height) * 0.25f;
    return dx * dx + dy * dy <= distance * distance;
}

bool GlyphGeometryOverlaps(const Rect& rectangle) {
    for (const GlyphPlacement& placement : placements) {
        const text_renderer::Glyph& glyph =
            text_renderer::GetGlyph(placement.character);
        for (int row = 0; row < 7; ++row)
            for (int column = 0; column < 5; ++column)
                if (glyph[row][column].occupied &&
                    Overlaps(
                        GlyphPixelRect(placement, row, column),
                        rectangle))
                    return true;
    }
    return false;
}

bool GlyphLevelAllowsPlayer(const Rect& rectangle) {
    const LevelRegion* region = CurrentLevelRegion();
    if (!region || region->map != "glyph") return false;
    constexpr float border = 12.0f;
    return rectangle.x >= region->x + border &&
           rectangle.y >= region->y + border &&
           rectangle.x + rectangle.width <=
               region->x + region->width - border &&
           rectangle.y + rectangle.height <=
               region->y + region->height - border &&
           !GlyphGeometryOverlaps(rectangle);
}

bool HitGlyphGeometry(const Rect& shot) {
    const float hitX = CenterX(shot);
    const float hitY = CenterY(shot);
    for (const GlyphPlacement& placement : placements) {
        float localX = 0, localY = 0;
        WorldToLocal(placement, hitX, hitY, localX, localY);
        if (localX < 0 || localY < 0 ||
            localX >= 5 * kArenaGlyphPixelScale ||
            localY >= 7 * kArenaGlyphPixelScale)
            continue;
        const int column =
            static_cast<int>(localX / kArenaGlyphPixelScale);
        const int row =
            static_cast<int>(localY / kArenaGlyphPixelScale);
        const text_renderer::GlyphPixel& pixel =
            text_renderer::GetGlyph(placement.character)[row][column];
        if (!pixel.occupied) continue;
        const float pixelX =
            localX - column * kArenaGlyphPixelScale;
        const int channel = std::clamp(
            static_cast<int>(
                pixelX * 3.0f / kArenaGlyphPixelScale),
            0, 2);
        return DamagePixel(
            placement.character, row, column, channel,
            kWallChannelDamage);
    }
    return false;
}

bool DamageGlyphGeometryInRadius(
    float x, float y, float radius, int damage) {
    const float radiusSquared = radius * radius;
    bool changed = false;
    for (const GlyphPlacement& placement : placements) {
        float localX = 0, localY = 0;
        WorldToLocal(placement, x, y, localX, localY);
        const text_renderer::Glyph& glyph =
            text_renderer::GetGlyph(placement.character);
        for (int row = 0; row < 7; ++row)
            for (int column = 0; column < 5; ++column) {
                if (!glyph[row][column].occupied) continue;
                const Rect rect =
                    GlyphPixelRect(placement, row, column);
                const float dx =
                    x - std::clamp(x, rect.x, rect.x + rect.width);
                const float dy =
                    y - std::clamp(y, rect.y, rect.y + rect.height);
                if (dx * dx + dy * dy > radiusSquared) continue;
                const float pixelX = std::clamp(
                    localX - column * kArenaGlyphPixelScale,
                    0.0f,
                    static_cast<float>(kArenaGlyphPixelScale) - 0.01f);
                const int channel = std::clamp(
                    static_cast<int>(
                        pixelX * 3.0f / kArenaGlyphPixelScale),
                    0, 2);
                changed = DamagePixel(
                    placement.character, row, column,
                    channel, damage) || changed;
            }
    }
    return changed;
}

}  // namespace game
