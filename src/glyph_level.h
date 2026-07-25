#pragma once

#include <cstdint>
#include <vector>

#include "state.h"

namespace game {

inline constexpr int kArenaGlyphPixelScale = 15;

struct GlyphPlacement {
    std::uint8_t character = 0;
    float centerX = 0, centerY = 0;
    float facing = 0;
};

void BuildGlyphLevel();
const std::vector<GlyphPlacement>& GlyphPlacements();
Rect GlyphPixelRect(
    const GlyphPlacement& placement, int row, int column,
    int channel = -1);
bool GlyphPlacementIsNear(const GlyphPlacement& placement);
bool GlyphGeometryOverlaps(const Rect& rectangle);
bool GlyphLevelAllowsPlayer(const Rect& rectangle);
bool HitGlyphGeometry(const Rect& shot);
bool DamageGlyphGeometryInRadius(
    float x, float y, float radius, int damage);

}  // namespace game
