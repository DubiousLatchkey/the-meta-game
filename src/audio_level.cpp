#include "audio_level.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace game {
namespace {

std::vector<AudioPanel> panels;

std::size_t FirstPixel(const AudioPanel& panel, const Rect& rectangle) {
    const float panelStart = panel.vertical ? panel.rect.y : panel.rect.x;
    const float rectangleStart = panel.vertical ? rectangle.y : rectangle.x;
    return static_cast<std::size_t>(std::max(
        0.0f, std::floor(rectangleStart - panelStart) - 1.0f));
}

std::size_t LastPixel(const AudioPanel& panel, const Rect& rectangle) {
    const std::size_t count = (AudioSampleCount(panel.sound) + 1) / 2;
    if (count == 0) return 0;
    const float panelStart = panel.vertical ? panel.rect.y : panel.rect.x;
    const float rectangleEnd = panel.vertical
        ? rectangle.y + rectangle.height : rectangle.x + rectangle.width;
    return std::min(count - 1, static_cast<std::size_t>(std::max(
        0.0f, std::ceil(rectangleEnd - panelStart) + 1.0f)));
}

Rect WaveSegment(const AudioPanel& panel, std::size_t pixel) {
    if (panel.vertical) {
        const float x = panel.rect.x +
            (255.0f - AudioPixelAverage(panel.sound, pixel));
        const float previousX = pixel > 0
            ? panel.rect.x +
                (255.0f - AudioPixelAverage(panel.sound, pixel - 1))
            : x;
        return {
            std::min(x, previousX) - 2.0f,
            panel.rect.y + static_cast<float>(pixel) - 1.0f,
            std::abs(x - previousX) + 4.0f, 3.0f};
    }
    const float y = AudioWaveY(panel, pixel);
    const float previousY =
        pixel > 0 ? AudioWaveY(panel, pixel - 1) : y;
    const float top = std::min(y, previousY) - 2.0f;
    return {
        panel.rect.x + static_cast<float>(pixel) - 1.0f,
        top, 3.0f, std::abs(y - previousY) + 4.0f};
}

}  // namespace

bool AudioPanelOverlaps(
    const AudioPanel& panel, const Rect& rectangle) {
    if (!Overlaps(
            {panel.rect.x - 2, panel.rect.y - 2,
             panel.rect.width + 4, panel.rect.height + 4},
            rectangle))
        return false;
    if (panel.representation == AudioRepresentation::Grayscale)
        return Overlaps(panel.rect, rectangle);
    const std::size_t first = FirstPixel(panel, rectangle);
    const std::size_t last = LastPixel(panel, rectangle);
    for (std::size_t pixel = first; pixel <= last; ++pixel)
        if (Overlaps(WaveSegment(panel, pixel), rectangle))
            return true;
    return false;
}

void BuildAudioLevel() {
    panels.clear();
    const LevelRegion* region = CurrentLevelRegion();
    if (!region || region->map != "audio") return;
    constexpr std::array<Sound, 3> sounds{
        Sound::LaserShoot, Sound::HitEnemy, Sound::HitHurt};
    for (std::size_t index = 0; index < sounds.size(); ++index) {
        const float width = static_cast<float>(
            (AudioSampleCount(sounds[index]) + 1) / 2);
        const float x = region->x + 120.0f;
        const float y =
            region->y + 80.0f + static_cast<float>(index) * 700.0f;
        panels.push_back(
            {sounds[index], AudioRepresentation::Waveform,
             {x, y, width, 256.0f}});
    }
}

const std::vector<AudioPanel>& AudioPanels() {
    return panels;
}

float AudioWaveY(const AudioPanel& panel, std::size_t pixel) {
    return panel.rect.y +
        (255.0f - AudioPixelAverage(panel.sound, pixel));
}

bool AudioGeometryOverlaps(const Rect& rectangle) {
    for (const AudioPanel& panel : panels)
        if (AudioPanelOverlaps(panel, rectangle)) return true;
    return false;
}

bool AudioLevelAllowsPlayer(const Rect& rectangle) {
    const LevelRegion* region = CurrentLevelRegion();
    if (!region || region->map != "audio") return false;
    constexpr float border = 20.0f;
    const Rect bounds{
        region->x + border, region->y + border,
        region->width - border * 2.0f,
        region->height - border * 2.0f};
    return rectangle.x >= bounds.x && rectangle.y >= bounds.y &&
           rectangle.x + rectangle.width <= bounds.x + bounds.width &&
           rectangle.y + rectangle.height <= bounds.y + bounds.height &&
           !AudioGeometryOverlaps(rectangle);
}

bool HitAudioPanel(const AudioPanel& panel, const Rect& shot) {
    if (!AudioPanelOverlaps(panel, shot)) return false;
    const std::size_t first = FirstPixel(panel, shot);
    const std::size_t last = LastPixel(panel, shot);
    std::size_t hitPixel = first;
    if (panel.representation == AudioRepresentation::Waveform) {
        bool found = false;
        for (std::size_t pixel = first; pixel <= last; ++pixel)
            if (Overlaps(WaveSegment(panel, pixel), shot)) {
                hitPixel = pixel;
                found = true;
                break;
            }
        if (!found) return false;
    } else {
        hitPixel = static_cast<std::size_t>(std::clamp(
            std::floor(CenterX(shot) - panel.rect.x),
            0.0f, std::max(0.0f, panel.rect.width - 1.0f)));
    }
    if (DamageAudioPixel(panel.sound, hitPixel, kAudioSampleDamage)) {
        PlaySoundEffect(panel.sound);
        return true;
    }
    return false;
}

bool HitAudioGeometry(const Rect& shot) {
    for (const AudioPanel& panel : panels)
        if (HitAudioPanel(panel, shot)) return true;
    return false;
}

}  // namespace game
