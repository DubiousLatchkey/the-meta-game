#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>

namespace game {

enum class Sound {
    LaserShoot,
    HitEnemy,
    HitHurt,
    Explosion,
    AimTick,
    RailgunShot,
    ChargerChargeUp,
    ChargerGo,
};

bool InitializeAudio(
    HWND window,
    const std::filesystem::path& goldenDirectory,
    const std::filesystem::path& alteredDirectory);
bool ResetAudio();
void PlaySoundEffect(Sound sound);
std::size_t AudioSampleCount(Sound sound);
std::uint8_t AudioPixelAverage(Sound sound, std::size_t pixel);
bool DamageAudioPixel(Sound sound, std::size_t pixel, int damage);
void UpdateAudio(float dt);
void ShutdownAudio();

}  // namespace game
