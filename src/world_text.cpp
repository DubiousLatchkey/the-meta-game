#include "world.h"

#include <algorithm>

#include "audio_level.h"
#include "glyph_level.h"
#include "text_renderer.h"

namespace game {

namespace {

void AddPhraseBoxes(
    const std::vector<int>& phrase, float x, float y, int organ = -1) {
    for (int word : phrase) {
        const int width =
            text_renderer::MeasureWidth(words[word].bytes.size());
        textBoxes.push_back(
            {{x, y, static_cast<float>(width),
              static_cast<float>(text_renderer::kGlyphHeight)},
             word, organ, false});
        x += width + text_renderer::kGlyphAdvance;
    }
}

}  // namespace

int PhraseWidth(const std::vector<int>& phrase) {
    int width = 0;
    for (std::size_t i = 0; i < phrase.size(); ++i) {
        if (i) width += text_renderer::kGlyphAdvance;
        width += text_renderer::MeasureWidth(words[phrase[i]].bytes.size());
    }
    return width;
}

void BuildWorldTextBoxes() {
    textBoxes.clear();
    if (currentMap == "audio") {
        const LevelRegion* region = CurrentLevelRegion();
        if (!region || levelValueWord < 0) return;
        const float x = region->x + 40.0f;
        const float y = region->y + 420.0f;
        AddPhraseBoxes(levelLabel, x, y);
        const int labelWidth = PhraseWidth(levelLabel);
        const int valueWidth = text_renderer::MeasureWidth(
            words[levelValueWord].bytes.size());
        textBoxes.push_back(
            {{x + labelWidth + text_renderer::kGlyphAdvance, y,
              static_cast<float>(valueWidth),
              static_cast<float>(text_renderer::kGlyphHeight)},
             levelValueWord, -1, false, true});
        return;
    }
    if (currentMap != "interior") return;
    const auto roomPhrase = phrases.find("room");
    if (roomPhrase != phrases.end())
        for (const Room& room : rooms)
            AddPhraseBoxes(
                roomPhrase->second, RoomX(room) + 42, RoomY(room) + 42);
    for (int index = 0; index < static_cast<int>(organs.size()); ++index) {
        const Organ& organ = organs[index];
        if (organ.room < 0) continue;
        const Room& room = rooms[organ.room];
        const float center =
            RoomX(room) + interior.roomSize * 0.5f;
        const int labelWidth = PhraseWidth(organ.label);
        AddPhraseBoxes(
            organ.label, center - labelWidth * 0.5f,
            RoomY(room) + interior.roomSize * 0.45f, index);
        if (organ.valueWord >= 0) {
            const int width = text_renderer::MeasureWidth(
                words[organ.valueWord].bytes.size());
            textBoxes.push_back(
                {{center - width * 0.5f,
                  RoomY(room) + interior.roomSize * 0.53f,
                  static_cast<float>(width),
                  static_cast<float>(text_renderer::kGlyphHeight)},
                 organ.valueWord, index, true});
        }
    }
}

int OrganIndex(const std::string& id) {
    for (int index = 0;
         index < static_cast<int>(organs.size()); ++index)
        if (organs[index].id == id) return index;
    return -1;
}

void UpdateSpawnerTypes() {
    for (Spawner& spawner : spawners)
        spawner.enemyType = interior.enemy;
}

void SelectCurrentLevel() {
    const std::string previousMap = currentMap;
    const auto selectedRegion = levelRegions.find(levelNumber);
    currentMap = selectedRegion != levelRegions.end()
        ? selectedRegion->second.map : "placeholder";
    if (levelValueWord >= 0) {
        const std::string value = std::to_string(levelNumber);
        words[levelValueWord].bytes.assign(value.begin(), value.end());
    }
    if (currentMap != previousMap) {
        // Map-owned actors never cross into a different simulation.
        enemies.clear();
        spawners.clear();
        lastPlayerRoom = -1;
    }
    if (currentMap != "interior") {
        projectiles.clear();
        bombs.clear();
        explosions.clear();
        enemyRails.clear();
        shieldBlocks.clear();
        textBoxes.clear();
    }
    if (selectedRegion != levelRegions.end()) {
        playerX =
            selectedRegion->second.x + selectedRegion->second.spawnX;
        playerY =
            selectedRegion->second.y + selectedRegion->second.spawnY;
    }
    BuildAudioLevel();
    BuildGlyphLevel();
    if (currentMap == "audio") BuildWorldTextBoxes();
}

void BuildShields() {
    shieldBlocks.clear();
    const int shieldOrgan = OrganIndex("shield");
    const int health = shieldOrgan >= 0
        ? std::max(0, organs[shieldOrgan].value) : 0;
    constexpr float padding = 24.0f;
    constexpr float thickness = 14.0f;
    constexpr float blockLength = 32.0f;
    for (int organIndex = 0;
         organIndex < static_cast<int>(organs.size()); ++organIndex) {
        float left = 1.0e30f, top = 1.0e30f;
        float right = -1.0e30f, bottom = -1.0e30f;
        for (const TextBox& box : textBoxes) {
            if (box.organ != organIndex) continue;
            left = std::min(left, box.rect.x);
            top = std::min(top, box.rect.y);
            right = std::max(right, box.rect.x + box.rect.width);
            bottom = std::max(bottom, box.rect.y + box.rect.height);
        }
        if (right <= left || bottom <= top) continue;
        left -= padding;
        right += padding;
        top -= padding;
        bottom += padding;
        for (float x = left; x < right; x += blockLength) {
            const float width = std::min(blockLength, right - x);
            shieldBlocks.push_back(
                {{x, top, width, thickness}, organIndex, health});
            shieldBlocks.push_back(
                {{x, bottom - thickness, width, thickness},
                 organIndex, health});
        }
        for (float y = top + thickness;
             y < bottom - thickness; y += blockLength) {
            const float height =
                std::min(blockLength, bottom - thickness - y);
            shieldBlocks.push_back(
                {{left, y, thickness, height}, organIndex, health});
            shieldBlocks.push_back(
                {{right - thickness, y, thickness, height},
                 organIndex, health});
        }
    }
}

bool HitsShield(const Rect& rectangle) {
    for (const ShieldBlock& shield : shieldBlocks)
        if (shield.health > 0 && Overlaps(rectangle, shield.rect))
            return true;
    return false;
}

void UpdateValueWord(Organ& organ) {
    if (organ.valueWord < 0) return;
    const std::string value = std::to_string(organ.value);
    words[organ.valueWord].bytes.assign(value.begin(), value.end());
}

bool SpawnPercentageZero() {
    return false;
}

}  // namespace game
