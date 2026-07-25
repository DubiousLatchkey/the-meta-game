#include <algorithm>

#include "arena_level.h"
#include "gameplay_internal.h"
#include "text_renderer.h"
#include "world.h"

namespace game {
namespace {

bool menuActive = false;
bool menuStarting = false;
bool menuPortalActive = false;
float menuFadeRemaining = 0;
constexpr float kMenuFadeSeconds = 0.75f;

void AddMenuPhrase(
    const char* id, float centerX, float y, bool title, bool start) {
    const auto found = phrases.find(id);
    if (found == phrases.end()) return;
    float x = centerX - PhraseWidth(found->second) * 0.5f;
    for (int word : found->second) {
        const float width = static_cast<float>(
            text_renderer::MeasureWidth(words[word].bytes.size()));
        textBoxes.push_back({
            {x, y, width, static_cast<float>(text_renderer::kGlyphHeight)},
            word, -1, false, false, title, start});
        x += width + text_renderer::kGlyphAdvance;
    }
}

void BuildMenuText() {
    textBoxes.clear();
    AddMenuPhrase(
        "main_title", kRunArenaWidth * 0.5f, 360.0f, true, false);
    AddMenuPhrase(
        "start_game", kRunArenaWidth * 0.5f, 680.0f, false, true);
}

void ConfigureMenu(bool placePlayer) {
    menuActive = true;
    debugRoom = false;
    run.mapActive = false;
    currentMap = "interior";
    projectiles.clear();
    bombs.clear();
    explosions.clear();
    enemyRails.clear();
    enemies.clear();
    spawners.clear();
    shieldBlocks.clear();
    runArena = BuildArenaLevel(
        {0, 0, kRunArenaWidth, kRunArenaHeight},
        0x4d41494e4d454e55ULL, {}, 12, kWall);
    if (placePlayer) {
        playerX = kRunArenaWidth * 0.5f - kPlayerSize * 0.5f;
        playerY = kRunArenaHeight * 0.5f - kPlayerSize * 0.5f;
        playerHealth = kPlayerMaxHealth;
        playerInvincibility = 0;
    }
    lastPlayerRoom = -1;
    BuildMenuText();
}

}  // namespace

void EnterMainMenu() {
    menuStarting = false;
    menuPortalActive = false;
    menuFadeRemaining = 0;
    ConfigureMenu(true);
}

bool MainMenuActive() { return menuActive; }
bool MainMenuStarting() { return menuStarting; }
bool MainMenuPortalActive() { return menuPortalActive; }

float MainMenuTitleAlpha() {
    if (!menuStarting) return menuPortalActive ? 0.0f : 1.0f;
    return std::clamp(menuFadeRemaining / kMenuFadeSeconds, 0.0f, 1.0f);
}

Rect MainMenuPortalRect() {
    return {kRunArenaWidth * 0.5f - 36.0f, 770.0f, 72, 72};
}

void TriggerMainMenuStart() {
    if (!menuActive || menuStarting || menuPortalActive) return;
    menuStarting = true;
    menuFadeRemaining = kMenuFadeSeconds;
}

void UpdateMainMenu(float dt) {
    if (!menuStarting) return;
    menuFadeRemaining = std::max(0.0f, menuFadeRemaining - dt);
    if (menuFadeRemaining > 0) return;
    StartFreshRun(false);
    menuStarting = false;
    menuPortalActive = true;
    // Starting a run preserves where the player shot START GAME so the
    // newly opened portal below it can be reached naturally. Full reset and
    // death still enter the menu through EnterMainMenu(), which respawns them.
    ConfigureMenu(false);
}

void LeaveMainMenu() { menuActive = false; }

}  // namespace game
