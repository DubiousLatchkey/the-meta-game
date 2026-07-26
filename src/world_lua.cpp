#include "world.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <fstream>
#include <system_error>

extern "C" {
#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"
}

#include "text_renderer.h"
#include "world_internal.h"

namespace game {

namespace {

std::vector<std::vector<std::uint8_t>> pristineWordBytes;

std::string LuaError(lua_State* lua) {
    const char* text = lua_tostring(lua, -1);
    return text ? text : "unknown Lua error";
}

int IntegerField(lua_State* lua, int table, const char* name, int fallback) {
    table = lua_absindex(lua, table);
    lua_getfield(lua, table, name);
    const int result = lua_isinteger(lua, -1)
        ? static_cast<int>(lua_tointeger(lua, -1)) : fallback;
    lua_pop(lua, 1);
    return result;
}

float NumberField(lua_State* lua, int table, const char* name, float fallback) {
    table = lua_absindex(lua, table);
    lua_getfield(lua, table, name);
    const float result = lua_isnumber(lua, -1)
        ? static_cast<float>(lua_tonumber(lua, -1)) : fallback;
    lua_pop(lua, 1);
    return result;
}

bool HasNumberField(lua_State* lua, int table, const char* name) {
    table = lua_absindex(lua, table);
    lua_getfield(lua, table, name);
    const bool result = lua_isnumber(lua, -1);
    lua_pop(lua, 1);
    return result;
}

bool BooleanField(lua_State* lua, int table, const char* name, bool fallback) {
    table = lua_absindex(lua, table);
    lua_getfield(lua, table, name);
    const bool result = lua_isboolean(lua, -1)
        ? lua_toboolean(lua, -1) != 0 : fallback;
    lua_pop(lua, 1);
    return result;
}

std::string StringField(lua_State* lua, int table, const char* name) {
    table = lua_absindex(lua, table);
    lua_getfield(lua, table, name);
    const char* value = lua_tostring(lua, -1);
    std::string result = value ? value : "";
    lua_pop(lua, 1);
    return result;
}

std::vector<int> ReadWordList(
    lua_State* lua, int table, const std::map<const void*, int>& pointers) {
    std::vector<int> result;
    table = lua_absindex(lua, table);
    const int count = static_cast<int>(lua_rawlen(lua, table));
    for (int i = 1; i <= count; ++i) {
        lua_rawgeti(lua, table, i);
        const auto found = pointers.find(lua_topointer(lua, -1));
        if (found != pointers.end()) result.push_back(found->second);
        lua_pop(lua, 1);
    }
    return result;
}

std::vector<std::vector<Pixel>> ReadSprite(lua_State* lua, int table) {
    std::vector<std::vector<Pixel>> sprite;
    table = lua_absindex(lua, table);
    const int rowCount = static_cast<int>(lua_rawlen(lua, table));
    for (int row = 1; row <= rowCount; ++row) {
        lua_rawgeti(lua, table, row);
        std::vector<Pixel> rowPixels;
        if (lua_istable(lua, -1)) {
            const int columnCount = static_cast<int>(lua_rawlen(lua, -1));
            for (int column = 1; column <= columnCount; ++column) {
                lua_rawgeti(lua, -1, column);
                Pixel pixel;
                pixel.occupied = lua_istable(lua, -1);
                if (pixel.occupied)
                    for (int channel = 0; channel < 3; ++channel) {
                        lua_rawgeti(lua, -1, channel + 1);
                        if (lua_isinteger(lua, -1))
                            pixel.rgb[channel] = std::clamp(
                                static_cast<int>(lua_tointeger(lua, -1)),
                                0, 255);
                        lua_pop(lua, 1);
                    }
                rowPixels.push_back(pixel);
                lua_pop(lua, 1);
            }
        }
        sprite.push_back(std::move(rowPixels));
        lua_pop(lua, 1);
    }
    return sprite;
}

bool LoadWorldScript(const std::filesystem::path& path) {
    lua_State* lua = luaL_newstate();
    if (!lua) return false;
    luaL_requiref(lua, "_G", luaopen_base, 1);
    lua_pop(lua, 1);
    lua_pushnil(lua); lua_setglobal(lua, "dofile");
    lua_pushnil(lua); lua_setglobal(lua, "loadfile");
    const std::string filename = path.string();
    if (luaL_loadfile(lua, filename.c_str()) != LUA_OK ||
        lua_pcall(lua, 0, 1, 0) != LUA_OK || !lua_istable(lua, -1)) {
        LuaError(lua);
        lua_close(lua);
        return false;
    }

    const int root = lua_absindex(lua, -1);
    std::vector<Word> loadedWords;
    std::map<std::string, int> loadedWordIds;
    std::map<const void*, int> wordPointers;
    lua_getfield(lua, root, "words");
    if (!lua_istable(lua, -1)) { lua_close(lua); return false; }
    lua_pushnil(lua);
    while (lua_next(lua, -2) != 0) {
        if (lua_type(lua, -2) == LUA_TSTRING && lua_istable(lua, -1)) {
            Word word;
            word.id = lua_tostring(lua, -2);
            const std::string value = StringField(lua, -1, "text");
            word.bytes.assign(value.begin(), value.end());
            const int index = static_cast<int>(loadedWords.size());
            loadedWordIds[word.id] = index;
            wordPointers[lua_topointer(lua, -1)] = index;
            loadedWords.push_back(std::move(word));
        }
        lua_pop(lua, 1);
    }
    lua_pop(lua, 1);
    if (loadedWords.empty()) { lua_close(lua); return false; }

    std::map<std::string, std::vector<int>> loadedPhrases;
    lua_getfield(lua, root, "text");
    if (lua_istable(lua, -1)) {
        lua_pushnil(lua);
        while (lua_next(lua, -2) != 0) {
            if (lua_type(lua, -2) == LUA_TSTRING && lua_istable(lua, -1))
                loadedPhrases[lua_tostring(lua, -2)] =
                    ReadWordList(lua, -1, wordPointers);
            lua_pop(lua, 1);
        }
    }
    lua_pop(lua, 1);

    text_renderer::GlyphAtlas loadedGlyphs =
        text_renderer::FallbackGlyphAtlas();
    int loadedGlyphCount = 0;
    lua_getfield(lua, root, "glyphs");
    if (!lua_istable(lua, -1)) { lua_close(lua); return false; }
    lua_pushnil(lua);
    while (lua_next(lua, -2) != 0) {
        int character = -1;
        if (lua_isinteger(lua, -2))
            character = static_cast<int>(lua_tointeger(lua, -2));
        else if (lua_type(lua, -2) == LUA_TSTRING) {
            std::size_t length = 0;
            const char* key = lua_tolstring(lua, -2, &length);
            if (key && length == 1)
                character = static_cast<unsigned char>(key[0]);
        }
        if (character >= 0 && character <= 255 &&
            lua_istable(lua, -1) && lua_rawlen(lua, -1) == 7) {
            text_renderer::Glyph glyph{};
            for (int row = 0; row < 7; ++row) {
                lua_rawgeti(lua, -1, row + 1);
                const int bits = lua_isinteger(lua, -1)
                    ? std::clamp(
                          static_cast<int>(lua_tointeger(lua, -1)),
                          0, 0x1F)
                    : 0;
                lua_pop(lua, 1);
                for (int column = 0; column < 5; ++column)
                    if ((bits & (1 << (4 - column))) != 0) {
                        glyph[row][column].occupied = true;
                        glyph[row][column].rgb = {255, 255, 255};
                    }
            }
            loadedGlyphs[character] = glyph;
            ++loadedGlyphCount;
        }
        lua_pop(lua, 1);
    }
    lua_pop(lua, 1);

    int loadedLevelNumber = 10;
    int loadedLevelValueWord = -1;
    std::vector<int> loadedLevelLabel;
    std::map<int, std::string> loadedLevelMaps;
    std::map<int, LevelRegion> loadedLevelRegions;
    lua_getfield(lua, root, "level");
    if (lua_istable(lua, -1)) {
        loadedLevelNumber =
            std::max(0, IntegerField(lua, -1, "value", 10));
        lua_getfield(lua, -1, "label");
        if (lua_istable(lua, -1))
            loadedLevelLabel =
                ReadWordList(lua, -1, wordPointers);
        lua_pop(lua, 1);
        lua_getfield(lua, -1, "value_text");
        if (lua_istable(lua, -1)) {
            const std::vector<int> valueText =
                ReadWordList(lua, -1, wordPointers);
            if (!valueText.empty())
                loadedLevelValueWord = valueText.front();
        }
        lua_pop(lua, 1);
    }
    lua_pop(lua, 1);
    lua_getfield(lua, root, "levels");
    if (lua_istable(lua, -1)) {
        lua_pushnil(lua);
        while (lua_next(lua, -2) != 0) {
            if (lua_isinteger(lua, -2) && lua_istable(lua, -1)) {
                LevelRegion region;
                region.number =
                    static_cast<int>(lua_tointeger(lua, -2));
                region.map = StringField(lua, -1, "map");
                region.x = NumberField(lua, -1, "x", 0);
                region.y = NumberField(lua, -1, "y", 0);
                region.width = std::max(
                    100.0f, NumberField(lua, -1, "width", 1000));
                region.height = std::max(
                    100.0f, NumberField(lua, -1, "height", 720));
                region.spawnX =
                    NumberField(lua, -1, "spawn_x", 100);
                region.spawnY =
                    NumberField(lua, -1, "spawn_y", 100);
                loadedLevelMaps[region.number] = region.map;
                loadedLevelRegions[region.number] = std::move(region);
            }
            lua_pop(lua, 1);
        }
    }
    lua_pop(lua, 1);

    std::map<std::string, EnemyType> loadedTypes;
    lua_getfield(lua, root, "enemies");
    if (!lua_istable(lua, -1)) { lua_close(lua); return false; }
    lua_pushnil(lua);
    while (lua_next(lua, -2) != 0) {
        if (lua_type(lua, -2) == LUA_TSTRING && lua_istable(lua, -1)) {
            EnemyType type;
            type.id = lua_tostring(lua, -2);
            type.maxHealth =
                std::max(1, IntegerField(lua, -1, "health", 1));
            type.speed =
                std::max(0.0f, NumberField(lua, -1, "speed", 1.0f));
            type.contactDamage =
                std::max(0, IntegerField(lua, -1, "contact_damage", 1));
            type.pixelScale =
                std::clamp(IntegerField(lua, -1, "pixel_scale", 7), 2, 20);
            type.burstMin =
                std::clamp(IntegerField(lua, -1, "burst_min", 1), 1, 10);
            type.burstMax = std::clamp(
                IntegerField(lua, -1, "burst_max", type.burstMin),
                type.burstMin, 12);
            type.spawnWeight =
                std::clamp(IntegerField(lua, -1, "spawn_weight", 0), 0, 100);
            type.preferredDistance = std::max(
                0.0f, NumberField(lua, -1, "preferred_distance", 0));
            type.attackRange = std::max(
                0.0f, NumberField(lua, -1, "attack_range", 0));
            type.windupSeconds = std::max(
                0.0f, NumberField(lua, -1, "windup_seconds", 0));
            type.aimLockSeconds = std::max(
                0.0f, NumberField(lua, -1, "aim_lock_seconds", 0));
            type.attackCooldown = std::max(
                0.0f, NumberField(lua, -1, "attack_cooldown", 0));
            type.attackDistance = std::max(
                0.0f, NumberField(lua, -1, "attack_distance", 0));
            type.attackSpeed = std::max(
                0.0f, NumberField(lua, -1, "attack_speed", 0));
            lua_getfield(lua, -1, "sprite");
            if (lua_istable(lua, -1))
                type.sprite = ReadSprite(lua, -1);
            lua_pop(lua, 1);
            if (!type.sprite.empty())
                loadedTypes[type.id] = std::move(type);
        }
        lua_pop(lua, 1);
    }
    lua_pop(lua, 1);

    std::map<std::string, WallAsset> loadedWallAssets;
    lua_getfield(lua, root, "wall_assets");
    if (lua_istable(lua, -1)) {
        lua_pushnil(lua);
        while (lua_next(lua, -2) != 0) {
            if (lua_type(lua, -2) == LUA_TSTRING && lua_istable(lua, -1)) {
                WallAsset asset;
                asset.id = lua_tostring(lua, -2);
                asset.sprite = ReadSprite(lua, -1);
                if (!asset.sprite.empty())
                    loadedWallAssets[asset.id] = std::move(asset);
            }
            lua_pop(lua, 1);
        }
    }
    lua_pop(lua, 1);

    std::map<std::string, WeaponStats> loadedPlayerWeapons;
    lua_getfield(lua, root, "player_weapons");
    if (lua_istable(lua, -1)) {
        lua_pushnil(lua);
        while (lua_next(lua, -2) != 0) {
            if (lua_type(lua, -2) == LUA_TSTRING && lua_istable(lua, -1)) {
                WeaponStats weapon;
                weapon.cadence = std::clamp(
                    NumberField(lua, -1, "cadence", weapon.cadence),
                    0.03f, 10.0f);
                weapon.cadenceEffectScale = std::clamp(
                    NumberField(
                        lua, -1, "cadence_effect_scale",
                        weapon.cadenceEffectScale),
                    0.05f, 20.0f);
                weapon.decelerationScale = std::clamp(
                    NumberField(
                        lua, -1, "deceleration_scale",
                        weapon.decelerationScale),
                    0.05f, 10.0f);
                weapon.speed = std::clamp(
                    NumberField(lua, -1, "speed", weapon.speed),
                    1.0f, 3000.0f);
                weapon.range = std::clamp(
                    NumberField(lua, -1, "range", weapon.range),
                    10.0f, 5000.0f);
                weapon.spread = std::clamp(
                    NumberField(lua, -1, "spread", weapon.spread),
                    0.0f, 1.0f);
                weapon.width = std::clamp(
                    NumberField(lua, -1, "width", weapon.width),
                    1.0f, 100.0f);
                weapon.radius = std::clamp(
                    NumberField(lua, -1, "radius", weapon.radius),
                    0.0f, 500.0f);
                weapon.count = std::clamp(
                    IntegerField(lua, -1, "count", weapon.count), 1, 32);
                weapon.projectilesPerShot = std::clamp(
                    IntegerField(
                        lua, -1, "projectiles_per_shot",
                        weapon.projectilesPerShot),
                    1, 32);
                weapon.damage = std::clamp(
                    IntegerField(lua, -1, "damage", weapon.damage), 1, 100);
                weapon.homing =
                    BooleanField(lua, -1, "homing", weapon.homing);
                weapon.explosive =
                    BooleanField(lua, -1, "explosive", weapon.explosive);
                weapon.contact =
                    BooleanField(lua, -1, "contact", weapon.contact);
                weapon.boomerang =
                    BooleanField(lua, -1, "boomerang", weapon.boomerang);
                loadedPlayerWeapons[lua_tostring(lua, -2)] = weapon;
            }
            lua_pop(lua, 1);
        }
    }
    lua_pop(lua, 1);

    std::array<int, 9> loadedPlayerInteriorDefaults{
        {5, 5, 100, 51, 3, 1, 1001, 1, 1}};
    float loadedMoveImprovementPerStep = 0.10f;
    float loadedFireImprovementPerStep = 0.10f;
    bool loadedPlayerInteriorScaling = false;
    static constexpr std::array<const char*, 9> playerInternalFields{{
        "move_delay_percent", "fire_delay_ms", "projectile_deficit",
        "regeneration_deciseconds", "multishot_deficit", "homing_error",
        "auto_rocket_delay_ms", "primary_deficit", "secondary_deficit"}};
    lua_getfield(lua, root, "player_internals");
    if (lua_istable(lua, -1)) {
        for (std::size_t index = 0;
             index < playerInternalFields.size(); ++index)
            loadedPlayerInteriorDefaults[index] = std::clamp(
                IntegerField(
                    lua, -1, playerInternalFields[index],
                    loadedPlayerInteriorDefaults[index]),
                index < 2 ? -100000 : 0, 100000);
        loadedMoveImprovementPerStep = std::clamp(
            NumberField(
                lua, -1, "move_delay_improvement_percent", 10.0f) / 100.0f,
            0.01f, 1.0f);
        loadedFireImprovementPerStep = std::clamp(
            NumberField(
                lua, -1, "fire_delay_improvement_percent", 10.0f) / 100.0f,
            0.01f, 1.0f);
        loadedPlayerInteriorScaling =
            HasNumberField(
                lua, -1, "move_delay_improvement_percent") &&
            HasNumberField(
                lua, -1, "fire_delay_improvement_percent");
    }
    lua_pop(lua, 1);

    Interior loadedInterior;
    std::vector<Organ> loadedOrgans;
    lua_getfield(lua, root, "interior");
    if (!lua_istable(lua, -1)) { lua_close(lua); return false; }
    loadedInterior.archetype = StringField(lua, -1, "archetype");
    loadedInterior.enemy = StringField(lua, -1, "enemy");
    loadedInterior.alternateEnemy =
        StringField(lua, -1, "alternate_enemy");
    loadedInterior.roomSize =
        std::clamp(IntegerField(lua, -1, "room_size", 640), 400, 1000);
    loadedInterior.seed = IntegerField(lua, -1, "seed", 7331);
    loadedInterior.spawnersMin =
        std::clamp(IntegerField(lua, -1, "spawners_min", 1), 1, 4);
    loadedInterior.spawnersMax = std::clamp(
        IntegerField(lua, -1, "spawners_max", 4),
        loadedInterior.spawnersMin, 4);
    loadedInterior.burstMin =
        std::clamp(IntegerField(lua, -1, "burst_min", 4), 1, 10);
    loadedInterior.burstMax = std::clamp(
        IntegerField(lua, -1, "burst_max", 6),
        loadedInterior.burstMin, 12);
    loadedInterior.alternateBurstMin = std::clamp(
        IntegerField(lua, -1, "alternate_burst_min", 4), 1, 10);
    loadedInterior.alternateBurstMax = std::clamp(
        IntegerField(lua, -1, "alternate_burst_max", 6),
        loadedInterior.alternateBurstMin, 12);
    loadedInterior.secondsMin =
        std::max(0.5f, NumberField(lua, -1, "spawn_seconds_min", 3));
    loadedInterior.secondsMax = std::max(
        loadedInterior.secondsMin,
        NumberField(lua, -1, "spawn_seconds_max", 6));
    loadedInterior.speedUnit =
        std::max(0.0f, NumberField(lua, -1, "speed_unit", 4.0f));
    lua_getfield(lua, -1, "organs");
    const std::array<const char*, 6> organNames{
        {"size", "speed", "health", "burst", "damage", "spawner_health"}};
    if (lua_istable(lua, -1)) {
        for (const char* name : organNames) {
            lua_getfield(lua, -1, name);
            if (lua_istable(lua, -1)) {
                Organ organ;
                organ.id = name;
                organ.value =
                    std::max(0, IntegerField(lua, -1, "value", 1));
                organ.maximum = std::max(
                    1, IntegerField(lua, -1, "maximum", organ.value));
                organ.decrement = std::max(
                    1, IntegerField(lua, -1, "decrement", 1));
                lua_getfield(lua, -1, "label");
                if (lua_istable(lua, -1))
                    organ.label = ReadWordList(lua, -1, wordPointers);
                lua_pop(lua, 1);
                lua_getfield(lua, -1, "value_text");
                if (lua_istable(lua, -1)) {
                    const std::vector<int> valueText =
                        ReadWordList(lua, -1, wordPointers);
                    if (!valueText.empty()) organ.valueWord = valueText.front();
                }
                lua_pop(lua, 1);
                loadedOrgans.push_back(std::move(organ));
            }
            lua_pop(lua, 1);
        }
    }
    lua_pop(lua, 2);
    RogueliteTuning loadedRogueliteTuning;
    lua_getfield(lua, root, "roguelite");
    if (lua_istable(lua, -1)) {
        loadedRogueliteTuning.runDepths = static_cast<std::uint32_t>(
            std::clamp(IntegerField(lua, -1, "depths", 10), 3, 20));
        loadedRogueliteTuning.branchNodesMin =
            static_cast<std::uint32_t>(std::clamp(
                IntegerField(lua, -1, "branch_nodes_min", 3), 2, 5));
        loadedRogueliteTuning.branchNodesMax =
            static_cast<std::uint32_t>(std::clamp(
                IntegerField(
                    lua, -1, "branch_nodes_max",
                    static_cast<int>(loadedRogueliteTuning.branchNodesMin)),
                static_cast<int>(loadedRogueliteTuning.branchNodesMin), 5));
        loadedRogueliteTuning.extraBranchEdges =
            static_cast<std::uint32_t>(std::clamp(
                IntegerField(lua, -1, "extra_branch_edges", 2), 0, 2));
        loadedRogueliteTuning.fullAudioWallChancePercent =
            static_cast<std::uint32_t>(std::clamp(
                IntegerField(
                    lua, -1, "full_audio_wall_chance_percent", 50),
                0, 100));
        loadedRogueliteTuning.healthPickupDropChance =
            static_cast<std::uint32_t>(std::clamp(
                IntegerField(lua, -1, "health_pickup_drop_chance", 10),
                1, 1000));
        loadedRogueliteTuning.powerupDropChance =
            static_cast<std::uint32_t>(std::clamp(
                IntegerField(lua, -1, "powerup_drop_chance", 20),
                1, 1000));
        loadedRogueliteTuning.shopPrice = static_cast<std::uint32_t>(
            std::clamp(IntegerField(lua, -1, "shop_price", 5), 1, 99));
        loadedRogueliteTuning.shopPurchaseSeconds = std::clamp(
            NumberField(lua, -1, "shop_purchase_seconds", 1.0f),
            0.1f, 10.0f);
        loadedRogueliteTuning.arenaWaves = static_cast<std::uint32_t>(
            std::clamp(IntegerField(lua, -1, "arena_waves", 3), 1, 8));
        loadedRogueliteTuning.interiorWaves = static_cast<std::uint32_t>(
            std::clamp(IntegerField(lua, -1, "interior_waves", 2), 1, 8));
        loadedRogueliteTuning.waveCooldown = std::clamp(
            NumberField(lua, -1, "wave_cooldown", 1.5f), 0.1f, 10.0f);
        loadedRogueliteTuning.powerupSeconds = std::clamp(
            NumberField(lua, -1, "powerup_seconds", 10.0f), 1.0f, 60.0f);
        loadedRogueliteTuning.portalWidth = std::clamp(
            NumberField(lua, -1, "portal_width", 110.0f), 40.0f, 240.0f);
        loadedRogueliteTuning.playerInvincibilitySeconds = std::clamp(
            NumberField(
                lua, -1, "player_invincibility_seconds", 0.5f),
            0.05f, 5.0f);
        loadedRogueliteTuning.playerInteriorRoomSize =
            static_cast<std::uint32_t>(std::clamp(
                IntegerField(lua, -1, "player_interior_room_size", 1500),
                960, 2400));
        loadedRogueliteTuning.playerInteriorWaveSpawners =
            static_cast<std::uint32_t>(std::clamp(
                IntegerField(lua, -1, "player_interior_wave_spawners", 8),
                4, 20));
    }
    lua_pop(lua, 1);
    BossTuning loadedBossTuning;
    lua_getfield(lua, root, "boss");
    if (lua_istable(lua, -1)) {
        loadedBossTuning.health = std::clamp(
            IntegerField(lua, -1, "health", 80), 10, 500);
        loadedBossTuning.radiusX = std::clamp(
            NumberField(lua, -1, "radius_x", 280.0f), 80.0f, 600.0f);
        loadedBossTuning.radiusY = std::clamp(
            NumberField(lua, -1, "radius_y", 140.0f), 40.0f, 400.0f);
        loadedBossTuning.burstTurretsPerQuadrant = std::clamp(
            IntegerField(lua, -1, "burst_turrets_per_quadrant", 2), 1, 4);
        loadedBossTuning.rocketTurretsPerQuadrant = std::clamp(
            IntegerField(lua, -1, "rocket_turrets_per_quadrant", 1), 0, 3);
        loadedBossTuning.sweepDegrees = std::clamp(
            NumberField(lua, -1, "sweep_degrees", 70.0f), 20.0f, 160.0f);
        loadedBossTuning.sweepSpeed = std::clamp(
            NumberField(lua, -1, "sweep_speed", 1.2f), 0.2f, 4.0f);
        loadedBossTuning.burstCooldown = std::clamp(
            NumberField(lua, -1, "burst_cooldown", 1.8f), 0.4f, 8.0f);
        loadedBossTuning.burstCount = std::clamp(
            IntegerField(lua, -1, "burst_count", 4), 1, 12);
        loadedBossTuning.burstInterval = std::clamp(
            NumberField(lua, -1, "burst_interval", 0.12f), 0.04f, 1.0f);
        loadedBossTuning.bulletSpeed = std::clamp(
            NumberField(lua, -1, "bullet_speed", 280.0f), 80.0f, 800.0f);
        loadedBossTuning.rocketSpeed = std::clamp(
            NumberField(lua, -1, "rocket_speed", 220.0f), 60.0f, 600.0f);
        loadedBossTuning.rocketTrackSeconds = std::clamp(
            NumberField(lua, -1, "rocket_track_seconds", 1.8f), 0.2f, 6.0f);
        loadedBossTuning.rocketCooldown = std::clamp(
            NumberField(lua, -1, "rocket_cooldown", 3.2f), 0.5f, 10.0f);
        loadedBossTuning.turretTargetHealth = std::clamp(
            IntegerField(lua, -1, "turret_target_health", 8), 1, 40);
        loadedBossTuning.contactDamage = std::clamp(
            IntegerField(lua, -1, "contact_damage", 1), 1, 5);
        loadedBossTuning.interiorRoomSize = std::clamp(
            IntegerField(lua, -1, "interior_room_size", 720), 480, 1200);
    }
    lua_pop(lua, 1);
    lua_close(lua);

    if (!loadedTypes.count(loadedInterior.archetype) ||
        !loadedTypes.count(loadedInterior.enemy) ||
        !loadedTypes.count(loadedInterior.alternateEnemy) ||
        !loadedTypes.count("player") ||
        !loadedTypes.count("triangle") ||
        !loadedTypes.count("charger") ||
        !loadedTypes.count("shooter") ||
        !loadedTypes.count("boss") ||
        !loadedPlayerWeapons.count("standard") ||
        !loadedPlayerWeapons.count("railgun") ||
        !loadedPlayerWeapons.count("bomb") ||
        !loadedPlayerWeapons.count("contact_bomb") ||
        !loadedPlayerWeapons.count("homing_rocket") ||
        !loadedPlayerWeapons.count("boomerang") ||
        !loadedPlayerWeapons.count("auto_rocket") ||
        !loadedPhrases.count("main_title") ||
        !loadedPhrases.count("start_game") ||
        !loadedPhrases.count("spawn_herd_modifier") ||
        !loadedPlayerInteriorScaling ||
        loadedOrgans.size() != 6 || loadedGlyphCount < 94 ||
        loadedLevelValueWord < 0 || loadedLevelMaps.empty())
        return false;
    words = std::move(loadedWords);
    wordIds = std::move(loadedWordIds);
    phrases = std::move(loadedPhrases);
    types = std::move(loadedTypes);
    wallAssets = std::move(loadedWallAssets);
    playerWeapons = std::move(loadedPlayerWeapons);
    playerInteriorDefaults = loadedPlayerInteriorDefaults;
    playerMoveImprovementPerStep = loadedMoveImprovementPerStep;
    playerFireImprovementPerStep = loadedFireImprovementPerStep;
    interior = std::move(loadedInterior);
    organs = std::move(loadedOrgans);
    rogueliteTuning = loadedRogueliteTuning;
    bossTuning = loadedBossTuning;
    levelNumber = loadedLevelNumber;
    levelValueWord = loadedLevelValueWord;
    levelLabel = std::move(loadedLevelLabel);
    levelMaps = std::move(loadedLevelMaps);
    levelRegions = std::move(loadedLevelRegions);
    text_renderer::SetGlyphAtlas(loadedGlyphs);
    const std::string levelText = std::to_string(levelNumber);
    words[levelValueWord].bytes.assign(
        levelText.begin(), levelText.end());
    pristineWordBytes.clear();
    pristineWordBytes.reserve(words.size());
    for (const Word& word : words) pristineWordBytes.push_back(word.bytes);
    const auto selectedMap = levelMaps.find(levelNumber);
    currentMap = selectedMap != levelMaps.end()
        ? selectedMap->second : "placeholder";
    return true;
}

bool CopyGoldenWorld(bool overwrite) {
    std::error_code error;
    std::filesystem::create_directories(gameDirectory, error);
    const auto source = goldenDirectory / "world.lua";
    const auto destination = gameDirectory / "world.lua";
    if (!std::filesystem::exists(source)) return false;
    if (overwrite || !std::filesystem::exists(destination))
        std::filesystem::copy_file(
            source, destination,
            std::filesystem::copy_options::overwrite_existing, error);
    return !error;
}

std::string HexWord(const Word& word) {
    static constexpr char digits[] = "0123456789ABCDEF";
    std::string result;
    for (std::uint8_t byte : word.bytes) {
        result.push_back(digits[byte >> 4]);
        result.push_back(digits[byte & 15]);
    }
    return result;
}

void SetWordHex(Word& word, const std::string& hex) {
    auto nibble = [](char value) {
        if (value >= '0' && value <= '9') return value - '0';
        if (value >= 'A' && value <= 'F') return value - 'A' + 10;
        if (value >= 'a' && value <= 'f') return value - 'a' + 10;
        return 0;
    };
    word.bytes.clear();
    for (std::size_t i = 0; i + 1 < hex.size(); i += 2)
        word.bytes.push_back(static_cast<std::uint8_t>(
            nibble(hex[i]) * 16 + nibble(hex[i + 1])));
}

void ApplyMutations() {
    std::ifstream input(gameDirectory / "mutations.lua");
    std::string line;
    std::array<bool, 9> loadedPlayerValue{};
    while (std::getline(input, line)) {
        char id[128]{}, hex[1024]{};
        int row, column, red, green, blue, value;
        if (sscanf_s(
                line.c_str(),
                "set_pixel(\"%127[^\"]\", %d, %d, %d, %d, %d)",
                id, static_cast<unsigned>(_countof(id)), &row, &column,
                &red, &green, &blue) == 6) {
            auto type = types.find(id);
            if (type != types.end() && row > 0 && column > 0 &&
                row <= static_cast<int>(type->second.sprite.size()) &&
                column <=
                    static_cast<int>(type->second.sprite[row - 1].size())) {
                Pixel& pixel = type->second.sprite[row - 1][column - 1];
                pixel.rgb = {
                    std::clamp(red, 0, 255), std::clamp(green, 0, 255),
                    std::clamp(blue, 0, 255)};
            }
        } else if (sscanf_s(
                       line.c_str(),
                       "set_asset_pixel(\"%127[^\"]\", %d, %d, %d, %d, %d)",
                       id, static_cast<unsigned>(_countof(id)), &row, &column,
                       &red, &green, &blue) == 6) {
            auto asset = wallAssets.find(id);
            if (asset != wallAssets.end() && row > 0 && column > 0 &&
                row <= static_cast<int>(asset->second.sprite.size()) &&
                column <= static_cast<int>(
                    asset->second.sprite[row - 1].size())) {
                Pixel& pixel = asset->second.sprite[row - 1][column - 1];
                if (pixel.occupied)
                    pixel.rgb = {std::clamp(red, 0, 255),
                        std::clamp(green, 0, 255),
                        std::clamp(blue, 0, 255)};
            }
        } else if (sscanf_s(
                       line.c_str(),
                       "set_glyph(%d, %d, %d, %d, %d, %d)",
                       &value, &row, &column, &red, &green, &blue) == 6) {
            if (value >= 0 && value <= 255 && row > 0 && row <= 7 &&
                column > 0 && column <= 5) {
                text_renderer::GlyphPixel& pixel =
                    text_renderer::MutableGlyph(
                        static_cast<std::uint8_t>(value))
                        [row - 1][column - 1];
                if (pixel.occupied)
                    pixel.rgb = {
                        std::clamp(red, 0, 255),
                        std::clamp(green, 0, 255),
                        std::clamp(blue, 0, 255)};
            }
        } else if (sscanf_s(
                       line.c_str(),
                       "set_word(\"%127[^\"]\", \"%1023[^\"]\")",
                       id, static_cast<unsigned>(_countof(id)),
                       hex, static_cast<unsigned>(_countof(hex))) == 2) {
            const auto found = wordIds.find(id);
            if (found != wordIds.end()) SetWordHex(words[found->second], hex);
        } else if (sscanf_s(
                       line.c_str(), "set_organ(\"%127[^\"]\", %d)",
                       id, static_cast<unsigned>(_countof(id)), &value) == 2) {
            for (Organ& organ : organs)
                if (organ.id == id)
                    organ.value = std::clamp(value, 0, organ.maximum);
        } else if (sscanf_s(
                       line.c_str(), "set_level(%d)", &value) == 1) {
            levelNumber = std::max(0, value);
        } else if (sscanf_s(
                       line.c_str(), "set_player_rank(%d, %d)",
                       &row, &value) == 2) {
            if (row >= 0 && row < 3)
                playerInteriorState.repeatableRanks[row] =
                    static_cast<std::uint32_t>(std::max(0, value));
        } else if (sscanf_s(
                       line.c_str(), "set_player_permanent(%d)", &row) == 1) {
            if (row >= 0 && row < 8)
                playerInteriorState.permanent[row] = true;
        } else if (sscanf_s(
                       line.c_str(), "set_player_value(%d, %d)",
                       &row, &value) == 2) {
            if (row >= 0 && row < 9)
                playerInteriorState.values[row] =
                    row < 2 ? value : std::max(0, value);
            if (row >= 0 && row < 9) loadedPlayerValue[row] = true;
        } else if (sscanf_s(
                       line.c_str(), "set_player_doorway(%d)", &row) == 1) {
            if (row >= 0 && row < 12)
                playerInteriorState.brokenDoorways[row] = true;
        }
    }
    // Convert the old percent/millisecond Player Internals representation to
    // the new five-step baseline while preserving earned repeatable ranks.
    for (int index = 0; index < 2; ++index)
        if (loadedPlayerValue[index] &&
            playerInteriorState.values[index] > 20)
            playerInteriorState.values[index] =
                playerInteriorDefaults[index] -
                static_cast<int>(playerInteriorState.repeatableRanks[index]);
    for (int index = 0; index < 3; ++index)
        if (!loadedPlayerValue[index] &&
            playerInteriorState.repeatableRanks[index] > 0)
            playerInteriorState.values[index] =
                index < 2
                    ? playerInteriorDefaults[index] -
                        static_cast<int>(
                            playerInteriorState.repeatableRanks[index])
                    : std::max(
                        0, playerInteriorDefaults[index] -
                            static_cast<int>(
                                playerInteriorState.repeatableRanks[index]));
    for (int index = 0; index < 6; ++index)
        if (!loadedPlayerValue[index + 3] &&
            playerInteriorState.permanent[index])
            playerInteriorState.values[index + 3] = std::max(
                0, playerInteriorDefaults[index + 3] - 1);
    if (levelValueWord >= 0) {
        const std::string value = std::to_string(levelNumber);
        words[levelValueWord].bytes.assign(value.begin(), value.end());
    }
    const auto selectedMap = levelMaps.find(levelNumber);
    currentMap = selectedMap != levelMaps.end()
        ? selectedMap->second : "placeholder";
}

}  // namespace

bool LoadGoldenWorldForTools() {
    return LoadWorldScript(goldenDirectory / "world.lua");
}

void SaveMutations() {
    std::ofstream output(gameDirectory / "mutations.lua", std::ios::trunc);
    output << "-- Generated persistent giant-enemy state. Press R to reset.\n";
    output << "local function set_pixel(enemy,row,column,r,g,b) end\n";
    output << "local function set_asset_pixel(asset,row,column,r,g,b) end\n";
    output << "local function set_glyph(character,row,column,r,g,b) end\n";
    output << "local function set_word(id,hex) end\n";
    output << "local function set_organ(id,value) end\n";
    output << "local function set_level(value) end\n";
    output << "local function set_player_rank(index,value) end\n";
    output << "local function set_player_permanent(index) end\n";
    output << "local function set_player_value(index,value) end\n";
    output << "local function set_player_doorway(index) end\n";
    for (const auto& [id, type] : types)
        for (std::size_t row = 0; row < type.sprite.size(); ++row)
            for (std::size_t column = 0;
                 column < type.sprite[row].size(); ++column) {
                const Pixel& pixel = type.sprite[row][column];
                if (!pixel.occupied) continue;
                output << "set_pixel(\"" << id << "\", " << row + 1
                       << ", " << column + 1 << ", " << pixel.rgb[0]
                       << ", " << pixel.rgb[1] << ", " << pixel.rgb[2]
                       << ")\n";
            }
    for (const auto& [id, asset] : wallAssets)
        for (std::size_t row = 0; row < asset.sprite.size(); ++row)
            for (std::size_t column = 0;
                 column < asset.sprite[row].size(); ++column) {
                const Pixel& pixel = asset.sprite[row][column];
                if (!pixel.occupied) continue;
                output << "set_asset_pixel(\"" << id << "\", " << row + 1
                       << ", " << column + 1 << ", " << pixel.rgb[0]
                       << ", " << pixel.rgb[1] << ", " << pixel.rgb[2]
                       << ")\n";
            }
    for (int character = 33; character <= 126; ++character) {
        const text_renderer::Glyph& glyph =
            text_renderer::GetGlyph(
                static_cast<std::uint8_t>(character));
        for (int row = 0; row < 7; ++row)
            for (int column = 0; column < 5; ++column) {
                const text_renderer::GlyphPixel& pixel =
                    glyph[row][column];
                if (!pixel.occupied ||
                    (pixel.rgb[0] == 255 && pixel.rgb[1] == 255 &&
                     pixel.rgb[2] == 255))
                    continue;
                output << "set_glyph(" << character << ", "
                       << row + 1 << ", " << column + 1 << ", "
                       << pixel.rgb[0] << ", " << pixel.rgb[1] << ", "
                       << pixel.rgb[2] << ")\n";
            }
    }
    for (const Word& word : words)
        output << "set_word(\"" << word.id << "\", \""
               << HexWord(word) << "\")\n";
    output << "set_level(" << levelNumber << ")\n";
    for (int index = 0; index < 3; ++index)
        output << "set_player_rank(" << index << ", "
               << playerInteriorState.repeatableRanks[index] << ")\n";
    for (int index = 0; index < 8; ++index)
        if (playerInteriorState.permanent[index])
            output << "set_player_permanent(" << index << ")\n";
    for (int index = 0; index < 9; ++index)
        output << "set_player_value(" << index << ", "
               << playerInteriorState.values[index] << ")\n";
    for (int index = 0; index < 12; ++index)
        if (playerInteriorState.brokenDoorways[index])
            output << "set_player_doorway(" << index << ")\n";
}

void ResetWordMutations() {
    if (pristineWordBytes.size() != words.size()) return;
    for (std::size_t index = 0; index < words.size(); ++index)
        words[index].bytes = pristineWordBytes[index];
    SaveMutations();
    BuildWorldTextBoxes();
}

bool ReloadWorld(bool reset) {
    if (reset) {
        // Validate the golden world before replacing a working runtime copy.
        // A partially edited golden script must not make the next launch fail.
        if (!LoadWorldScript(goldenDirectory / "world.lua"))
            return false;
        if (!CopyGoldenWorld(true)) return false;
        playerInteriorState = PlayerInteriorState{};
        playerInteriorState.values = playerInteriorDefaults;
        std::error_code ignored;
        std::filesystem::remove(
            gameDirectory / "mutations.lua", ignored);
        ResetPlay();
        return true;
    }
    if (!CopyGoldenWorld(false)) return false;
    if (!LoadWorldScript(gameDirectory / "world.lua")) {
        if (!LoadWorldScript(goldenDirectory / "world.lua"))
            return false;
        if (!CopyGoldenWorld(true)) return false;
    }
    playerInteriorState = PlayerInteriorState{};
    playerInteriorState.values = playerInteriorDefaults;
    ApplyMutations();
    ResetPlay();
    return true;
}

}  // namespace game
