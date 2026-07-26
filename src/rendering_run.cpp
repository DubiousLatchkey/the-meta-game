#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>

#include "rendering_internal.h"
#include "roguelite.h"
#include "text_renderer.h"
#include "world.h"

namespace game {

namespace {

const char* NodeTypeName(RunNodeType type) {
    switch (type) {
        case RunNodeType::EnemyArena: return "ARENA";
        case RunNodeType::Interior: return "INTERIOR";
        case RunNodeType::PlayerInterior: return "PLAYER INTERNALS";
        case RunNodeType::BossInterior: return "BOSS INTERIOR";
        case RunNodeType::Shop: return "SHOP";
        case RunNodeType::Boss: return "BOSS";
    }
    return "NODE";
}

std::string NodeLabel(const RunNode& node) {
    std::string label;
    if (node.type == RunNodeType::BossInterior) {
        label = "INSIDE BOSS";
        return label;
    }
    if ((node.type == RunNodeType::Interior ||
         node.type == RunNodeType::EnemyArena) &&
        !node.arenaArchetype.empty()) {
        label = node.arenaArchetype;
        std::transform(
            label.begin(), label.end(), label.begin(),
            [](unsigned char value) {
                return static_cast<char>(std::toupper(value));
            });
        label = node.type == RunNodeType::Interior
            ? "INSIDE " + label : label + " ARENA";
        if (node.type == RunNodeType::EnemyArena) {
            label += " +ALL ENEMY " +
                std::string(DifficultyStatName(node.downside));
            if (node.hardArena) label = "HARD " + label;
        }
        return label;
    }
    return NodeTypeName(node.type);
}

bool IsReachableRunNode(RunNodeId id) {
    const RunNode* current = CurrentRunNode();
    return current && std::find(
        current->next.begin(), current->next.end(), id) != current->next.end();
}

void DrawHealthSprite(int x, int y, float alpha) {
    const auto found = types.find("player");
    if (found == types.end() || found->second.sprite.empty()) return;
    const auto& sprite = found->second.sprite;
    std::size_t columns = 0;
    for (const auto& row : sprite) columns = std::max(columns, row.size());
    const std::size_t units = std::max(columns, sprite.size());
    if (units == 0) return;
    const float scale = 14.0f / static_cast<float>(units);
    for (std::size_t row = 0; row < sprite.size(); ++row)
        for (std::size_t column = 0;
             column < sprite[row].size(); ++column) {
            const Pixel& pixel = sprite[row][column];
            if (!pixel.occupied) continue;
            DrawRectangleAlpha(
                static_cast<int>(x + column * scale),
                static_cast<int>(y + row * scale),
                std::max(1, static_cast<int>(std::ceil(scale))),
                std::max(1, static_cast<int>(std::ceil(scale))),
                CompositeColor(pixel), alpha);
        }
}

}  // namespace

void DrawRunMap() {
    DrawWorldRect({0, 0, kRunMapWidth, kRunMapHeight}, 0x00070B11);
    for (const RunNode& node : run.nodes) {
        const RunMapVertex* from = GetRunMapVertex(run, node.id);
        if (!from) continue;
        for (RunNodeId next : node.next) {
            const RunMapVertex* to = GetRunMapVertex(run, next);
            if (!to) continue;
            const float dx = to->x - from->x;
            const float dy = to->y - from->y;
            const float length = std::sqrt(dx * dx + dy * dy);
            if (length > 0.01f)
                DrawWorldLine(
                    from->x, from->y, dx / length, dy / length,
                    length, 4.0f, 0x00343C48, false);
        }
    }
    for (const RunMapVertex& vertex : run.mapVertices) {
        const RunNode* node = GetRunNode(run, vertex.node);
        if (!node) continue;
        const bool current = node->id == run.currentNode;
        const bool reachable = IsReachableRunNode(node->id);
        const std::uint32_t color = current ? 0x00FFFFFF :
            reachable ? 0x0048D890 : node->entered ? 0x005878B8 :
            0x00323842;
        const Rect rect = RunMapVertexRect(vertex);
        DrawWorldRect(rect, color);
        DrawWorldRect(
            {rect.x + 6, rect.y + 6, rect.width - 12, rect.height - 12},
            0x00070B11);
        if (reachable) {
            DrawWorldRect(
                {rect.x + 13, rect.y + rect.height - 13,
                 rect.width - 26, 5}, color);
            const char* assetId =
                node->type == RunNodeType::PlayerInterior
                    ? "portal_player_interior" :
                node->type == RunNodeType::BossInterior
                    ? "portal_boss_interior" :
                node->type == RunNodeType::Interior ? "portal_interior" :
                node->type == RunNodeType::Shop ? "portal_shop" :
                node->type == RunNodeType::Boss ? "portal_boss" :
                "portal_arena";
            DrawPortalEffect(
                {vertex.x - 17.5f, vertex.y - 24.5f, 35.0f, 49.0f},
                assetId);
        }
        const std::string label = NodeLabel(*node);
        DrawTextString(
            label,
            static_cast<int>(
                vertex.x - text_renderer::MeasureWidth(label.size()) * 0.5f -
                CameraX()),
            static_cast<int>(
                vertex.y + rect.height * 0.5f + 10 - CameraY()),
            0x00FFFFFF);
    }
}

void DrawPickupIcon(PickupType type, float x, float y) {
    const char* id = type == PickupType::Health
        ? "powerup_health" : type == PickupType::Multishot
        ? "powerup_multishot" : type == PickupType::Homing
        ? "powerup_homing" : "powerup_auto_rocket";
    const auto found = wallAssets.find(id);
    if (found == wallAssets.end()) return;
    for (std::size_t row = 0; row < found->second.sprite.size(); ++row)
        for (std::size_t column = 0;
             column < found->second.sprite[row].size(); ++column) {
            const Pixel& pixel = found->second.sprite[row][column];
            if (pixel.occupied)
                DrawWorldRect({x + column * 4.0f, y + row * 4.0f, 4, 4},
                    CompositeColor(pixel));
        }
}

void DrawRunArena() {
    const RunNode* node = CurrentRunNode();
    if (!node && !MainMenuActive()) return;
    const ArenaLevel& arena = ActiveRunArena();
    DrawWorldRect(arena.bounds, 0x00090D15);
    for (const AudioPanel& wall : arena.audioWalls)
        DrawAudioWaveform(wall);
    const float nearDistance =
        std::max(buffer.width, buffer.height) * 0.25f;
    for (const ArenaMotifPixel& pixel : BuildArenaMotifPixels(arena)) {
        const float dx = pixel.placementCenterX -
            (playerX + kPlayerSize * 0.5f);
        const float dy = pixel.placementCenterY -
            (playerY + kPlayerSize * 0.5f);
        if (dx * dx + dy * dy > nearDistance * nearDistance) {
            DrawArenaQuad(pixel.corners, pixel.color);
            continue;
        }
        for (int channel = 0; channel < 3; ++channel) {
            const float begin = channel / 3.0f;
            const float end = (channel + 1) / 3.0f;
            auto interpolate = [](const ArenaMotifPoint& a,
                                  const ArenaMotifPoint& b, float amount) {
                return ArenaMotifPoint{
                    a.x + (b.x - a.x) * amount,
                    a.y + (b.y - a.y) * amount};
            };
            const std::array<ArenaMotifPoint, 4> section{{
                interpolate(pixel.corners[0], pixel.corners[1], begin),
                interpolate(pixel.corners[0], pixel.corners[1], end),
                interpolate(pixel.corners[3], pixel.corners[2], end),
                interpolate(pixel.corners[3], pixel.corners[2], begin)}};
            const int value = static_cast<int>(
                (pixel.color >> (16 - channel * 8)) & 0xFF);
            DrawArenaQuad(section, ChannelColor(channel, value));
        }
    }
    if (MainMenuActive()) {
        for (const TextBox& box : textBoxes) {
            const float alpha = box.menuTitle ? MainMenuTitleAlpha() : 1.0f;
            if (alpha <= 0.0f) continue;
            const int channel = std::clamp(
                static_cast<int>(255.0f * alpha), 0, 255);
            DrawWord(
                box.word, box.rect.x, box.rect.y,
                static_cast<std::uint32_t>(
                    (channel << 16) | (channel << 8) | channel),
                true);
        }
        if (MainMenuPortalActive()) {
            const Rect portal = MainMenuPortalRect();
            DrawPortalEffect(portal, "portal_arena");
        }
        return;
    }
    if (PostBossTuningRoomActive()) {
        constexpr std::size_t columns = 3;
        constexpr float columnWidth = 780.0f;
        constexpr float rowHeight = 30.0f;
        constexpr float left = 70.0f;
        constexpr float top = 105.0f;
        const std::size_t rows =
            (worldConstants.size() + columns - 1) / columns;
        DrawTextString(
            "WORLD LUA CONSTANTS",
            static_cast<int>(left - CameraX()),
            static_cast<int>(55.0f - CameraY()), 0x00FFFFFF);
        for (std::size_t index = 0; index < worldConstants.size(); ++index) {
            const std::size_t column = rows > 0 ? index / rows : 0;
            const std::size_t row = rows > 0 ? index % rows : 0;
            const WorldConstant& entry = worldConstants[index];
            DrawTextString(
                entry.label + "  " + entry.value,
                static_cast<int>(
                    left + static_cast<float>(column) * columnWidth -
                    CameraX()),
                static_cast<int>(
                    top + static_cast<float>(row) * rowHeight -
                    CameraY()),
                0x00D8E4F0);
        }
        const Rect portal = ExitPortalRect();
        DrawPortalEffect(portal, "portal_arena");
        const std::string label = "TITLE SCREEN";
        DrawTextString(
            label,
            static_cast<int>(
                CenterX(portal) -
                text_renderer::MeasureWidth(label.size()) * 0.5f -
                CameraX()),
            static_cast<int>(
                portal.y - text_renderer::kGlyphHeight - 16 - CameraY()),
            0x00FFFFFF);
        return;
    }
    for (const RunPortal& portal : node->portals) {
        if (!portal.active || DebugRoomActive()) continue;
        const RunNode* destination = GetRunNode(run, portal.destination);
        const Rect portalRect = portal.interiorTrigger.width > 0
            ? portal.interiorTrigger : ExitPortalRect();
        const float x = portalRect.x, y = portalRect.y;
        const char* assetId = !destination ? "portal_arena" :
            destination->type == RunNodeType::BossInterior
                ? "portal_boss_interior" :
            destination->type == RunNodeType::PlayerInterior
                ? "portal_player_interior" :
            destination->type == RunNodeType::Interior ? "portal_interior" :
            destination->type == RunNodeType::Shop ? "portal_shop" :
            destination->type == RunNodeType::Boss ? "portal_boss" :
            "portal_arena";
        DrawPortalEffect({x, y, portalRect.width, portalRect.height}, assetId);
    }
    if (DebugRoomActive()) {
        const Rect portalRect = ExitPortalRect();
        DrawPortalEffect(portalRect, "portal_arena");
    }
    for (const RunPickup& pickup : node->pickups)
        if (!pickup.collected)
            DrawPickupIcon(pickup.type, pickup.x, pickup.y);
    if (node->type == RunNodeType::Boss) {
        const float bossX = run.boss.centerX;
        const float bossY = run.boss.centerY;
        for (int row = -static_cast<int>(run.boss.radiusY);
             row <= static_cast<int>(run.boss.radiusY); row += 3) {
            const float ratio =
                static_cast<float>(row) / run.boss.radiusY;
            const float halfWidth = run.boss.radiusX * std::sqrt(
                std::max(0.0f, 1.0f - ratio * ratio));
            DrawWorldRect(
                {bossX - halfWidth, bossY + row,
                 halfWidth * 2.0f, 3.0f}, 0x00D04060);
        }
        for (const BossTurret& turret : run.boss.turrets) {
            if (!turret.alive) continue;
            const float muzzleX = bossX +
                std::cos(turret.mountAngle) * run.boss.radiusX;
            const float muzzleY = bossY +
                std::sin(turret.mountAngle) * run.boss.radiusY;
            const std::uint32_t color =
                turret.kind == BossTurretKind::Rocket
                    ? 0x00FF8A30 : 0x00F0E060;
            DrawWorldRect(
                {muzzleX - 10, muzzleY - 10, 20, 20}, color);
            DrawWorldLine(
                muzzleX, muzzleY, std::cos(turret.aim),
                std::sin(turret.aim), 42.0f, 3.0f, color, false);
        }
        for (const EnemyProjectile& shot : run.boss.projectiles) {
            DrawWorldRect(
                {shot.x - (shot.rocket ? 5.0f : 3.0f),
                 shot.y - (shot.rocket ? 5.0f : 3.0f),
                 shot.rocket ? 10.0f : 6.0f,
                 shot.rocket ? 10.0f : 6.0f},
                shot.rocket ? 0x00FF8A30 : 0x00FFFFFF);
        }
        const std::string health =
            run.status == RunStatus::Won ? "BOSS DEFEATED" :
            "BOSS " + std::to_string(run.boss.health) + "/" +
                std::to_string(run.boss.maxHealth);
        DrawTextString(
            health,
            static_cast<int>(
                bossX - text_renderer::MeasureWidth(health.size()) * 0.5f -
                CameraX()),
            static_cast<int>(bossY - run.boss.radiusY - 28 - CameraY()),
            0x00FFFFFF);
        std::string cleared;
        for (int index = 0; index < 4; ++index)
            if (run.clearedBossQuadrants[index]) {
                if (!cleared.empty()) cleared += " ";
                cleared += BossQuadrantName(
                    static_cast<BossQuadrant>(index));
            }
        if (!cleared.empty())
            DrawTextString(
                "DISABLED " + cleared,
                static_cast<int>(
                    bossX - text_renderer::MeasureWidth(
                        ("DISABLED " + cleared).size()) * 0.5f -
                    CameraX()),
                static_cast<int>(
                    bossY + run.boss.radiusY + 12 - CameraY()),
                0x00A0FFC0);
    }
    if (node->type == RunNodeType::Shop) {
        for (std::size_t index = 0; index < node->shopOffers.size(); ++index) {
            const ShopOffer& offer = node->shopOffers[index];
            const Rect target = ShopOfferTarget(index);
            DrawWorldRect(target, offer.purchased ? 0x00252A30 : 0x00204460);
            const Rect purchase = ShopOfferPurchaseArea(index);
            DrawWorldRect(purchase, offer.purchased ? 0x00252A30 : 0x00502070);
            const std::string value = offer.kind == ShopOfferKind::Upgrade
                ? ShortNumber(UpgradeCurrentValue(offer.upgrade))
                : (offer.purchased ? "EQUIPPED" : "REPLACES SLOT");
            DrawTextString(
                ShopOfferName(offer),
                static_cast<int>(target.x - CameraX()),
                static_cast<int>(target.y - 66 - CameraY()), 0x00FFFFFF);
            DrawTextString(
                "COST " + std::to_string(offer.price) + " COINS",
                static_cast<int>(target.x - CameraX()),
                static_cast<int>(target.y - 40 - CameraY()), 0x00FFFFFF);
            DrawTextString(
                value,
                static_cast<int>(
                    target.x + target.width * 0.5f -
                    text_renderer::MeasureWidth(value.size()) * 0.5f -
                    CameraX()),
                static_cast<int>(target.y + 52 - CameraY()), 0x00FFFFFF);
            const float progress = std::clamp(
                offer.purchaseTimer /
                    std::max(0.01f, rogueliteTuning.shopPurchaseSeconds),
                0.0f, 1.0f);
            DrawTextString(
                offer.purchased ? "OWNED" : "STAND HERE",
                static_cast<int>(CenterX(purchase) -
                    text_renderer::MeasureWidth(
                        offer.purchased ? 5 : 10) * 0.5f - CameraX()),
                static_cast<int>(purchase.y + 32 - CameraY()), 0x00FFFFFF);
            DrawWorldRect({purchase.x + 12, purchase.y + 88,
                (purchase.width - 24) * progress, 12}, 0x00FFFFFF);
        }
        const Rect reset = ResetWordsTarget();
        DrawWorldRect(reset, 0x00204460);
        const Rect purchase = ResetWordsPurchaseArea();
        DrawWorldRect(purchase, 0x00502070);
        const std::string label = "RESET WORDS";
        DrawTextString(
            label,
            static_cast<int>(reset.x - CameraX()),
            static_cast<int>(reset.y - 66 - CameraY()), 0x00FFFFFF);
        DrawTextString(
            "COST 3 COINS", static_cast<int>(reset.x - CameraX()),
            static_cast<int>(reset.y - 40 - CameraY()), 0x00FFFFFF);
        DrawTextString(
            "RESTORE BASELINE",
            static_cast<int>(CenterX(reset) -
                text_renderer::MeasureWidth(16) * 0.5f - CameraX()),
            static_cast<int>(reset.y + 22 - CameraY()), 0x00FFFFFF);
        DrawTextString(
            "STAND HERE",
            static_cast<int>(CenterX(purchase) -
                text_renderer::MeasureWidth(10) * 0.5f - CameraX()),
            static_cast<int>(purchase.y + 20 - CameraY()), 0x00FFFFFF);
        const float progress = std::clamp(
            node->resetWordsPurchaseTimer /
                std::max(0.01f, rogueliteTuning.shopPurchaseSeconds),
            0.0f, 1.0f);
        DrawWorldRect(
            {purchase.x + 12, purchase.y + 52,
             (purchase.width - 24) * progress, 12},
            0x00FFFFFF);
    }
    if (DebugRoomActive()) {
        static constexpr std::array<UpgradeType, 8> upgrades{{
            UpgradeType::MaxHealth, UpgradeType::MoveSpeed,
            UpgradeType::FireRate, UpgradeType::ProjectileDamage,
            UpgradeType::BombCooldown, UpgradeType::BombDamage,
            UpgradeType::Invincibility, UpgradeType::ExtraProjectile}};
        for (std::size_t index = 0; index < upgrades.size(); ++index) {
            const Rect target = DebugUpgradeTarget(index);
            DrawWorldRect(target, 0x00204460);
            DrawTextString(
                UpgradeName(upgrades[index]),
                static_cast<int>(target.x - CameraX()),
                static_cast<int>(target.y - 30 - CameraY()), 0x00FFFFFF);
            DrawTextString(
                "VALUE " + ShortNumber(
                    UpgradeCurrentValue(upgrades[index])) +
                    "  STAND HERE TO BUY",
                static_cast<int>(target.x - CameraX()),
                static_cast<int>(target.y + 25 - CameraY()), 0x00FFFFFF);
        }
        for (std::size_t index = 0;
             index < playerInteriorState.permanent.size(); ++index) {
            const Rect target = DebugLimiterTarget(index);
            const bool off = playerInteriorState.permanent[index];
            DrawWorldRect(target, off ? 0x00252A30 : 0x00502070);
            DrawTextString(
                PlayerAlterationName(static_cast<PlayerAlteration>(index + 3)),
                static_cast<int>(target.x - CameraX()),
                static_cast<int>(target.y - 30 - CameraY()), 0x00FFFFFF);
            DrawTextString(
                off ? "OFF  STAND HERE TO TOGGLE" :
                    "ON  STAND HERE TO TOGGLE",
                static_cast<int>(target.x - CameraX()),
                static_cast<int>(target.y + 25 - CameraY()), 0x00FFFFFF);
        }
        static constexpr std::array<const char*, 4> enemyNames{{
            "CIRCLE", "TRIANGLE", "CHARGER", "SHOOTER"}};
        for (std::size_t index = 0; index < enemyNames.size(); ++index) {
            const Rect target = DebugSpawnerTarget(index);
            DrawWorldRect(target, 0x00204460);
            const std::string label = std::string(enemyNames[index]) +
                (DebugSpawnerEnabled(index) ? " ON - STAND HERE" :
                    " OFF - STAND HERE");
            DrawTextString(
                label, static_cast<int>(target.x - CameraX()),
                static_cast<int>(target.y + 22 - CameraY()), 0x00FFFFFF);
        }
        static constexpr std::array<PickupType, 3> pickups{{
            PickupType::Multishot, PickupType::Homing,
            PickupType::AutoRocket}};
        static constexpr std::array<const char*, 3> pickupNames{{
            "MULTISHOT", "HOMING", "AUTO ROCKET"}};
        for (std::size_t index = 0; index < pickups.size(); ++index) {
            const float x = 480.0f + static_cast<float>(index) * 260.0f;
            DrawTextString(
                pickupNames[index], static_cast<int>(x - 35 - CameraX()),
                static_cast<int>(390.0f - CameraY()), 0x00FFFFFF);
            if (DebugPickupRespawn(index) <= 0)
                DrawPickupIcon(pickups[index], x, 430.0f);
        }
        static constexpr std::array<const char*, 6> weaponNames{{
            "STANDARD SHOT", "RAILGUN", "BOOMERANG", "BOMB", "HOMING ROCKET",
            "CONTACT BOMB"}};
        for (std::size_t index = 0; index < weaponNames.size(); ++index) {
            const Rect target = DebugWeaponTarget(index);
            const bool equipped =
                (index == 0 &&
                    run.primaryWeapon == PrimaryWeapon::Standard) ||
                (index == 1 &&
                    run.primaryWeapon == PrimaryWeapon::Railgun) ||
                (index == 2 &&
                    run.primaryWeapon == PrimaryWeapon::Boomerang) ||
                (index == 3 &&
                    run.secondaryWeapon == SecondaryWeapon::Bomb) ||
                (index == 4 &&
                    run.secondaryWeapon == SecondaryWeapon::HomingRocket) ||
                (index == 5 &&
                    run.secondaryWeapon == SecondaryWeapon::ContactBomb);
            DrawWorldRect(target, equipped ? 0x00406020 : 0x00204460);
            const std::string label =
                std::string(weaponNames[index]) +
                (equipped ? " EQUIPPED" : " STAND HERE TO EQUIP");
            DrawTextString(
                label, static_cast<int>(target.x - CameraX()),
                static_cast<int>(target.y + 22 - CameraY()), 0x00FFFFFF);
        }
    }
}

void DrawInteriorMinimap(const RunNode& node) {
    const bool interiorNode = node.type == RunNodeType::Interior ||
        node.type == RunNodeType::PlayerInterior ||
        node.type == RunNodeType::BossInterior;
    if (!interiorNode) return;

    int minRow = 0, maxRow = 0, minColumn = 0, maxColumn = 0;
    bool haveRoom = false;
    for (const Room& room : rooms) {
        if (!haveRoom) {
            minRow = maxRow = room.row;
            minColumn = maxColumn = room.column;
            haveRoom = true;
        } else {
            minRow = std::min(minRow, room.row);
            maxRow = std::max(maxRow, room.row);
            minColumn = std::min(minColumn, room.column);
            maxColumn = std::max(maxColumn, room.column);
        }
    }
    if (!haveRoom) return;
    if (node.type == RunNodeType::Interior) {
        const auto found = types.find(node.arenaArchetype);
        if (found != types.end() && !found->second.sprite.empty()) {
            std::size_t spriteColumns = 0;
            for (const auto& row : found->second.sprite)
                spriteColumns = std::max(spriteColumns, row.size());
            const Room& anchor = rooms.front();
            minRow = anchor.row - anchor.pixelRow;
            minColumn = anchor.column - anchor.pixelColumn;
            maxRow = minRow +
                static_cast<int>(found->second.sprite.size()) - 1;
            maxColumn = minColumn +
                static_cast<int>(spriteColumns) - 1;
        }
    }

    const int rows = maxRow - minRow + 1;
    const int columns = maxColumn - minColumn + 1;
    const int cell = std::max(3, std::min(12,
        std::min(180 / rows, 220 / columns)));
    const int width = columns * cell + 8;
    const int height = rows * cell + 8;
    const int left = 12;
    const int top = buffer.height - height - 12;
    DrawRectangle(left - 2, top - 2, width + 4, height + 4, 0x00070B11);
    DrawRectangle(left, top, width, height, 0x00252A30);
    for (int row = minRow; row <= maxRow; ++row)
        for (int column = minColumn; column <= maxColumn; ++column)
            DrawRectangle(
                left + 4 + (column - minColumn) * cell,
                top + 4 + (row - minRow) * cell,
                cell - 1, cell - 1, 0x00242C34);

    const int playerRoom = CurrentRoom();
    for (int index = 0; index < static_cast<int>(rooms.size()); ++index) {
        const Room& room = rooms[index];
        const int x = left + 4 + (room.column - minColumn) * cell;
        const int y = top + 4 + (room.row - minRow) * cell;
        const bool isPlayer = index == playerRoom;
        DrawRectangle(x, y, cell - 1, cell - 1,
            isPlayer ? 0x00FFFFFF :
            room.distance >= 0 ? 0x0060C0D0 : 0x00141A20);
    }
}

void DrawRunHud() {
    const RunNode* current = CurrentRunNode();
    if (!current) return;
    DrawRectangle(0, 0, buffer.width, 98, 0x00070B11);
    const float regenerationProgress =
        playerInteriorState.permanent[0] &&
        playerHealth < EffectivePlayerMaxHealth()
        ? std::clamp(
            run.regenerationTimer / std::max(
                0.1f, playerInteriorState.values[3] / 10.0f),
            0.0f, 1.0f)
        : 0.0f;
    const std::string healthLabel = "HEALTH";
    DrawTextString(healthLabel, 12, 10, 0x00FFFFFF);
    const int healthX = 24 + text_renderer::MeasureWidth(healthLabel.size());
    const int healthY =
        10 + (text_renderer::kGlyphHeight - kPlayerRenderSize) / 2;
    for (int index = 0; index < EffectivePlayerMaxHealth(); ++index) {
        float alpha = index < playerHealth ? 1.0f : 0.15f;
        if (index == playerHealth && regenerationProgress > 0.0f)
            alpha = 0.15f + regenerationProgress * 0.85f;
        DrawHealthSprite(healthX + index * 22, healthY, alpha);
    }
    const std::string weapons =
        std::string(PrimaryWeaponName(run.primaryWeapon)) + " / " +
        SecondaryWeaponName(run.secondaryWeapon);
    DrawTextString(weapons, 12, 42, 0x00FFFFFF);
    const float bombDuration = std::max(
        0.01f, SecondaryCooldownDuration());
    const float bombReady =
        1.0f - std::clamp(bombCooldown / bombDuration, 0.0f, 1.0f);
    const int cooldownX =
        24 + text_renderer::MeasureWidth(weapons.size());
    DrawRectangle(cooldownX, 47, 120, 10, 0x00252A30);
    DrawRectangle(
        cooldownX, 47, static_cast<int>(120 * bombReady), 10, 0x00FFFFFF);
    if (run.primaryWeapon == PrimaryWeapon::Railgun &&
        run.primaryCharge > 0) {
        DrawRectangle(cooldownX + 132, 47, 120, 10, 0x00252A30);
        DrawRectangle(
            cooldownX + 132, 47,
            static_cast<int>(120 * PrimaryChargeProgress()),
            10, 0x00FFFFFF);
    }

    const std::string coins = "COINS " + std::to_string(run.currency);
    const std::string depth =
        "DEPTH LEVEL " + std::to_string(current->depth);
    DrawTextString(
        coins,
        buffer.width - 12 - text_renderer::MeasureWidth(coins.size()),
        10, 0x00FFFFFF);
    DrawTextString(
        depth,
        buffer.width - 12 - text_renderer::MeasureWidth(depth.size()),
        42, 0x00FFFFFF);
    if (current->type == RunNodeType::EnemyArena &&
        !current->waves.empty()) {
        const std::uint32_t shownRound = std::min<std::uint32_t>(
            current->activeWave + 1,
            static_cast<std::uint32_t>(current->waves.size()));
        const std::string round = "ROUND " +
            std::to_string(shownRound) + " / " +
            std::to_string(current->waves.size());
        DrawTextString(
            round,
            buffer.width - 12 -
                text_renderer::MeasureWidth(round.size()),
            70, 0x00FFFFFF);
    }

    std::vector<std::string> active;
    if (run.multishotRemaining > 0)
        active.push_back(
            "MULTISHOT " + ShortNumber(run.multishotRemaining));
    if (run.homingRemaining > 0)
        active.push_back("HOMING " + ShortNumber(run.homingRemaining));
    if (run.autoRocketRemaining > 0)
        active.push_back(
            "AUTO ROCKET " + ShortNumber(run.autoRocketRemaining));
    std::string powerups;
    for (const std::string& item : active) {
        if (!powerups.empty()) powerups += "  ";
        powerups += item;
    }
    if (!powerups.empty())
        DrawTextString(
            powerups,
            (buffer.width -
                text_renderer::MeasureWidth(powerups.size())) / 2,
            10, 0x00FFFFFF);
    if (run.extraLifeAvailable) {
        const std::string extraLife = "EXTRA LIFE READY";
        DrawTextString(
            extraLife,
            (buffer.width - text_renderer::MeasureWidth(extraLife.size())) / 2,
            42, 0x00A0FFC0);
    }
    if (current->type == RunNodeType::Boss &&
        run.status == RunStatus::Won)
        DrawTextString(
            "YOU WON",
            (buffer.width - text_renderer::MeasureWidth(7)) / 2,
            64, 0x00FFFFFF);
    DrawInteriorMinimap(*current);
}

}  // namespace game
