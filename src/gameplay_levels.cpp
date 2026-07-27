#include <algorithm>
#include <cmath>

#include "audio_level.h"
#include "gameplay_internal.h"
#include "glyph_level.h"
#include "world.h"

namespace game {

namespace {

bool GlyphBombObstacle(const Rect& bomb) {
    return !GlyphLevelAllowsPlayer(bomb);
}

void DetonateGlyphBomb(float x, float y, int damage, float radius) {
    PlaySoundEffect(Sound::Explosion);
    if (DamageGlyphGeometryInRadius(
            x, y, radius, damage * kWallChannelDamage))
        MarkMutationsDirty();
    explosions.push_back({x, y, kExplosionDuration});
}

}  // namespace

void UpdateAudioMap(float dt) {
    float moveX = static_cast<float>(keys[1]) -
                  static_cast<float>(keys[0]);
    float moveY = static_cast<float>(keys[3]) -
                  static_cast<float>(keys[2]);
    const float movementLength =
        std::sqrt(moveX * moveX + moveY * moveY);
    if (movementLength > 0) {
        moveX /= movementLength;
        moveY /= movementLength;
    }
    Rect nextX{
        playerX + moveX * kPlayerSpeed * dt, playerY,
        kPlayerSize, kPlayerSize};
    if (AudioLevelAllowsPlayer(nextX)) playerX = nextX.x;
    Rect nextY{
        playerX, playerY + moveY * kPlayerSpeed * dt,
        kPlayerSize, kPlayerSize};
    if (AudioLevelAllowsPlayer(nextY)) playerY = nextY.y;

    UpdatePlayerWeapons(dt);

    for (Projectile& projectile : projectiles) {
        if (!WithinSimulationRange(projectile.x, projectile.y)) {
            projectile.distance = -1;
            continue;
        }
        const float speed =
            std::sqrt(projectile.vx * projectile.vx +
                      projectile.vy * projectile.vy);
        const int steps = std::max(
            1, static_cast<int>(std::ceil(speed * dt / 4.0f)));
        for (int step = 0;
             step < steps && projectile.distance >= 0; ++step) {
            const float dx = projectile.vx * dt / steps;
            const float dy = projectile.vy * dt / steps;
            projectile.x += dx;
            projectile.y += dy;
            projectile.distance += std::sqrt(dx * dx + dy * dy);
            const Rect shot{
                projectile.x, projectile.y,
                projectile.width, projectile.width};
            if (HitText(projectile) || HitAudioGeometry(shot))
                projectile.distance = -1;
        }
    }
    projectiles.erase(
        std::remove_if(
            projectiles.begin(), projectiles.end(),
            [](const Projectile& projectile) {
                return projectile.distance < 0 ||
                       projectile.distance > projectile.maxDistance;
            }),
        projectiles.end());
    if (pendingLevelSelection) {
        pendingLevelSelection = false;
        SelectCurrentLevel();
    }
}

void UpdateGlyphMap(float dt) {
    bombCooldown = std::max(0.0f, bombCooldown - dt);
    float moveX = static_cast<float>(keys[1]) -
                  static_cast<float>(keys[0]);
    float moveY = static_cast<float>(keys[3]) -
                  static_cast<float>(keys[2]);
    const float movementLength =
        std::sqrt(moveX * moveX + moveY * moveY);
    if (movementLength > 0) {
        moveX /= movementLength;
        moveY /= movementLength;
    }
    Rect nextX{
        playerX + moveX * kPlayerSpeed * dt, playerY,
        kPlayerSize, kPlayerSize};
    if (GlyphLevelAllowsPlayer(nextX)) playerX = nextX.x;
    Rect nextY{
        playerX, playerY + moveY * kPlayerSpeed * dt,
        kPlayerSize, kPlayerSize};
    if (GlyphLevelAllowsPlayer(nextY)) playerY = nextY.y;

    UpdatePlayerWeapons(dt);
    for (Projectile& projectile : projectiles) {
        HomeProjectile(projectile);
        const float speed =
            std::sqrt(projectile.vx * projectile.vx +
                      projectile.vy * projectile.vy);
        const int steps = std::max(
            1, static_cast<int>(std::ceil(speed * dt / 4.0f)));
        for (int step = 0;
             step < steps && projectile.distance >= 0; ++step) {
            const float dx = projectile.vx * dt / steps;
            const float dy = projectile.vy * dt / steps;
            projectile.x += dx;
            projectile.y += dy;
            projectile.distance += std::sqrt(dx * dx + dy * dy);
            const Rect shot{
                projectile.x, projectile.y,
                projectile.width, projectile.width};
            if (HitGlyphGeometry(shot)) {
                if (projectile.rocket)
                    DetonateGlyphBomb(
                        projectile.x, projectile.y, projectile.damage,
                        projectile.explosionRadius);
                MarkMutationsDirty();
                projectile.distance = -1;
            }
        }
    }
    projectiles.erase(
        std::remove_if(
            projectiles.begin(), projectiles.end(),
            [](const Projectile& projectile) {
                return projectile.distance < 0 ||
                       projectile.distance > projectile.maxDistance;
            }),
        projectiles.end());

    for (Bomb& bomb : bombs) {
        HomeBomb(bomb);
        const float bombSpeed =
            std::sqrt(bomb.vx * bomb.vx + bomb.vy * bomb.vy);
        const float travel =
            std::min(bomb.distanceRemaining, bombSpeed * dt);
        const int steps = std::max(
            1, static_cast<int>(std::ceil(travel / 4.0f)));
        const float stepDistance = travel / steps;
        for (int step = 0;
             step < steps && bomb.distanceRemaining > 0; ++step) {
            const float speed =
                std::sqrt(bomb.vx * bomb.vx + bomb.vy * bomb.vy);
            const float dx = bomb.vx / speed * stepDistance;
            const float dy = bomb.vy / speed * stepDistance;
            Rect nextBombX{bomb.x + dx - 5, bomb.y - 5, 10, 10};
            Rect nextBombY{bomb.x - 5, bomb.y + dy - 5, 10, 10};
            const bool hitX = GlyphBombObstacle(nextBombX);
            const bool hitY = GlyphBombObstacle(nextBombY);
            if (hitX)
                bomb.vx = -bomb.vx;
            else
                bomb.x += dx;
            if (hitY)
                bomb.vy = -bomb.vy;
            else
                bomb.y += dy;
            bomb.distanceRemaining -= stepDistance;
        }
    }
    for (const Bomb& bomb : bombs)
        if (bomb.distanceRemaining <= 0)
            DetonateGlyphBomb(
                bomb.x, bomb.y, bomb.damage, bomb.radius);
    bombs.erase(
        std::remove_if(
            bombs.begin(), bombs.end(),
            [](const Bomb& bomb) {
                return bomb.distanceRemaining <= 0;
            }),
        bombs.end());
    for (Explosion& explosion : explosions)
        explosion.timeRemaining -= dt;
    explosions.erase(
        std::remove_if(
            explosions.begin(), explosions.end(),
            [](const Explosion& explosion) {
                return explosion.timeRemaining <= 0;
            }),
        explosions.end());
}

}  // namespace game
