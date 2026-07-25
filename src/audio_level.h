#pragma once

#include <cstddef>
#include <vector>

#include "audio.h"
#include "state.h"

namespace game {

enum class AudioRepresentation {
    Waveform,
    Grayscale,
};

struct AudioPanel {
    Sound sound = Sound::LaserShoot;
    AudioRepresentation representation = AudioRepresentation::Waveform;
    Rect rect{};
    bool vertical = false;
};

void BuildAudioLevel();
const std::vector<AudioPanel>& AudioPanels();
float AudioWaveY(const AudioPanel& panel, std::size_t pixel);
bool AudioPanelOverlaps(const AudioPanel& panel, const Rect& rectangle);
bool HitAudioPanel(const AudioPanel& panel, const Rect& shot);
bool AudioGeometryOverlaps(const Rect& rectangle);
bool AudioLevelAllowsPlayer(const Rect& rectangle);
bool HitAudioGeometry(const Rect& shot);

}  // namespace game
