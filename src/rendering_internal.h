#pragma once

// Shared declarations for rendering_*.cpp translation units only.
// Nothing outside the rendering split should include this header.

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "arena_level.h"
#include "gameplay.h"
#include "rendering.h"
#include "state.h"

namespace game {

// Low-level pixel/rect primitives (rendering.cpp).
void DrawRectangle(
    int x, int y, int width, int height, std::uint32_t color);
void BlendPixel(int x, int y, std::uint32_t color, float alpha);
void DrawRectangleAlpha(
    int x, int y, int width, int height,
    std::uint32_t color, float alpha);
std::uint32_t CompositeColor(const Pixel& pixel, int divisor = 1);
std::uint32_t ChannelColor(int channel, int value);
void DrawWorldRect(const Rect& rect, std::uint32_t color);
void DrawArenaQuad(
    const std::array<ArenaMotifPoint, 4>& worldCorners,
    std::uint32_t color);
void DrawWorldRectAlpha(
    const Rect& rect, std::uint32_t color, float alpha);
void DrawWorldLine(
    float x, float y, float dx, float dy, float length, float width,
    std::uint32_t color, bool dotted);
void DrawPortalEffect(const Rect& portal, const char* assetId);
void DrawWord(
    int word, float x, float y, std::uint32_t color, bool world);
void DrawTextString(
    const std::string& text, int x, int y, std::uint32_t color);
void DrawPhrase(
    const std::vector<int>& phrase, float x, float y,
    std::uint32_t color, bool world);
std::string ShortNumber(float value);
void DrawChargerWindup(const Enemy& enemy);
void DrawEnemySprite(const Enemy& enemy);
void DrawPlayer();

// Run presentation (rendering_run.cpp).
void DrawPickupIcon(PickupType type, float x, float y);
void DrawRunMap();
void DrawRunArena();
void DrawRunHud();

// Coordinate-level presentation (rendering_levels.cpp).
void DrawAudioWaveform(const AudioPanel& panel);
void DrawAudioPanels();
void DrawGlyphArena();

}  // namespace game
