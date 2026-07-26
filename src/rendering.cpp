#include "rendering.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <sstream>

#include "audio.h"
#include "gameplay.h"
#include "rendering_internal.h"
#include "roguelite.h"
#include "state.h"
#include "text_renderer.h"
#include "world.h"

namespace game {

namespace {

bool OrganAtMinimum(const TextBox& box) {
    if (box.organ < 0 || box.organ >= static_cast<int>(organs.size()))
        return false;
    const std::string& id = organs[box.organ].id;
    EnemyDifficultyStat stat;
    if (id == "size") stat = EnemyDifficultyStat::Size;
    else if (id == "speed") stat = EnemyDifficultyStat::Speed;
    else if (id == "health") stat = EnemyDifficultyStat::Health;
    else if (id == "burst") stat = EnemyDifficultyStat::Burst;
    else if (id == "damage") stat = EnemyDifficultyStat::Damage;
    else if (id == "spawner_health")
        stat = EnemyDifficultyStat::SpawnerHealth;
    else if (id == "spawn_speed")
        stat = EnemyDifficultyStat::SpawnSpeed;
    else if (id == "child_capacity")
        stat = EnemyDifficultyStat::ChildCapacity;
    else
        return false;
    return DifficultyStage(run, interior.archetype, stat) == 0;
}

bool EnemyIsSimulated(const Enemy& enemy) {
    const Rect rect = EnemyRect(enemy);
    return WithinSimulationRange(CenterX(rect), CenterY(rect));
}

bool SpawnerIsSimulated(const Spawner& spawner) {
    return WithinSimulationRange(spawner.x + 15.0f, spawner.y + 15.0f);
}

struct PresentationRect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

PresentationRect BackBufferPresentationRect(HWND window) {
    RECT client{};
    GetClientRect(window, &client);
    const int clientWidth = std::max(0L, client.right - client.left);
    const int clientHeight = std::max(0L, client.bottom - client.top);
    if (clientWidth <= 0 || clientHeight <= 0 ||
        buffer.width <= 0 || buffer.height <= 0)
        return {};
    const float scale = std::min(
        static_cast<float>(clientWidth) / buffer.width,
        static_cast<float>(clientHeight) / buffer.height);
    const int width = std::max(
        1, static_cast<int>(std::lround(buffer.width * scale)));
    const int height = std::max(
        1, static_cast<int>(std::lround(buffer.height * scale)));
    return {
        (clientWidth - width) / 2,
        (clientHeight - height) / 2,
        width,
        height};
}

void PresentBackBuffer(HWND window) {
    const PresentationRect destination =
        BackBufferPresentationRect(window);
    if (destination.width <= 0 || destination.height <= 0) return;
    HDC target = GetDC(window);
    if (!target) return;
    RECT client{};
    GetClientRect(window, &client);
    const int clientWidth = client.right - client.left;
    const int clientHeight = client.bottom - client.top;
    const int destinationRight =
        destination.x + destination.width;
    const int destinationBottom =
        destination.y + destination.height;
    if (destination.y > 0)
        PatBlt(
            target, 0, 0, clientWidth, destination.y, BLACKNESS);
    if (destinationBottom < clientHeight)
        PatBlt(
            target, 0, destinationBottom, clientWidth,
            clientHeight - destinationBottom, BLACKNESS);
    if (destination.x > 0)
        PatBlt(
            target, 0, destination.y, destination.x,
            destination.height, BLACKNESS);
    if (destinationRight < clientWidth)
        PatBlt(
            target, destinationRight, destination.y,
            clientWidth - destinationRight,
            destination.height, BLACKNESS);
    SetStretchBltMode(target, COLORONCOLOR);
    StretchBlt(
        target,
        destination.x, destination.y,
        destination.width, destination.height,
        buffer.dc, 0, 0, buffer.width, buffer.height,
        SRCCOPY);
    ReleaseDC(window, target);
}

}  // namespace

void ClientToGameCoordinates(
    HWND window, int clientX, int clientY, int& gameX, int& gameY) {
    const PresentationRect destination =
        BackBufferPresentationRect(window);
    if (destination.width <= 0 || destination.height <= 0) {
        gameX = buffer.width / 2;
        gameY = buffer.height / 2;
        return;
    }
    const float normalizedX =
        static_cast<float>(clientX - destination.x) / destination.width;
    const float normalizedY =
        static_cast<float>(clientY - destination.y) / destination.height;
    gameX = std::clamp(
        static_cast<int>(std::floor(normalizedX * buffer.width)),
        0, std::max(0, buffer.width - 1));
    gameY = std::clamp(
        static_cast<int>(std::floor(normalizedY * buffer.height)),
        0, std::max(0, buffer.height - 1));
}

void DrawRectangle(
    int x, int y, int width, int height, std::uint32_t color) {
    if (!buffer.pixels) return;
    const int left = std::max(0, x);
    const int top = std::max(0, y);
    const int right = std::min(buffer.width, x + width);
    const int bottom = std::min(buffer.height, y + height);
    for (int pixelY = top; pixelY < bottom; ++pixelY) {
        std::uint32_t* row =
            buffer.pixels + pixelY * buffer.width;
        for (int pixelX = left; pixelX < right; ++pixelX)
            row[pixelX] = color;
    }
}

void BlendPixel(
    int x, int y, std::uint32_t color, float alpha) {
    if (x < 0 || y < 0 || x >= buffer.width ||
        y >= buffer.height || !buffer.pixels)
        return;
    alpha = std::clamp(alpha, 0.0f, 1.0f);
    const std::uint32_t destination =
        buffer.pixels[y * buffer.width + x];
    const int red = static_cast<int>(
        ((color >> 16) & 0xFF) * alpha +
        ((destination >> 16) & 0xFF) * (1.0f - alpha));
    const int green = static_cast<int>(
        ((color >> 8) & 0xFF) * alpha +
        ((destination >> 8) & 0xFF) * (1.0f - alpha));
    const int blue = static_cast<int>(
        (color & 0xFF) * alpha +
        (destination & 0xFF) * (1.0f - alpha));
    buffer.pixels[y * buffer.width + x] =
        static_cast<std::uint32_t>(
            (red << 16) | (green << 8) | blue);
}

void DrawRectangleAlpha(
    int x, int y, int width, int height,
    std::uint32_t color, float alpha) {
    alpha = std::clamp(alpha, 0.0f, 1.0f);
    if (alpha <= 0 || !buffer.pixels) return;
    if (alpha >= 1) {
        DrawRectangle(x, y, width, height, color);
        return;
    }
    const int left = std::max(0, x);
    const int top = std::max(0, y);
    const int right = std::min(buffer.width, x + width);
    const int bottom = std::min(buffer.height, y + height);
    for (int pixelY = top; pixelY < bottom; ++pixelY) {
        for (int pixelX = left; pixelX < right; ++pixelX)
            BlendPixel(pixelX, pixelY, color, alpha);
    }
}

std::uint32_t CompositeColor(const Pixel& pixel, int divisor) {
    return static_cast<std::uint32_t>(
               (pixel.rgb[0] / divisor) << 16) |
           static_cast<std::uint32_t>(
               (pixel.rgb[1] / divisor) << 8) |
           static_cast<std::uint32_t>(pixel.rgb[2] / divisor);
}

std::uint32_t ChannelColor(int channel, int value) {
    value = std::clamp(value, 0, 255);
    return channel == 0
        ? static_cast<std::uint32_t>(value << 16)
        : channel == 1
            ? static_cast<std::uint32_t>(value << 8)
            : static_cast<std::uint32_t>(value);
}

void DrawWorldRect(const Rect& rect, std::uint32_t color) {
    DrawRectangle(
        static_cast<int>(rect.x - CameraX()),
        static_cast<int>(rect.y - CameraY()),
        static_cast<int>(rect.width),
        static_cast<int>(rect.height), color);
}

void DrawArenaQuad(
    const std::array<ArenaMotifPoint, 4>& worldCorners,
    std::uint32_t color) {
    std::array<ArenaMotifPoint, 4> corners = worldCorners;
    float left = static_cast<float>(buffer.width);
    float top = static_cast<float>(buffer.height);
    float right = 0, bottom = 0;
    for (ArenaMotifPoint& corner : corners) {
        corner.x -= CameraX();
        corner.y -= CameraY();
        left = std::min(left, corner.x);
        top = std::min(top, corner.y);
        right = std::max(right, corner.x);
        bottom = std::max(bottom, corner.y);
    }
    const int firstX = std::max(0, static_cast<int>(std::floor(left)));
    const int firstY = std::max(0, static_cast<int>(std::floor(top)));
    const int lastX = std::min(
        buffer.width, static_cast<int>(std::ceil(right)));
    const int lastY = std::min(
        buffer.height, static_cast<int>(std::ceil(bottom)));
    for (int y = firstY; y < lastY; ++y)
        for (int x = firstX; x < lastX; ++x) {
            bool positive = false, negative = false;
            for (std::size_t edge = 0; edge < corners.size(); ++edge) {
                const ArenaMotifPoint& a = corners[edge];
                const ArenaMotifPoint& b =
                    corners[(edge + 1) % corners.size()];
                const float cross = (b.x - a.x) * (y + 0.5f - a.y) -
                    (b.y - a.y) * (x + 0.5f - a.x);
                positive = positive || cross > 0;
                negative = negative || cross < 0;
            }
            if (!(positive && negative))
                buffer.pixels[y * buffer.width + x] = color;
        }
}

void DrawWorldRectAlpha(
    const Rect& rect, std::uint32_t color, float alpha) {
    DrawRectangleAlpha(
        static_cast<int>(rect.x - CameraX()),
        static_cast<int>(rect.y - CameraY()),
        static_cast<int>(rect.width),
        static_cast<int>(rect.height), color, alpha);
}

void DrawPlayer() {
    if (playerInvincibility > 0 &&
        static_cast<int>(playerInvincibility * 12.0f) % 2 == 0)
        return;
    const auto found = types.find("player");
    if (found == types.end() || found->second.sprite.empty()) return;
    const auto& sprite = found->second.sprite;
    std::size_t columns = 0;
    for (const auto& row : sprite) columns = std::max(columns, row.size());
    const std::size_t units = std::max(columns, sprite.size());
    if (units == 0) return;
    const float scale = static_cast<float>(kPlayerRenderSize) / units;
    const float originX = playerX + kPlayerSize * 0.5f -
        columns * scale * 0.5f;
    const float originY = playerY + kPlayerSize * 0.5f -
        sprite.size() * scale * 0.5f;
    for (std::size_t row = 0; row < sprite.size(); ++row)
        for (std::size_t column = 0;
             column < sprite[row].size(); ++column) {
            const Pixel& pixel = sprite[row][column];
            if (!pixel.occupied) continue;
            std::uint32_t color = CompositeColor(pixel);
            if (playerHealth <= 0)
                color = ((color & 0x00FCFCFC) >> 2);
            const int left = static_cast<int>(std::lround(
                originX + column * scale - CameraX()));
            const int top = static_cast<int>(std::lround(
                originY + row * scale - CameraY()));
            const int right = static_cast<int>(std::lround(
                originX + (column + 1) * scale - CameraX()));
            const int bottom = static_cast<int>(std::lround(
                originY + (row + 1) * scale - CameraY()));
            DrawRectangle(
                left, top, std::max(1, right - left),
                std::max(1, bottom - top), color);
        }
}

void DrawWorldLine(
    float x, float y, float dx, float dy, float length, float width,
    std::uint32_t color, bool dotted) {
    const float spacing = dotted ? 16.0f : std::max(2.0f, width * 0.5f);
    const float mark = dotted ? 5.0f : spacing + 1.0f;
    for (float distance = 0; distance < length; distance += spacing)
        DrawWorldRect(
            {x + dx * distance - width * 0.5f,
             y + dy * distance - width * 0.5f,
             std::max(width, std::abs(dx) * mark),
             std::max(width, std::abs(dy) * mark)},
            color);
}

void DrawChargerWindup(const Enemy& enemy) {
    if (enemy.type != "charger" ||
        enemy.phase != EnemyPhase::Windup)
        return;
    const EnemyType& type = types.at(enemy.type);
    if (type.windupSeconds <= 0) return;
    const Rect rect = EnemyRect(enemy);
    const float centerX = CenterX(rect);
    const float centerY = CenterY(rect);
    const float remaining = std::clamp(
        enemy.phaseTimer / type.windupSeconds, 0.0f, 1.0f);
    const float radius = 18.0f + remaining * 55.0f;
    constexpr int particleCount = 10;
    for (int index = 0; index < particleCount; ++index) {
        const float angle =
            index * (kPi * 2.0f / particleCount) +
            (1.0f - remaining) * 0.7f;
        DrawWorldRect(
            {centerX + std::cos(angle) * radius - 2.0f,
             centerY + std::sin(angle) * radius - 2.0f,
             4.0f, 4.0f},
            0x00FF9A38);
    }
}

bool EnemyVisible(const Enemy& enemy) {
    const Rect rect = EnemyRect(enemy);
    constexpr float margin = 80.0f;
    return Overlaps(rect, {
        CameraX() - margin, CameraY() - margin,
        static_cast<float>(buffer.width) + margin * 2.0f,
        static_cast<float>(buffer.height) + margin * 2.0f});
}

void DrawEnemySprite(const Enemy& enemy) {
    const EnemyType& type = types.at(enemy.type);
    const int scale = EnemyScale(enemy);
    std::size_t columns = 0;
    for (const auto& row : type.sprite)
        columns = std::max(columns, row.size());
    const float width = static_cast<float>(columns * scale);
    const float height =
        static_cast<float>(type.sprite.size() * scale);
    float centerX = enemy.x + width * 0.5f;
    float centerY = enemy.y + height * 0.5f;
    if (enemy.type == "charger" &&
        enemy.phase == EnemyPhase::Windup) {
        centerX += std::sin(enemy.phaseTimer * 110.0f) * 2.0f;
        centerY += std::cos(enemy.phaseTimer * 137.0f) * 2.0f;
    }
    const float cosine = std::cos(enemy.facing);
    const float sine = std::sin(enemy.facing);
    const float extentX =
        std::abs(cosine) * width * 0.5f +
        std::abs(sine) * height * 0.5f;
    const float extentY =
        std::abs(sine) * width * 0.5f +
        std::abs(cosine) * height * 0.5f;
    const int left = std::max(
        0, static_cast<int>(
               std::floor(centerX - CameraX() - extentX)));
    const int right = std::min(
        buffer.width, static_cast<int>(
            std::ceil(centerX - CameraX() + extentX)));
    const int top = std::max(
        0, static_cast<int>(
               std::floor(centerY - CameraY() - extentY)));
    const int bottom = std::min(
        buffer.height, static_cast<int>(
            std::ceil(centerY - CameraY() + extentY)));
    const float fadeAlpha = 1.0f -
        enemy.activationRemaining / kEnemyFadeDuration;
    const float healthAlpha = enemy.maxHealth > 0
        ? static_cast<float>(enemy.health) / enemy.maxHealth : 0.0f;
    const float alpha =
        std::clamp(fadeAlpha * healthAlpha, 0.0f, 1.0f);
    for (int screenY = top; screenY < bottom; ++screenY)
        for (int screenX = left; screenX < right; ++screenX) {
            const float dx =
                screenX + CameraX() + 0.5f - centerX;
            const float dy =
                screenY + CameraY() + 0.5f - centerY;
            const float localX =
                cosine * dx + sine * dy + width * 0.5f;
            const float localY =
                -sine * dx + cosine * dy + height * 0.5f;
            if (localX < 0 || localY < 0 ||
                localX >= width || localY >= height)
                continue;
            const int column =
                static_cast<int>(localX / scale);
            const int row = static_cast<int>(localY / scale);
            if (row >= static_cast<int>(type.sprite.size()) ||
                column >= static_cast<int>(type.sprite[row].size()))
                continue;
            const Pixel& pixel = type.sprite[row][column];
            if (!pixel.occupied) continue;
            BlendPixel(
                screenX, screenY, CompositeColor(pixel), alpha);
        }
}

void DrawWord(
    int word, float x, float y, std::uint32_t color, bool world) {
    if (word < 0 || word >= static_cast<int>(words.size())) return;
    const int drawX = static_cast<int>(world ? x - CameraX() : x);
    const int drawY = static_cast<int>(world ? y - CameraY() : y);
    text_renderer::RenderText(
        {buffer.pixels, buffer.width, buffer.height},
        words[word].bytes.data(), words[word].bytes.size(),
        drawX, drawY, color);
}

void DrawTextString(
    const std::string& text, int x, int y, std::uint32_t color) {
    text_renderer::RenderText(
        {buffer.pixels, buffer.width, buffer.height},
        reinterpret_cast<const std::uint8_t*>(text.data()), text.size(),
        x, y, color);
}

std::string ShortNumber(float value) {
    std::ostringstream output;
    output << std::fixed << std::setprecision(2) << value;
    return output.str();
}

// Legacy wall renderers retained for reference. Arena walls are rendered
// through BuildArenaMotifPixels and DrawArenaQuad instead.
#if 0
void DrawSpriteWall(const Rect& wall, std::uint64_t seed) {
    if (types.empty()) return;
    auto selected = types.begin();
    std::advance(
        selected, static_cast<std::ptrdiff_t>(seed % types.size()));
    const auto& sprite = selected->second.sprite;
    if (sprite.empty()) return;
    const bool horizontal = wall.width >= wall.height;
    const float scale = 4.0f;
    std::size_t columns = 0;
    for (const auto& row : sprite) columns = std::max(columns, row.size());
    const float motifLength =
        (horizontal ? columns : sprite.size()) * scale + scale * 2;
    for (float offset = 0;
         offset < (horizontal ? wall.width : wall.height);
         offset += std::max(scale, motifLength))
        for (std::size_t row = 0; row < sprite.size(); ++row)
            for (std::size_t column = 0;
                 column < sprite[row].size(); ++column) {
                const Pixel& pixel = sprite[row][column];
                if (!pixel.occupied) continue;
                DrawWorldRect({
                    wall.x + (horizontal ? offset + column * scale
                                         : row * scale),
                    wall.y + (horizontal ? row * scale
                                         : offset + column * scale),
                    scale, scale}, CompositeColor(pixel));
            }
}

void DrawGlyphWall(
    const Rect& wall, std::uint64_t seed,
    const std::vector<std::uint8_t>& glyphOrder,
    std::size_t& glyphOrdinal) {
    const bool horizontal = wall.width >= wall.height;
    // Seven-pixel cells keep the whole arena perimeter below the 94 printable
    // glyphs, so a seeded arena never needs to repeat one.
    constexpr float scale = 7.0f;
    constexpr float motifLength = 5.0f * scale;
    for (float offset = 0;
         offset < (horizontal ? wall.width : wall.height);
         offset += motifLength + scale) {
        if (glyphOrdinal >= glyphOrder.size()) break;
        const std::uint8_t character = glyphOrder[glyphOrdinal++];
        const std::uint64_t glyphSeed =
            DeriveRunSeed(seed, 0x474c595048ULL, character);
        const int rotation = static_cast<int>(
            DeriveRunSeed(glyphSeed, 0x524f54415445ULL) % 4);
        const auto& glyph = text_renderer::GetGlyph(character);
        for (int row = 0; row < 7; ++row)
            for (int column = 0; column < 5; ++column) {
                const auto& pixel = glyph[row][column];
                if (!pixel.occupied) continue;
                int x = column, y = row;
                if (rotation == 1) { x = 6 - row; y = column; }
                if (rotation == 2) { x = 4 - column; y = 6 - row; }
                if (rotation == 3) { x = row; y = 4 - column; }
                const std::uint32_t color =
                    (pixel.rgb[0] << 16) | (pixel.rgb[1] << 8) |
                    pixel.rgb[2];
                DrawWorldRect({
                    wall.x + (horizontal ? offset + x * scale : y * scale),
                    wall.y + (horizontal ? y * scale : offset + x * scale),
                    scale, scale}, color);
            }
    }
}

void DrawWaveformWall(
    const Rect& wall, std::uint64_t seed, std::size_t wallIndex) {
    const Sound sounds[]{
        Sound::LaserShoot, Sound::HitEnemy, Sound::HitHurt,
        Sound::Explosion};
    const Sound sound = sounds[
        DeriveRunSeed(seed, 0x534f554e44ULL, wallIndex) % 4];
    const std::size_t pixelCount = (AudioSampleCount(sound) + 1) / 2;
    if (pixelCount == 0) return;
    const bool horizontal = wall.width >= wall.height;
    const float length = horizontal ? wall.width : wall.height;
    const float thickness = horizontal ? wall.height : wall.width;
    const std::size_t points = std::max<std::size_t>(
        2, static_cast<std::size_t>(length / 3.0f));
    const std::size_t section = std::min(points, pixelCount);
    const std::size_t maximumStart = pixelCount - section;
    const std::size_t start = maximumStart == 0 ? 0 :
        DeriveRunSeed(seed, 0x53454354494f4eULL, wallIndex) %
            (maximumStart + 1);
    for (std::size_t point = 0; point < points; ++point) {
        const std::size_t sample =
            start + std::min(section - 1, point * section / points);
        const float along = static_cast<float>(point) * length / points;
        const float amplitude =
            static_cast<float>(AudioPixelAverage(sound, sample)) / 255.0f;
        const float across = 2.0f + amplitude * std::max(1.0f, thickness - 5);
        DrawWorldRect({
            wall.x + (horizontal ? along : across),
            wall.y + (horizontal ? across : along),
            horizontal ? 4.0f : 3.0f,
            horizontal ? 3.0f : 4.0f}, 0x0000D8FF);
    }
}
#endif

void DrawProjectileVisual(const Projectile& projectile) {
    if ((projectile.rocket && projectile.homing) || projectile.boomerang) {
        const auto asset = wallAssets.find(
            projectile.boomerang ? "boomerang" : "homing_rocket");
        if (asset != wallAssets.end()) {
            constexpr float scale = 3.0f;
            const float centerX = projectile.x + projectile.width * 0.5f;
            const float centerY = projectile.y + projectile.width * 0.5f;
            if (!projectile.boomerang) {
                const float speed = std::sqrt(
                    projectile.vx * projectile.vx +
                    projectile.vy * projectile.vy);
                if (speed > 0.01f)
                    for (int index = 0; index < 3; ++index) {
                        const float phase =
                            projectile.distance * 0.08f + index * 2.2f;
                        const float behind = 12.0f + index * 6.0f +
                            std::sin(phase) * 2.0f;
                        const float sideways = std::cos(phase * 1.7f) *
                            (2.0f + index);
                        DrawWorldRect({
                            centerX - projectile.vx / speed * behind -
                                projectile.vy / speed * sideways,
                            centerY - projectile.vy / speed * behind +
                                projectile.vx / speed * sideways,
                            2.0f, 2.0f},
                            index == 0 ? 0x00FF3030 : 0x00C02020);
                    }
            }
            const int rotation = projectile.boomerang
                ? static_cast<int>(projectile.distance / 54.0f) % 4 : 0;
            const float rocketAngle = projectile.boomerang ? 0.0f :
                std::atan2(projectile.vy, projectile.vx) + kPi * 0.5f;
            const float cosine = std::cos(rocketAngle);
            const float sine = std::sin(rocketAngle);
            for (std::size_t row = 0; row < asset->second.sprite.size(); ++row)
                for (std::size_t column = 0;
                     column < asset->second.sprite[row].size(); ++column) {
                    const Pixel& pixel = asset->second.sprite[row][column];
                    if (!pixel.occupied) continue;
                    int x = static_cast<int>(column);
                    int y = static_cast<int>(row);
                    if (rotation == 1) {
                        x = 6 - static_cast<int>(row);
                        y = static_cast<int>(column);
                    }
                    if (rotation == 2) {
                        x = 4 - static_cast<int>(column);
                        y = 6 - static_cast<int>(row);
                    }
                    if (rotation == 3) {
                        x = static_cast<int>(row);
                        y = 4 - static_cast<int>(column);
                    }
                    float localX = (static_cast<float>(x) - 2.5f) * scale;
                    float localY = (static_cast<float>(y) - 3.5f) * scale;
                    if (!projectile.boomerang) {
                        const float rotatedX =
                            cosine * localX - sine * localY;
                        const float rotatedY =
                            sine * localX + cosine * localY;
                        localX = rotatedX;
                        localY = rotatedY;
                    }
                    DrawWorldRect(
                        {centerX + localX, centerY + localY, scale, scale},
                        CompositeColor(pixel));
                }
            return;
        }
    }
    DrawWorldRect(
        {projectile.x, projectile.y,
         projectile.width, projectile.width},
        0x00FFFFFF);
}

void DrawPortalEffect(const Rect& portal, const char* assetId) {
    static const auto started = std::chrono::steady_clock::now();
    const float time = std::chrono::duration<float>(
        std::chrono::steady_clock::now() - started).count();
    const float pulse = 1.0f + 0.08f * std::sin(time * 2.0f);
    const float centerX = CenterX(portal);
    const float centerY = CenterY(portal);
    const auto asset = wallAssets.find(assetId);
    if (asset != wallAssets.end())
        {
        std::size_t columns = 0;
        for (const auto& row : asset->second.sprite)
            columns = std::max(columns, row.size());
        const float pixelSize = std::min(
            portal.width / std::max<std::size_t>(1, columns),
            portal.height / std::max<std::size_t>(
                1, asset->second.sprite.size()));
        for (std::size_t row = 0; row < asset->second.sprite.size(); ++row)
            for (std::size_t column = 0;
                 column < asset->second.sprite[row].size(); ++column) {
                const Pixel& pixel = asset->second.sprite[row][column];
                if (!pixel.occupied) continue;
                const float size = pixelSize * pulse;
                DrawWorldRect(
                    {centerX + (static_cast<float>(column) -
                        static_cast<float>(columns) * 0.5f) * size,
                     centerY + (static_cast<float>(row) -
                        static_cast<float>(asset->second.sprite.size()) *
                            0.5f) * size,
                     size, size},
                    CompositeColor(pixel));
            }
        }
    for (int index = 0; index < 12; ++index) {
        const float phase = std::fmod(
            time * 0.55f + static_cast<float>(index) / 12.0f, 1.0f);
        const float angle =
            static_cast<float>(index) * (2.0f * kPi / 12.0f) +
            time * 0.22f;
        const float radius = (1.0f - phase) *
            (std::max(portal.width, portal.height) * 0.85f + 26.0f);
        const float size = 2.0f + (1.0f - phase) * 2.0f;
        DrawWorldRectAlpha(
            {centerX + std::cos(angle) * radius - size * 0.5f,
             centerY + std::sin(angle) * radius - size * 0.5f,
             size, size},
            0x0000E8FF, 0.25f + phase * 0.65f);
    }
}

void DrawPhrase(
    const std::vector<int>& phrase, float x, float y,
    std::uint32_t color, bool world) {
    for (int word : phrase) {
        DrawWord(word, x, y, color, world);
        x += text_renderer::MeasureWidth(words[word].bytes.size()) +
             text_renderer::kGlyphAdvance;
    }
}

bool InitializeBackBuffer(HWND window) {
    HDC target = GetDC(window);
    buffer.dc = CreateCompatibleDC(target);
    ReleaseDC(window, target);
    if (!buffer.dc) return false;
    ResizeBackBuffer(kInitialWidth, kInitialHeight);
    return buffer.bitmap != nullptr;
}

void ResizeBackBuffer(int width, int height) {
    buffer.width = width;
    buffer.height = height;
    if (!buffer.dc) return;
    if (buffer.bitmap) {
        SelectObject(buffer.dc, buffer.previousBitmap);
        DeleteObject(buffer.bitmap);
        buffer.bitmap = nullptr;
        buffer.pixels = nullptr;
    }
    if (width <= 0 || height <= 0) return;
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* pixels = nullptr;
    buffer.bitmap = CreateDIBSection(
        buffer.dc, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
    if (buffer.bitmap) {
        buffer.pixels = static_cast<std::uint32_t*>(pixels);
        buffer.previousBitmap = reinterpret_cast<HBITMAP>(
            SelectObject(buffer.dc, buffer.bitmap));
    }
}

void DestroyBackBuffer() {
    if (buffer.bitmap) {
        SelectObject(buffer.dc, buffer.previousBitmap);
        DeleteObject(buffer.bitmap);
        buffer.bitmap = nullptr;
        buffer.pixels = nullptr;
    }
    if (buffer.dc) {
        DeleteDC(buffer.dc);
        buffer.dc = nullptr;
    }
}

void Render(HWND window) {
    if (!buffer.pixels) return;
    std::fill(
        buffer.pixels,
        buffer.pixels +
            static_cast<std::size_t>(buffer.width) * buffer.height,
        0x0004070B);
    if (currentMap == "audio") {
        const LevelRegion* region = CurrentLevelRegion();
        if (region) {
            DrawWorldRect(
                {region->x, region->y, region->width, region->height},
                0x00070B11);
            constexpr float border = 20.0f;
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
        }
        DrawAudioPanels();
        for (const TextBox& box : textBoxes)
            DrawWord(
                box.word, box.rect.x, box.rect.y,
                0x00FFFFFF, true);
        DrawPlayer();
        for (const Projectile& projectile : projectiles)
            DrawProjectileVisual(projectile);
        PresentBackBuffer(window);
        return;
    }
    if (currentMap == "glyph") {
        DrawGlyphArena();
        DrawPlayer();
        for (const Projectile& projectile : projectiles)
            DrawProjectileVisual(projectile);
        for (const Bomb& bomb : bombs) {
            DrawWorldRect(
                {bomb.x - 5, bomb.y - 5, 11, 11}, 0x0000FFFF);
            DrawWorldRect(
                {bomb.x - 2, bomb.y - 2, 5, 5}, 0x00FFFFFF);
        }
        for (const Explosion& explosion : explosions) {
            const float progress =
                explosion.timeRemaining / kExplosionDuration;
            const float radius =
                kBombRadius * (1.0f - progress * 0.25f);
            DrawWorldRect(
                {explosion.x - radius, explosion.y - radius,
                 radius * 2, 4},
                0x0000FFFF);
            DrawWorldRect(
                {explosion.x - radius, explosion.y + radius - 4,
                 radius * 2, 4},
                0x000066AA);
        }
        PresentBackBuffer(window);
        return;
    }
    if (currentMap != "interior") {
        std::vector<int> line = levelLabel;
        if (levelValueWord >= 0) line.push_back(levelValueWord);
        const auto placeholder = phrases.find("placeholder");
        if (placeholder != phrases.end())
            line.insert(
                line.end(), placeholder->second.begin(),
                placeholder->second.end());
        const int width = PhraseWidth(line);
        DrawPhrase(
            line, (buffer.width - width) * 0.5f,
            buffer.height * 0.5f -
                text_renderer::kGlyphHeight * 0.5f,
            0x00FFFFFF, false);
        PresentBackBuffer(window);
        return;
    }
    const bool runMode = run.status == RunStatus::Active ||
        run.status == RunStatus::Won;
    const RunNode* runNode = CurrentRunNode();
    const bool runInterior =
        runMode && !DebugRoomActive() && !run.mapActive && runNode &&
        (runNode->type == RunNodeType::Interior ||
         runNode->type == RunNodeType::PlayerInterior ||
         runNode->type == RunNodeType::BossInterior);
    if ((runMode || MainMenuActive()) && !runInterior) {
        if (run.mapActive)
            DrawRunMap();
        else
            DrawRunArena();
    } else if (!MainMenuActive()) {
    if (rooms.empty()) {
        PresentBackBuffer(window);
        return;
    }
    const EnemyType& giant = types.at(interior.archetype);
    for (const Room& room : rooms) {
        Rect floor{
            RoomX(room), RoomY(room),
            static_cast<float>(interior.roomSize),
            static_cast<float>(interior.roomSize)};
        Rect view{
            CameraX(), CameraY(),
            static_cast<float>(buffer.width),
            static_cast<float>(buffer.height)};
        if (!Overlaps(floor, view)) continue;
        const Pixel& pixel =
            giant.sprite[room.pixelRow][room.pixelColumn];
        DrawWorldRect(floor, CompositeColor(pixel, 7));
    }
    const int activeRoom = CurrentRoom();
    const std::vector<WallRect> walls = BuildWalls();
    for (const WallRect& wall : walls) {
        if (wall.room == activeRoom) continue;
        const Room& room = rooms[wall.room];
        const Pixel& pixel =
            giant.sprite[room.pixelRow][room.pixelColumn];
        DrawWorldRect(wall.rect, CompositeColor(pixel));
    }
    // Draw the active room last so its shared boundaries expose the individual
    // channels instead of being overwritten by a neighboring composite wall.
    for (const WallRect& wall : walls) {
        if (wall.room != activeRoom) continue;
        const Room& room = rooms[wall.room];
        const Pixel& pixel =
            giant.sprite[room.pixelRow][room.pixelColumn];
        const float roomLeft = RoomX(room);
        const float third = interior.roomSize / 3.0f;
        for (int channel = 0; channel < 3; ++channel) {
            const float left = std::max(
                wall.rect.x, roomLeft + channel * third);
            const float right = std::min(
                wall.rect.x + wall.rect.width,
                roomLeft + (channel + 1) * third);
            if (right > left)
                DrawWorldRect(
                    {left, wall.rect.y, right - left,
                     wall.rect.height},
                    ChannelColor(channel, pixel.rgb[channel]));
        }
    }
    }
    if (runInterior && runNode->type == RunNodeType::PlayerInterior &&
        (runNode->playerInteriorAlteration < 0 ||
         runNode->playerInteriorAlterationTimer > 0)) {
        static constexpr std::array<PlayerAlteration, 9> layout{{
            PlayerAlteration::SecondMultishot,
            PlayerAlteration::ExtraProjectileLimiter,
            PlayerAlteration::DualPrimary,
            PlayerAlteration::InfiniteHoming,
            PlayerAlteration::Regeneration,
            PlayerAlteration::InfiniteMultishot,
            PlayerAlteration::DualSecondary,
            PlayerAlteration::ExtraLife,
            PlayerAlteration::InfiniteAutoRocket}};
        for (int room = 0; room < static_cast<int>(layout.size()); ++room) {
            const int alteration = static_cast<int>(layout[room]);
            const bool selected =
                runNode->playerInteriorAlteration == alteration;
            if (runNode->playerInteriorAlteration >= 0 && !selected)
                continue;
            const Rect target = PlayerAlterationTarget(room);
            const bool taken = playerInteriorState.permanent[alteration - 3];
            const float fade = !selected ? 1.0f : std::clamp(
                runNode->playerInteriorAlterationTimer /
                    kPlayerAlterationFadeSeconds,
                0.0f, 1.0f);
            DrawWorldRectAlpha(
                target, selected ? 0x00502070 :
                    (taken ? 0x00252A30 : 0x00502070), fade);
            const int channel =
                std::clamp(static_cast<int>(255 * fade), 0, 255);
            const std::uint32_t textColor = static_cast<std::uint32_t>(
                (channel << 16) | (channel << 8) | channel);
            const std::string label = PlayerAlterationName(
                static_cast<PlayerAlteration>(alteration));
            DrawTextString(label,
                static_cast<int>(CenterX(target) -
                    text_renderer::MeasureWidth(label.size()) * 0.5f -
                    CameraX()),
                static_cast<int>(CenterY(target) -
                    text_renderer::kGlyphHeight -
                    text_renderer::kGlyphAdvance * 0.5f - CameraY()),
                textColor);
            const std::string state = taken ? "OFF" : "ON";
            DrawTextString(state,
                static_cast<int>(CenterX(target) -
                    text_renderer::MeasureWidth(state.size()) * 0.5f -
                    CameraX()),
                static_cast<int>(CenterY(target) +
                    text_renderer::kGlyphAdvance * 0.5f - CameraY()),
                textColor);
        }
    }
    if (runInterior && runNode->type == RunNodeType::BossInterior) {
        for (const BossTurret& turret : run.boss.turrets) {
            const Rect target = BossTurretTargetRect(turret);
            if (target.width <= 0) continue;
            DrawWorldRect(
                target, turret.alive ? 0x00D04060 : 0x00252A30);
            const std::string label = turret.alive
                ? (turret.kind == BossTurretKind::Rocket
                    ? "ROCKET " + std::to_string(turret.health)
                    : "BURST " + std::to_string(turret.health))
                : "DISABLED";
            DrawTextString(
                label,
                static_cast<int>(
                    CenterX(target) -
                    text_renderer::MeasureWidth(label.size()) * 0.5f -
                    CameraX()),
                static_cast<int>(
                    CenterY(target) -
                    text_renderer::kGlyphHeight * 0.5f - CameraY()),
                0x00FFFFFF);
        }
    }
    for (const ShieldBlock& shield : shieldBlocks)
        if (shield.health > 0) {
            DrawWorldRectAlpha(
                shield.rect, 0x0048C8FF,
                static_cast<float>(shield.health) / 10.0f);
            const auto asset = wallAssets.find("shield");
            if (asset != wallAssets.end()) {
                const float pixel = std::max(3.0f, std::min(
                    shield.rect.width / 5.0f, shield.rect.height / 7.0f));
                const float left = CenterX(shield.rect) - pixel * 2.5f;
                const float top = CenterY(shield.rect) - pixel * 3.5f;
                for (std::size_t row = 0;
                     row < asset->second.sprite.size(); ++row)
                    for (std::size_t column = 0;
                         column < asset->second.sprite[row].size(); ++column) {
                        const Pixel& value =
                            asset->second.sprite[row][column];
                        if (value.occupied)
                            DrawWorldRect(
                                {left + column * pixel, top + row * pixel,
                                 pixel, pixel},
                                CompositeColor(value));
                    }
            }
        }
    for (const TextBox& box : textBoxes)
        DrawWord(
            box.word, box.rect.x, box.rect.y,
            OrganAtMinimum(box) ? 0x00FF4040 : 0x00FFFFFF, true);
    if (runInterior)
        for (const RunPortal& portal : runNode->portals) {
            if (!portal.active) continue;
            const Rect trigger = portal.interiorTrigger.width > 0
                ? portal.interiorTrigger : ExitPortalRect();
            const RunNode* destination =
                GetRunNode(run, portal.destination);
            const char* assetId = !destination ? "portal_arena" :
                destination->type == RunNodeType::PlayerInterior
                    ? "portal_player_interior" :
                destination->type == RunNodeType::BossInterior
                    ? "portal_boss_interior" :
                destination->type == RunNodeType::Interior
                    ? "portal_interior" :
                destination->type == RunNodeType::Shop ? "portal_shop" :
                destination->type == RunNodeType::Boss ? "portal_boss" :
                "portal_arena";
            DrawPortalEffect(trigger, assetId);
            (void)destination;
        }
    for (const Spawner& spawner : spawners) {
        if (spawner.health <= 0 || !SpawnerIsSimulated(spawner)) continue;
        std::uint32_t spawnerColor = 0x00A02050;
        if (spawner.enemyType == "triangle")
            spawnerColor = 0x004C72D8;
        else if (spawner.enemyType == "charger")
            spawnerColor = 0x00E07828;
        else if (spawner.enemyType == "shooter")
            spawnerColor = 0x00B048D0;
        const float progress = std::clamp(
            1.0f - spawner.timer / std::max(0.001f, spawner.spawnDelay),
            0.0f, 1.0f);
        const float strength = progress * progress * 5.0f;
        const float elapsed = std::max(0.0f, spawner.spawnDelay - spawner.timer);
        const float phase = elapsed * (12.0f + progress * 48.0f) +
            static_cast<float>(spawner.id % 97);
        const float vibrationX = std::sin(phase * 1.71f) * strength;
        const float vibrationY = std::cos(phase * 2.33f) * strength;
        DrawWorldRect(
            {spawner.x + vibrationX, spawner.y + vibrationY, 30, 30},
            spawnerColor);
        DrawWorldRect(
            {spawner.x + vibrationX + 5, spawner.y + vibrationY + 5,
             static_cast<float>(spawner.health * 4), 5},
            0x00FFFFFF);
    }
    for (const Enemy& enemy : enemies) {
        if (!EnemyIsSimulated(enemy) || enemy.type != "shooter" ||
            enemy.phase != EnemyPhase::Windup)
            continue;
        const Rect source = EnemyRect(enemy);
        const float startX = CenterX(source);
        const float startY = CenterY(source);
        float dx = enemy.targetX - startX;
        float dy = enemy.targetY - startY;
        const float length = std::sqrt(dx * dx + dy * dy);
        if (length <= 0.01f) continue;
        dx /= length;
        dy /= length;
        DrawWorldLine(
            startX, startY, dx, dy,
            types.at(enemy.type).attackDistance, 3.0f,
            0x00FF70FF, true);
    }
    for (const EnemyRail& rail : enemyRails)
        DrawWorldLine(
            rail.x, rail.y, rail.dx, rail.dy, rail.length, rail.width,
            0x00FFFFFF, false);
    for (const Enemy& enemy : enemies)
        if (EnemyIsSimulated(enemy) && EnemyVisible(enemy))
            DrawChargerWindup(enemy);
    for (const Enemy& enemy : enemies)
        if (EnemyIsSimulated(enemy) && EnemyVisible(enemy))
            DrawEnemySprite(enemy);
    DrawPlayer();
    for (const Projectile& projectile : projectiles)
        DrawProjectileVisual(projectile);
    for (const Bomb& bomb : bombs) {
        DrawWorldRect(
            {bomb.x - 5, bomb.y - 5, 11, 11}, 0x0000FFFF);
        DrawWorldRect(
            {bomb.x - 2, bomb.y - 2, 5, 5}, 0x00FFFFFF);
    }
    for (const Explosion& explosion : explosions) {
        const float progress =
            explosion.timeRemaining / kExplosionDuration;
        const float radius =
            kBombRadius * (1.0f - progress * 0.25f);
        const std::uint32_t color =
            progress > 0.5f ? 0x0000FFFF : 0x000066AA;
        DrawWorldRect(
            {explosion.x - radius, explosion.y - radius,
             radius * 2, 4},
            color);
        DrawWorldRect(
            {explosion.x - radius,
             explosion.y + radius - 4, radius * 2, 4},
            color);
        DrawWorldRect(
            {explosion.x - radius, explosion.y - radius,
             4, radius * 2},
            color);
        DrawWorldRect(
            {explosion.x + radius - 4,
             explosion.y - radius, 4, radius * 2},
            color);
    }

    if (runMode) {
        if (!PostBossTuningRoomActive()) DrawRunHud();
        if (run.mapActive) {
            const std::string prompt = "CHOOSE NEXT PATH";
            DrawTextString(
                prompt,
                (buffer.width -
                    text_renderer::MeasureWidth(prompt.size())) / 2,
                54, 0x00FFFFFF);
        }
    } else if (!MainMenuActive()) {
    DrawRectangle(0, 0, buffer.width, 64, 0x00070B11);
    const auto help = phrases.find("help");
    if (help != phrases.end())
        DrawPhrase(help->second, 16, 42, 0x00FFFFFF, false);
    const auto health = phrases.find("hud_health");
    if (health != phrases.end())
        DrawPhrase(health->second, 16, 16, 0x00FFFFFF, false);
    for (int value = 0; value < playerHealth; ++value)
        DrawRectangle(
            100 + value * 19, 16, 14, 14, 0x00FFFFFF);
    const float bombReady =
        1.0f - bombCooldown /
            std::max(0.01f, SecondaryCooldownDuration());
    DrawRectangle(
        buffer.width - 70, 18, 54, 7, 0x00202A33);
    DrawRectangle(
        buffer.width - 70, 18,
        static_cast<int>(54 * bombReady), 7,
        bombCooldown <= 0 ? 0x0000FFFF : 0x00006688);
    }
    PresentBackBuffer(window);
}

}  // namespace game
