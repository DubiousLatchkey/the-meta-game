#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <filesystem>

namespace game {

enum class Sound {
    LaserShoot,
    HitEnemy,
    HitHurt,
    Explosion,
};

bool InitializeAudio(
    HWND window,
    const std::filesystem::path& goldenDirectory,
    const std::filesystem::path& alteredDirectory);
bool ResetAudio();
void PlaySoundEffect(Sound sound);
void UpdateAudio();
void ShutdownAudio();

}  // namespace game
