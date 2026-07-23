#include "rendering.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "gameplay.h"
#include "state.h"
#include "text_renderer.h"
#include "world.h"

namespace game {
namespace {

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

std::uint32_t CompositeColor(const Pixel& pixel, int divisor = 1) {
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

void DrawWorldRectAlpha(
    const Rect& rect, std::uint32_t color, float alpha) {
    DrawRectangleAlpha(
        static_cast<int>(rect.x - CameraX()),
        static_cast<int>(rect.y - CameraY()),
        static_cast<int>(rect.width),
        static_cast<int>(rect.height), color, alpha);
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

void DrawPhrase(
    const std::vector<int>& phrase, float x, float y,
    std::uint32_t color, bool world) {
    for (int word : phrase) {
        DrawWord(word, x, y, color, world);
        x += text_renderer::MeasureWidth(words[word].bytes.size()) +
             text_renderer::kGlyphAdvance;
    }
}

}  // namespace

bool InitializeBackBuffer(HWND window) {
    HDC target = GetDC(window);
    buffer.dc = CreateCompatibleDC(target);
    ReleaseDC(window, target);
    if (!buffer.dc) return false;
    RECT client{};
    GetClientRect(window, &client);
    ResizeBackBuffer(client.right, client.bottom);
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
            0x0000FFFF, false);
        HDC target = GetDC(window);
        BitBlt(
            target, 0, 0, buffer.width, buffer.height,
            buffer.dc, 0, 0, SRCCOPY);
        ReleaseDC(window, target);
        return;
    }
    if (rooms.empty()) return;
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
    for (const ShieldBlock& shield : shieldBlocks)
        if (shield.health > 0)
            DrawWorldRectAlpha(
                shield.rect, 0x0048C8FF,
                static_cast<float>(shield.health) / 10.0f);
    for (const TextBox& box : textBoxes)
        DrawWord(
            box.word, box.rect.x, box.rect.y,
            box.value ? 0x00FFFFFF : 0x00B8C8D8, true);
    for (const Spawner& spawner : spawners) {
        if (spawner.health <= 0) continue;
        std::uint32_t spawnerColor = 0x00A02050;
        if (spawner.enemyType == "triangle")
            spawnerColor = 0x004C72D8;
        else if (spawner.enemyType == "charger")
            spawnerColor = 0x00E07828;
        else if (spawner.enemyType == "shooter")
            spawnerColor = 0x00B048D0;
        DrawWorldRect(
            {spawner.x, spawner.y, 30, 30}, spawnerColor);
        DrawWorldRect(
            {spawner.x + 5, spawner.y + 5,
             static_cast<float>(spawner.health * 4), 5},
            0x00FFFFFF);
    }
    for (const Enemy& enemy : enemies) {
        if (enemy.type != "shooter" ||
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
        DrawChargerWindup(enemy);
    for (const Enemy& enemy : enemies)
        DrawEnemySprite(enemy);
    DrawWorldRect(
        {playerX, playerY, kPlayerSize, kPlayerSize},
        playerHealth > 0 ? 0x00FFFFFF : 0x00333333);
    for (const Projectile& projectile : projectiles)
        DrawWorldRect(
            {projectile.x, projectile.y,
             kProjectileSize, kProjectileSize},
            0x00FFFFFF);
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

    DrawRectangle(0, 0, buffer.width, 64, 0x00070B11);
    const auto help = phrases.find("help");
    if (help != phrases.end())
        DrawPhrase(help->second, 16, 42, 0x007FA9C4, false);
    const auto health = phrases.find("hud_health");
    if (health != phrases.end())
        DrawPhrase(health->second, 16, 16, 0x00B8C8D8, false);
    for (int value = 0; value < playerHealth; ++value)
        DrawRectangle(
            100 + value * 19, 16, 14, 14, 0x00FFFFFF);
    const float bombReady =
        1.0f - bombCooldown / kBombCooldownDuration;
    DrawRectangle(
        buffer.width - 70, 18, 54, 7, 0x00202A33);
    DrawRectangle(
        buffer.width - 70, 18,
        static_cast<int>(54 * bombReady), 7,
        bombCooldown <= 0 ? 0x0000FFFF : 0x00006688);
    HDC target = GetDC(window);
    BitBlt(
        target, 0, 0, buffer.width, buffer.height,
        buffer.dc, 0, 0, SRCCOPY);
    ReleaseDC(window, target);
}

}  // namespace game
