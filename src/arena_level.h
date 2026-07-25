#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "audio_level.h"
#include "state.h"

namespace game {

struct ArenaPortalOpening {
    PortalDirection direction = PortalDirection::North;
    float center = 0;
    float width = 0;
    bool active = false;
};

struct ArenaDecoration {
    std::uint64_t assetId = 0;
    float x = 0, y = 0;
    float scale = 1;
    float rotation = 0;
};

enum class ArenaMotifSource { Asset, Glyph, Audio };
struct ArenaMotifPoint {
    float x = 0, y = 0;
};
struct ArenaMotifPixel {
    std::array<ArenaMotifPoint, 4> corners{};
    ArenaMotifSource source = ArenaMotifSource::Asset;
    std::string id;
    int row = 0, column = 0;
    std::uint8_t character = 0;
    std::size_t audioPixel = 0;
    std::uint32_t color = 0;
    std::uint64_t placement = 0;
    float placementCenterX = 0, placementCenterY = 0;
};

struct ArenaLevel {
    Rect bounds{};
    float wallThickness = kWall;
    std::uint64_t seed = 0;
    std::vector<ArenaPortalOpening> portals;
    std::vector<Rect> walls;
    std::vector<ArenaDecoration> decorations;
    std::vector<AudioPanel> audioWalls;
    std::vector<ArenaMotifPixel> collisionMotifs;
    std::unordered_map<std::int64_t, std::vector<std::size_t>>
        collisionCells;
    bool forceAudioMotif = false;
    bool fullAudioWaveform = false;
    PortalDirection fullAudioDirection = PortalDirection::South;
};

ArenaLevel BuildArenaLevel(
    const Rect& bounds, std::uint64_t seed,
    const std::vector<ArenaPortalOpening>& portals = {},
    std::uint32_t decorationCount = 12,
    float wallThickness = kWall,
    bool forceAudioMotif = false,
    bool fullAudioWaveform = false,
    Sound fullAudioSound = Sound::LaserShoot,
    PortalDirection fullAudioDirection = PortalDirection::South);
bool ArenaGeometryOverlaps(
    const ArenaLevel& arena, const Rect& rectangle);
bool ArenaContains(const ArenaLevel& arena, const Rect& rectangle);
bool ArenaAllowsPlayer(
    const ArenaLevel& arena, const Rect& playerRectangle);
std::vector<ArenaMotifPixel> BuildArenaMotifPixels(
    const ArenaLevel& arena);
void RebuildArenaCollisionCache(ArenaLevel& arena);
bool HitArenaWallMotif(ArenaLevel& arena, const Rect& shot);

}  // namespace game
