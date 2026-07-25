#include <algorithm>
#include <cmath>

#include "audio.h"
#include "gameplay_internal.h"
#include "roguelite.h"
#include "world.h"

namespace game {

bool BossInteriorMode() {
    const RunNode* node = CurrentRunNode();
    return (run.status == RunStatus::Active ||
            run.status == RunStatus::Won) &&
        !run.mapActive && node &&
        node->type == RunNodeType::BossInterior;
}

bool BossFightMode() {
    const RunNode* node = CurrentRunNode();
    return (run.status == RunStatus::Active ||
            run.status == RunStatus::Won) &&
        !run.mapActive && node &&
        node->type == RunNodeType::Boss && run.boss.active;
}

Rect BossTurretTargetRect(const BossTurret& turret) {
    if (turret.targetRoom < 0 ||
        turret.targetRoom >= static_cast<int>(rooms.size()))
        return {};
    const Room& room = rooms[turret.targetRoom];
    return {
        RoomX(room) + interior.roomSize * 0.5f - 28.0f,
        RoomY(room) + interior.roomSize * 0.5f - 28.0f,
        56, 56};
}

const char* BossQuadrantName(BossQuadrant quadrant) {
    switch (quadrant) {
        case BossQuadrant::NorthWest: return "NW";
        case BossQuadrant::NorthEast: return "NE";
        case BossQuadrant::SouthWest: return "SW";
        case BossQuadrant::SouthEast: return "SE";
        case BossQuadrant::Count: break;
    }
    return "Q";
}

namespace {

float BossMountAngle(BossQuadrant quadrant, int index, int count) {
    const float base =
        quadrant == BossQuadrant::NorthWest ? -2.6f :
        quadrant == BossQuadrant::NorthEast ? -0.55f :
        quadrant == BossQuadrant::SouthEast ? 0.55f : 2.05f;
    const float span = 0.9f;
    if (count <= 1) return base + span * 0.5f;
    return base + span * (static_cast<float>(index) + 0.5f) /
        static_cast<float>(count);
}

void FireBossBullet(BossTurret& turret, bool rocket) {
    const float muzzleX = run.boss.centerX +
        std::cos(turret.mountAngle) * run.boss.radiusX;
    const float muzzleY = run.boss.centerY +
        std::sin(turret.mountAngle) * run.boss.radiusY;
    const float speed = rocket
        ? bossTuning.rocketSpeed : bossTuning.bulletSpeed;
    EnemyProjectile shot;
    shot.x = muzzleX;
    shot.y = muzzleY;
    shot.vx = std::cos(turret.aim) * speed;
    shot.vy = std::sin(turret.aim) * speed;
    shot.rocket = rocket;
    shot.trackRemaining = rocket ? bossTuning.rocketTrackSeconds : 0;
    shot.damage = rocket ? 2 : 1;
    run.boss.projectiles.push_back(shot);
}

}  // namespace

void BuildBossTurrets(
    std::uint64_t seed, bool includeCleared, BossQuadrant onlyQuadrant,
    bool filterQuadrant) {
    run.boss.turrets.clear();
    run.boss.projectiles.clear();
    const float halfSweep =
        bossTuning.sweepDegrees * kPi / 180.0f * 0.5f;
    int midRow = 0, midCol = 0;
    if (types.count("boss")) BossSpriteMidpoint(midRow, midCol);
    for (int quadrantIndex = 0; quadrantIndex < 4; ++quadrantIndex) {
        const BossQuadrant quadrant =
            static_cast<BossQuadrant>(quadrantIndex);
        if (filterQuadrant && quadrant != onlyQuadrant) continue;
        if (!includeCleared &&
            run.clearedBossQuadrants[quadrantIndex])
            continue;
        std::vector<int> quadrantRooms;
        for (int room = 0; room < static_cast<int>(rooms.size()); ++room)
            if (BossQuadrantForCell(
                    rooms[room].row, rooms[room].column,
                    midRow, midCol) == quadrant)
                quadrantRooms.push_back(room);
        const int burstCount = bossTuning.burstTurretsPerQuadrant;
        const int rocketCount = bossTuning.rocketTurretsPerQuadrant;
        const int total = burstCount + rocketCount;
        for (int index = 0; index < total; ++index) {
            BossTurret turret;
            turret.quadrant = quadrant;
            turret.kind = index < burstCount
                ? BossTurretKind::Burst : BossTurretKind::Rocket;
            turret.mountAngle = BossMountAngle(
                quadrant, index, total);
            turret.aim = turret.mountAngle;
            turret.sweepMin = turret.mountAngle - halfSweep;
            turret.sweepMax = turret.mountAngle + halfSweep;
            turret.sweepSpeed = bossTuning.sweepSpeed *
                (1.0f + 0.15f * static_cast<float>(
                    DeriveRunSeed(seed, 0x53574545ULL, index) % 3));
            turret.sweepDirection =
                DeriveRunSeed(seed, 0x444952ULL, index) & 1 ? 1.0f : -1.0f;
            turret.fireCooldown = 0.4f + 0.2f * static_cast<float>(index);
            turret.alive = true;
            turret.health = bossTuning.turretTargetHealth;
            if (!quadrantRooms.empty())
                turret.targetRoom = quadrantRooms[
                    DeriveRunSeed(seed, 0x524f4f4dULL, index) %
                        quadrantRooms.size()];
            run.boss.turrets.push_back(turret);
        }
    }
}

bool PointInBoss(float x, float y) {
    if (!run.boss.active) return false;
    const float dx = (x - run.boss.centerX) / run.boss.radiusX;
    const float dy = (y - run.boss.centerY) / run.boss.radiusY;
    return dx * dx + dy * dy <= 1.0f;
}

bool RectHitsBoss(const Rect& shot) {
    return PointInBoss(CenterX(shot), CenterY(shot));
}

void UnlockBossInteriorPortals() {
    RunNode* node = CurrentRunNode();
    if (!node || node->type != RunNodeType::BossInterior) return;
    node->completed = true;
    run.clearedBossQuadrants[static_cast<int>(node->bossQuadrant)] = true;
    for (RunPortal& portal : node->portals) {
        portal.active = true;
        if (node->interiorEntryRoom >= 0 &&
            node->interiorEntryRoom < static_cast<int>(rooms.size())) {
            const Room& room = rooms[node->interiorEntryRoom];
            portal.interiorTrigger = {
                RoomX(room) + interior.roomSize * 0.5f - 36.0f,
                RoomY(room) + interior.roomSize * 0.5f - 36.0f,
                72, 72};
        }
    }
    RebuildGameplayTextBoxes();
}

void CheckBossInteriorCompletion() {
    if (!BossInteriorMode()) return;
    if (run.boss.turrets.empty()) return;
    const bool cleared = std::all_of(
        run.boss.turrets.begin(), run.boss.turrets.end(),
        [](const BossTurret& turret) { return !turret.alive; });
    RunNode* node = CurrentRunNode();
    if (cleared && node && !node->completed)
        UnlockBossInteriorPortals();
}

bool HitBossTurretTarget(const Rect& shot, int damage) {
    if (!BossInteriorMode()) return false;
    for (BossTurret& turret : run.boss.turrets) {
        if (!turret.alive) continue;
        const Rect target = BossTurretTargetRect(turret);
        if (!Overlaps(shot, target)) continue;
        turret.health -= damage;
        if (turret.health <= 0) {
            turret.alive = false;
            PlaySoundEffect(Sound::Explosion);
            CheckBossInteriorCompletion();
        } else {
            PlaySoundEffect(Sound::HitEnemy);
        }
        return true;
    }
    return false;
}

bool HitBoss(const Rect& shot, int damage) {
    if (!BossFightMode() || run.boss.health <= 0) return false;
    if (!RectHitsBoss(shot)) return false;
    run.boss.health = std::max(0, run.boss.health - damage);
    PlaySoundEffect(Sound::HitEnemy);
    if (run.boss.health <= 0) {
        RunNode* node = CurrentRunNode();
        if (node) {
            node->completed = true;
            run.status = RunStatus::Won;
            run.boss.turrets.clear();
            run.boss.projectiles.clear();
        }
        PlaySoundEffect(Sound::Explosion);
    }
    return true;
}

void UpdateBossFight(float dt) {
    if (!BossFightMode() || run.boss.health <= 0) return;
    run.boss.contactCooldown =
        std::max(0.0f, run.boss.contactCooldown - dt);
    const Rect player{playerX, playerY, kPlayerSize, kPlayerSize};
    if (run.boss.contactCooldown <= 0 && RectHitsBoss(player)) {
        DamagePlayer(bossTuning.contactDamage);
        run.boss.contactCooldown = 0.75f;
    }
    for (BossTurret& turret : run.boss.turrets) {
        if (!turret.alive) continue;
        turret.aim += turret.sweepDirection * turret.sweepSpeed * dt;
        if (turret.aim > turret.sweepMax) {
            turret.aim = turret.sweepMax;
            turret.sweepDirection = -1.0f;
        } else if (turret.aim < turret.sweepMin) {
            turret.aim = turret.sweepMin;
            turret.sweepDirection = 1.0f;
        }
        turret.fireCooldown -= dt;
        if (turret.kind == BossTurretKind::Burst) {
            if (turret.burstRemaining > 0) {
                turret.burstGap -= dt;
                if (turret.burstGap <= 0) {
                    FireBossBullet(turret, false);
                    --turret.burstRemaining;
                    turret.burstGap = bossTuning.burstInterval;
                }
            } else if (turret.fireCooldown <= 0) {
                turret.burstRemaining =
                    static_cast<float>(bossTuning.burstCount);
                turret.burstGap = 0;
                turret.fireCooldown = bossTuning.burstCooldown;
            }
        } else if (turret.fireCooldown <= 0) {
            FireBossBullet(turret, true);
            turret.fireCooldown = bossTuning.rocketCooldown;
        }
    }
    for (EnemyProjectile& shot : run.boss.projectiles) {
        if (shot.trackRemaining > 0) {
            shot.trackRemaining -= dt;
            const float cx = playerX + kPlayerSize * 0.5f;
            const float cy = playerY + kPlayerSize * 0.5f;
            float dx = cx - shot.x;
            float dy = cy - shot.y;
            const float length = std::sqrt(dx * dx + dy * dy);
            if (length > 0.01f) {
                const float speed = bossTuning.rocketSpeed;
                shot.vx = dx / length * speed;
                shot.vy = dy / length * speed;
            }
        }
        shot.x += shot.vx * dt;
        shot.y += shot.vy * dt;
        const Rect body{shot.x - 4, shot.y - 4, 8, 8};
        if (RunWall(body) || !ArenaContains(runArena, body)) {
            shot.damage = 0;
            continue;
        }
        if (Overlaps(body, player)) {
            DamagePlayer(shot.damage);
            if (shot.rocket)
                DetonateBomb(shot.x, shot.y, 2, kBombRadius);
            shot.damage = 0;
        }
    }
    run.boss.projectiles.erase(
        std::remove_if(
            run.boss.projectiles.begin(), run.boss.projectiles.end(),
            [](const EnemyProjectile& shot) {
                return shot.damage <= 0;
            }),
        run.boss.projectiles.end());
}

void InitializeBossFight(RunNode& node) {
    run.boss = BossFightState{};
    run.boss.active = true;
    run.boss.centerX = kRunArenaWidth * 0.5f;
    run.boss.centerY = kRunArenaHeight * 0.45f;
    run.boss.radiusX = bossTuning.radiusX;
    run.boss.radiusY = bossTuning.radiusY;
    run.boss.maxHealth = bossTuning.health;
    run.boss.health = bossTuning.health;
    BuildBossTurrets(node.seed, false, BossQuadrant::NorthWest, false);
    playerX = kRunArenaWidth * 0.5f - kPlayerSize * 0.5f;
    playerY = kRunArenaHeight - 120.0f;
}

}  // namespace game
