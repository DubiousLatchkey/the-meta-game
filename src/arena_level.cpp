#include "arena_level.h"

#include <algorithm>
#include <cmath>
#include "audio.h"
#include "roguelite.h"
#include "text_renderer.h"
#include "world.h"

namespace game {
namespace {

struct Interval {
    float begin = 0, end = 0;
};

float UnitFloat(std::uint64_t value) {
    return static_cast<float>(value >> 40) /
        static_cast<float>(1ULL << 24);
}

void AddSide(
    ArenaLevel& arena, PortalDirection direction,
    float sideStart, float sideLength) {
    std::vector<Interval> openings;
    for (const ArenaPortalOpening& portal : arena.portals) {
        if (!portal.active || portal.direction != direction) continue;
        // An active opening is always genuinely traversable by a player.
        const float maximumWidth = std::max(
            0.0f, sideLength - arena.wallThickness * 2.0f);
        const float width = std::min(
            std::max(
                portal.width, static_cast<float>(kPlayerSize + 2)),
            maximumWidth);
        if (width < static_cast<float>(kPlayerSize + 2)) continue;
        const float center = std::clamp(
            portal.center, arena.wallThickness + width * 0.5f,
            sideLength - arena.wallThickness - width * 0.5f);
        openings.push_back({
            std::max(0.0f, center - width * 0.5f),
            std::min(sideLength, center + width * 0.5f)});
    }
    std::sort(
        openings.begin(), openings.end(),
        [](const Interval& a, const Interval& b) {
            return a.begin < b.begin;
        });

    std::vector<Interval> merged;
    for (const Interval& opening : openings) {
        if (opening.end <= opening.begin) continue;
        if (merged.empty() || opening.begin > merged.back().end)
            merged.push_back(opening);
        else
            merged.back().end = std::max(
                merged.back().end, opening.end);
    }

    float cursor = 0;
    auto addWall = [&](float begin, float end) {
        if (end <= begin) return;
        if (direction == PortalDirection::North ||
            direction == PortalDirection::South) {
            arena.walls.push_back({
                sideStart + begin,
                direction == PortalDirection::North
                    ? arena.bounds.y
                    : arena.bounds.y + arena.bounds.height -
                        arena.wallThickness,
                end - begin, arena.wallThickness});
        } else {
            arena.walls.push_back({
                direction == PortalDirection::West
                    ? arena.bounds.x
                    : arena.bounds.x + arena.bounds.width -
                        arena.wallThickness,
                sideStart + begin, arena.wallThickness, end - begin});
        }
    };
    for (const Interval& opening : merged) {
        addWall(cursor, opening.begin);
        cursor = opening.end;
    }
    addWall(cursor, sideLength);
}

}  // namespace

bool MotifOverlaps(
    const ArenaMotifPixel& motif, const Rect& rectangle) {
    auto cross = [](const ArenaMotifPoint& a, const ArenaMotifPoint& b,
                    float x, float y) {
        return (b.x - a.x) * (y - a.y) - (b.y - a.y) * (x - a.x);
    };
    auto contains = [&](const ArenaMotifPixel& pixel, float x, float y) {
        bool positive = false, negative = false;
        for (std::size_t index = 0; index < pixel.corners.size(); ++index) {
            const float value = cross(pixel.corners[index],
                pixel.corners[(index + 1) % pixel.corners.size()], x, y);
            positive = positive || value > 0;
            negative = negative || value < 0;
        }
        return !(positive && negative);
    };
    const std::array<ArenaMotifPoint, 4> rectangleCorners{{
        {rectangle.x, rectangle.y},
        {rectangle.x + rectangle.width, rectangle.y},
        {rectangle.x + rectangle.width, rectangle.y + rectangle.height},
        {rectangle.x, rectangle.y + rectangle.height}}};
    for (const ArenaMotifPoint& corner : rectangleCorners)
        if (contains(motif, corner.x, corner.y)) return true;
    for (const ArenaMotifPoint& corner : motif.corners)
        if (corner.x >= rectangle.x &&
            corner.x <= rectangle.x + rectangle.width &&
            corner.y >= rectangle.y &&
            corner.y <= rectangle.y + rectangle.height)
            return true;
    auto orientation = [](const ArenaMotifPoint& a,
                          const ArenaMotifPoint& b,
                          const ArenaMotifPoint& c) {
        const float value =
            (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
        return (value > 0.0f) - (value < 0.0f);
    };
    for (std::size_t a = 0; a < motif.corners.size(); ++a)
        for (std::size_t b = 0; b < rectangleCorners.size(); ++b) {
            const ArenaMotifPoint& first = motif.corners[a];
            const ArenaMotifPoint& second =
                motif.corners[(a + 1) % motif.corners.size()];
            const ArenaMotifPoint& third = rectangleCorners[b];
            const ArenaMotifPoint& fourth =
                rectangleCorners[(b + 1) % rectangleCorners.size()];
            if (orientation(first, second, third) !=
                    orientation(first, second, fourth) &&
                orientation(third, fourth, first) !=
                    orientation(third, fourth, second))
                return true;
        }
    return false;
}

constexpr float kCollisionCellSize = 32.0f;

std::int64_t CollisionCellKey(int x, int y) {
    return static_cast<std::int64_t>(
        (static_cast<std::uint64_t>(static_cast<std::uint32_t>(x)) << 32) |
        static_cast<std::uint32_t>(y));
}

std::vector<std::size_t> CollisionCandidates(
    const ArenaLevel& arena, const Rect& rectangle) {
    const int minX = static_cast<int>(
        std::floor(rectangle.x / kCollisionCellSize));
    const int minY = static_cast<int>(
        std::floor(rectangle.y / kCollisionCellSize));
    const int maxX = static_cast<int>(
        std::floor((rectangle.x + rectangle.width) / kCollisionCellSize));
    const int maxY = static_cast<int>(
        std::floor((rectangle.y + rectangle.height) / kCollisionCellSize));
    std::vector<std::size_t> candidates;
    for (int y = minY; y <= maxY; ++y)
        for (int x = minX; x <= maxX; ++x) {
            const auto found =
                arena.collisionCells.find(CollisionCellKey(x, y));
            if (found == arena.collisionCells.end()) continue;
            candidates.insert(
                candidates.end(), found->second.begin(), found->second.end());
        }
    std::sort(candidates.begin(), candidates.end());
    candidates.erase(
        std::unique(candidates.begin(), candidates.end()), candidates.end());
    return candidates;
}

ArenaLevel BuildArenaLevel(
    const Rect& bounds, std::uint64_t seed,
    const std::vector<ArenaPortalOpening>& portals,
    std::uint32_t decorationCount, float wallThickness,
    bool forceAudioMotif, bool fullAudioWaveform, Sound fullAudioSound,
    PortalDirection fullAudioDirection) {
    ArenaLevel arena;
    arena.bounds = bounds;
    arena.seed = seed;
    arena.portals = portals;
    arena.wallThickness = std::max(1.0f, wallThickness);
    arena.forceAudioMotif = forceAudioMotif;
    arena.fullAudioWaveform = fullAudioWaveform;
    arena.fullAudioDirection = fullAudioDirection;

    AddSide(
        arena, PortalDirection::North, bounds.x, bounds.width);
    AddSide(
        arena, PortalDirection::South, bounds.x, bounds.width);
    AddSide(
        arena, PortalDirection::West, bounds.y, bounds.height);
    AddSide(
        arena, PortalDirection::East, bounds.y, bounds.height);
    if (arena.fullAudioWaveform) {
        const float width = static_cast<float>(
            (AudioSampleCount(fullAudioSound) + 1) / 2);
        AudioPanel waveform;
        waveform.sound = fullAudioSound;
        waveform.representation = AudioRepresentation::Waveform;
        switch (arena.fullAudioDirection) {
            case PortalDirection::North:
                waveform.rect = {bounds.x + 120.0f,
                    bounds.y + arena.wallThickness, width, 256.0f};
                break;
            case PortalDirection::South:
                waveform.rect = {bounds.x + 120.0f,
                    bounds.y + bounds.height - arena.wallThickness - 255.0f,
                    width, 256.0f};
                break;
            case PortalDirection::West:
                waveform.vertical = true;
                waveform.rect = {bounds.x + arena.wallThickness,
                    bounds.y + 120.0f, 256.0f, width};
                break;
            case PortalDirection::East:
                waveform.vertical = true;
                waveform.rect = {bounds.x + bounds.width -
                    arena.wallThickness - 255.0f,
                    bounds.y + 120.0f, 256.0f, width};
                break;
        }
        arena.audioWalls.push_back(waveform);
    }

    const float inset = arena.wallThickness +
        static_cast<float>(kPlayerSize);
    const float usableWidth = std::max(0.0f, bounds.width - inset * 2);
    const float usableHeight = std::max(0.0f, bounds.height - inset * 2);
    for (std::uint32_t index = 0; index < decorationCount; ++index) {
        const std::uint64_t placementSeed =
            DeriveRunSeed(seed, 0x4445434f52ULL, index);
        ArenaDecoration decoration;
        decoration.assetId =
            DeriveRunSeed(placementSeed, 0x4153534554ULL);
        decoration.x = bounds.x + inset +
            UnitFloat(DeriveRunSeed(placementSeed, 1)) * usableWidth;
        decoration.y = bounds.y + inset +
            UnitFloat(DeriveRunSeed(placementSeed, 2)) * usableHeight;
        decoration.scale = 0.75f +
            UnitFloat(DeriveRunSeed(placementSeed, 3)) * 0.5f;
        decoration.rotation =
            UnitFloat(DeriveRunSeed(placementSeed, 4)) * 2.0f * kPi;
        arena.decorations.push_back(decoration);
    }
    RebuildArenaCollisionCache(arena);
    return arena;
}

bool ArenaGeometryOverlaps(
    const ArenaLevel& arena, const Rect& rectangle) {
    for (const AudioPanel& wall : arena.audioWalls)
        if (AudioPanelOverlaps(wall, rectangle)) return true;
    for (const std::size_t index : CollisionCandidates(arena, rectangle))
        if (MotifOverlaps(arena.collisionMotifs[index], rectangle))
            return true;
    return false;
}

bool ArenaContains(const ArenaLevel& arena, const Rect& rectangle) {
    return rectangle.x >= arena.bounds.x &&
        rectangle.y >= arena.bounds.y &&
        rectangle.x + rectangle.width <=
            arena.bounds.x + arena.bounds.width &&
        rectangle.y + rectangle.height <=
            arena.bounds.y + arena.bounds.height;
}

bool ArenaAllowsPlayer(
    const ArenaLevel& arena, const Rect& playerRectangle) {
    return !ArenaGeometryOverlaps(arena, playerRectangle);
}

std::vector<ArenaMotifPixel> BuildArenaMotifPixels(
    const ArenaLevel& arena) {
    // Audio competes with every mutable wall asset and the remaining glyph
    // atlas. Give it several tickets so a typical arena visibly includes at
    // least one waveform motif instead of making it a roughly one-percent
    // outcome among non-glyph placements.
    std::vector<std::string> assets{
        "@audio", "@audio", "@audio", "@audio"};
    for (const auto& [id, asset] : wallAssets) {
        (void)asset;
        assets.push_back(id);
    }
    auto stableId = [](const std::string& value) {
        std::uint64_t hash = 1469598103934665603ULL;
        for (unsigned char byte : value) {
            hash ^= byte;
            hash *= 1099511628211ULL;
        }
        return hash;
    };
    std::sort(assets.begin(), assets.end(), [&](const std::string& a,
                                                const std::string& b) {
        return DeriveRunSeed(arena.seed, 0x4153534554ULL,
                   stableId(a)) <
            DeriveRunSeed(arena.seed, 0x4153534554ULL,
                stableId(b));
    });
    std::vector<std::uint8_t> glyphs(94);
    for (std::size_t index = 0; index < glyphs.size(); ++index)
        glyphs[index] = static_cast<std::uint8_t>(33 + index);
    std::sort(glyphs.begin(), glyphs.end(), [&](std::uint8_t a,
                                                std::uint8_t b) {
        return DeriveRunSeed(arena.seed, 0x474c595048ULL, a) <
            DeriveRunSeed(arena.seed, 0x474c595048ULL, b);
    });
    std::vector<ArenaMotifPixel> result;
    std::size_t placement = 0, glyphOrdinal = 0;
    constexpr float baseScale = 15.0f;
    const Sound sounds[]{
        Sound::LaserShoot, Sound::HitEnemy, Sound::HitHurt,
        Sound::Explosion, Sound::AimTick, Sound::RailgunShot,
        Sound::ChargerChargeUp, Sound::ChargerGo};

    auto addPixel = [&](ArenaMotifSource source, const std::string& id,
                        int row, int column, std::uint8_t character,
                        std::size_t audioPixel, std::uint32_t color,
                        float localX, float localY, float scale,
                        float centerX, float centerY, float angle,
                        std::uint64_t placementId) {
        const float cosine = std::cos(angle), sine = std::sin(angle);
        ArenaMotifPixel pixel;
        pixel.source = source;
        pixel.id = id;
        pixel.row = row;
        pixel.column = column;
        pixel.character = character;
        pixel.audioPixel = audioPixel;
        pixel.color = color;
        pixel.placement = placementId;
        pixel.placementCenterX = centerX;
        pixel.placementCenterY = centerY;
        const std::array<ArenaMotifPoint, 4> local{{
            {localX, localY}, {localX + scale, localY},
            {localX + scale, localY + scale},
            {localX, localY + scale}}};
        for (std::size_t corner = 0; corner < local.size(); ++corner) {
            pixel.corners[corner] = {
                centerX + cosine * local[corner].x - sine * local[corner].y,
                centerY + sine * local[corner].x + cosine * local[corner].y};
        }
        result.push_back(pixel);
    };

    for (const Rect& wall : arena.walls) {
        const bool horizontal = wall.width >= wall.height;
        // A full audio waveform is itself the south wall.  Do not overlay
        // the normal glyph/sprite motif pass on that same wall.
        const bool waveformWall = arena.fullAudioWaveform &&
            ((arena.fullAudioDirection == PortalDirection::North &&
                horizontal &&
                std::abs(wall.y - arena.bounds.y) < 0.01f) ||
             (arena.fullAudioDirection == PortalDirection::South &&
                horizontal && std::abs(
                    wall.y - (arena.bounds.y + arena.bounds.height -
                        arena.wallThickness)) < 0.01f) ||
             (arena.fullAudioDirection == PortalDirection::West &&
                !horizontal &&
                std::abs(wall.x - arena.bounds.x) < 0.01f) ||
             (arena.fullAudioDirection == PortalDirection::East &&
                !horizontal && std::abs(
                    wall.x - (arena.bounds.x + arena.bounds.width -
                        arena.wallThickness)) < 0.01f));
        if (waveformWall) continue;
        const float length = horizontal ? wall.width : wall.height;
        float offset = 0;
        while (offset < length - 0.5f) {
            const std::uint64_t seed =
                DeriveRunSeed(arena.seed, 0x504c414345ULL, placement);
            const std::size_t candidateCount =
                (glyphOrdinal < glyphs.size() ? glyphs.size() - glyphOrdinal : 0)
                + assets.size();
            const bool forceAudio =
                arena.forceAudioMotif && placement == 0;
            bool useGlyph = !forceAudio && glyphOrdinal < glyphs.size() &&
                DeriveRunSeed(seed, 0x43484f494345ULL) % candidateCount <
                    glyphs.size() - glyphOrdinal;
            std::string assetId;
            int rows = 7, columns = 5;
            if (!useGlyph) {
                assetId = forceAudio ? "@audio" :
                    assets[DeriveRunSeed(seed, 0x4153534554ULL) %
                        assets.size()];
                if (assetId == "@audio") {
                    rows = 5;
                    columns = 9;
                } else {
                    // Keep mutable sprites uncommon.  The other half of
                    // their placements becomes a glyph, including after the
                    // unique glyph pass has been exhausted.
                    if (DeriveRunSeed(seed, 0x535052495445ULL) % 2 == 0) {
                        useGlyph = true;
                        rows = 7;
                        columns = 5;
                    } else {
                        const auto& sprite = wallAssets.at(assetId).sprite;
                        rows = static_cast<int>(sprite.size());
                        columns = rows == 0 ? 0 :
                            static_cast<int>(sprite.front().size());
                    }
                }
            }
            if (rows == 0 || columns == 0) {
                ++placement;
                continue;
            }
            const float angle = UnitFloat(
                DeriveRunSeed(seed, 0x524f54415445ULL)) * 2.0f * kPi;
            const float cosine = std::abs(std::cos(angle));
            const float sine = std::abs(std::sin(angle));
            const float projectedUnits = horizontal
                ? columns * cosine + rows * sine
                : columns * sine + rows * cosine;
            const float remaining = length - offset;
            const float scale = std::min(
                baseScale, remaining / std::max(0.01f, projectedUnits));
            const float extent = projectedUnits * scale;
            const float centerX = horizontal
                ? wall.x + offset + extent * 0.5f
                : wall.x + wall.width * 0.5f;
            const float centerY = horizontal
                ? wall.y + wall.height * 0.5f
                : wall.y + offset + extent * 0.5f;
            const float originX = -columns * scale * 0.5f;
            const float originY = -rows * scale * 0.5f;
            const std::uint64_t placementId = placement++;

            if (useGlyph) {
                const std::uint8_t character = glyphOrdinal < glyphs.size()
                    ? glyphs[glyphOrdinal++]
                    : glyphs[DeriveRunSeed(
                        seed, 0x524550454154474cULL) % glyphs.size()];
                const auto& glyph = text_renderer::GetGlyph(character);
                for (int row = 0; row < 7; ++row)
                    for (int column = 0; column < 5; ++column) {
                        const auto& pixel = glyph[row][column];
                        if (!pixel.occupied) continue;
                        addPixel(ArenaMotifSource::Glyph, "", row, column,
                            character, 0, static_cast<std::uint32_t>(
                                (pixel.rgb[0] << 16) | (pixel.rgb[1] << 8) |
                                pixel.rgb[2]),
                            originX + column * scale,
                            originY + row * scale, scale, centerX, centerY,
                            angle, placementId);
                    }
            } else if (assetId == "@audio") {
                const Sound sound =
                    sounds[seed % (sizeof(sounds) / sizeof(sounds[0]))];
                const std::size_t count = (AudioSampleCount(sound) + 1) / 2;
                const std::size_t section = std::min<std::size_t>(columns, count);
                if (!section) {
                    offset += extent;
                    continue;
                }
                const std::size_t maximumStart = count - section;
                const std::size_t start = maximumStart == 0 ? 0 :
                    DeriveRunSeed(seed, 0x50434dULL) % (maximumStart + 1);
                for (std::size_t point = 0; point < section; ++point) {
                    const std::size_t sample = start + point;
                    const int across = static_cast<int>(
                        AudioPixelAverage(sound, sample) * 4 / 256);
                    const int value = AudioPixelAverage(sound, sample);
                    addPixel(ArenaMotifSource::Audio,
                        std::to_string(static_cast<int>(sound)),
                        static_cast<int>(sound), static_cast<int>(point), 0,
                        sample, static_cast<std::uint32_t>(
                            (value << 16) | (value << 8) | value),
                        originX + static_cast<float>(point) * scale,
                        originY + static_cast<float>(across) * scale,
                        scale, centerX, centerY, angle, placementId);
                }
            } else {
                const auto& sprite = wallAssets.at(assetId).sprite;
                for (int row = 0; row < static_cast<int>(sprite.size()); ++row)
                    for (int column = 0;
                         column < static_cast<int>(sprite[row].size());
                         ++column) {
                        const Pixel& pixel = sprite[row][column];
                        if (!pixel.occupied) continue;
                        addPixel(ArenaMotifSource::Asset, assetId, row, column,
                            0, 0, static_cast<std::uint32_t>(
                                (pixel.rgb[0] << 16) | (pixel.rgb[1] << 8) |
                                pixel.rgb[2]),
                            originX + column * scale,
                            originY + row * scale, scale, centerX, centerY,
                            angle, placementId);
                    }
            }
            offset += extent;
        }
    }
    return result;
}

void RebuildArenaCollisionCache(ArenaLevel& arena) {
    arena.collisionMotifs = BuildArenaMotifPixels(arena);
    arena.collisionCells.clear();
    for (std::size_t index = 0; index < arena.collisionMotifs.size();
         ++index) {
        const ArenaMotifPixel& motif = arena.collisionMotifs[index];
        float minX = motif.corners[0].x, maxX = minX;
        float minY = motif.corners[0].y, maxY = minY;
        for (const ArenaMotifPoint& corner : motif.corners) {
            minX = std::min(minX, corner.x);
            maxX = std::max(maxX, corner.x);
            minY = std::min(minY, corner.y);
            maxY = std::max(maxY, corner.y);
        }
        const int firstX = static_cast<int>(
            std::floor(minX / kCollisionCellSize));
        const int firstY = static_cast<int>(
            std::floor(minY / kCollisionCellSize));
        const int lastX = static_cast<int>(
            std::floor(maxX / kCollisionCellSize));
        const int lastY = static_cast<int>(
            std::floor(maxY / kCollisionCellSize));
        for (int y = firstY; y <= lastY; ++y)
            for (int x = firstX; x <= lastX; ++x)
                arena.collisionCells[CollisionCellKey(x, y)].push_back(index);
    }
}

bool HitArenaWallMotif(ArenaLevel& arena, const Rect& shot) {
    for (const AudioPanel& wall : arena.audioWalls)
        if (HitAudioPanel(wall, shot)) return true;
    for (const std::size_t index : CollisionCandidates(arena, shot)) {
        const ArenaMotifPixel& motif = arena.collisionMotifs[index];
        if (!MotifOverlaps(motif, shot)) continue;
        const float hitX = CenterX(shot), hitY = CenterY(shot);
        const ArenaMotifPoint& left = motif.corners[0];
        const ArenaMotifPoint& right = motif.corners[1];
        const float widthSquared = (right.x - left.x) * (right.x - left.x) +
            (right.y - left.y) * (right.y - left.y);
        const float along = ((hitX - left.x) * (right.x - left.x) +
            (hitY - left.y) * (right.y - left.y)) /
            std::max(0.01f, widthSquared);
        const int channel = std::clamp(static_cast<int>(along * 3), 0, 2);
        if (motif.source == ArenaMotifSource::Audio) {
            DamageAudioPixel(
                static_cast<Sound>(motif.row), motif.audioPixel,
                kAudioSampleDamage);
            RebuildArenaCollisionCache(arena);
            return true;
        }
        if (motif.source == ArenaMotifSource::Glyph) {
            auto& pixel = text_renderer::MutableGlyph(motif.character)
                              [motif.row][motif.column];
            pixel.rgb[channel] =
                std::max(0, pixel.rgb[channel] - kWallChannelDamage);
        } else {
            Pixel& pixel =
                wallAssets.at(motif.id).sprite[motif.row][motif.column];
            pixel.rgb[channel] =
                std::max(0, pixel.rgb[channel] - kWallChannelDamage);
        }
        SaveMutations();
        return true;
    }
    return ArenaGeometryOverlaps(arena, shot);
}

}  // namespace game
