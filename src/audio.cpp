#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>
#include <dsound.h>

#include "audio.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <system_error>
#include <vector>

namespace game {
namespace {

std::filesystem::path goldenAudioDirectory;
std::filesystem::path alteredAudioDirectory;
IDirectSound8* soundDevice = nullptr;

struct DecodedWave {
    WAVEFORMATEX format{};
    std::vector<std::uint8_t> samples;
};

struct CachedSound {
    Sound sound;
    IDirectSoundBuffer* buffer = nullptr;
    WAVEFORMATEX format{};
    std::vector<std::uint8_t> samples;
    bool dirty = false;
    float dirtySeconds = 0;
};

std::array<CachedSound, 13> cachedSounds{{
    {Sound::LaserShoot, nullptr},
    {Sound::HitEnemy, nullptr},
    {Sound::HitHurt, nullptr},
    {Sound::Explosion, nullptr},
    {Sound::AimTick, nullptr},
    {Sound::RailgunShot, nullptr},
    {Sound::ChargerChargeUp, nullptr},
    {Sound::ChargerGo, nullptr},
    {Sound::Teleport, nullptr},
    {Sound::PowerUp, nullptr},
    {Sound::ValueLowered, nullptr},
    {Sound::SpawnerHit, nullptr},
    {Sound::SpawnerDeath, nullptr},
}};
std::vector<IDirectSoundBuffer*> voices;

struct SoundFile {
    Sound sound;
    const wchar_t* filename;
};

constexpr std::array<SoundFile, 13> kSoundFiles{{
    {Sound::LaserShoot, L"laserShoot.wav"},
    {Sound::HitEnemy, L"hitEnemy.wav"},
    {Sound::HitHurt, L"hitHurt.wav"},
    {Sound::Explosion, L"explosion.wav"},
    {Sound::AimTick, L"aimTick.wav"},
    {Sound::RailgunShot, L"railgunShot.wav"},
    {Sound::ChargerChargeUp, L"chargerChargeUp.wav"},
    {Sound::ChargerGo, L"chargerGo.wav"},
    {Sound::Teleport, L"teleport.wav"},
    {Sound::PowerUp, L"powerUp.wav"},
    {Sound::ValueLowered, L"valueLowered.wav"},
    {Sound::SpawnerHit, L"spawnerHit.wav"},
    {Sound::SpawnerDeath, L"spawnerDeath.wav"},
}};

const wchar_t* FilenameFor(Sound sound) {
    for (const SoundFile& file : kSoundFiles)
        if (file.sound == sound) return file.filename;
    return L"";
}

std::uint16_t Read16(
    const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    return static_cast<std::uint16_t>(
        bytes[offset] | (bytes[offset + 1] << 8));
}

std::uint32_t Read32(
    const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    return static_cast<std::uint32_t>(bytes[offset]) |
           (static_cast<std::uint32_t>(bytes[offset + 1]) << 8) |
           (static_cast<std::uint32_t>(bytes[offset + 2]) << 16) |
           (static_cast<std::uint32_t>(bytes[offset + 3]) << 24);
}

bool HasTag(
    const std::vector<std::uint8_t>& bytes,
    std::size_t offset, const char (&tag)[5]) {
    return offset + 4 <= bytes.size() &&
           std::memcmp(bytes.data() + offset, tag, 4) == 0;
}

bool DecodeWave(
    const std::filesystem::path& path, DecodedWave& wave) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return false;
    input.seekg(0, std::ios::end);
    const std::streamoff length = input.tellg();
    if (length < 12) return false;
    input.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> bytes(
        static_cast<std::size_t>(length));
    input.read(
        reinterpret_cast<char*>(bytes.data()),
        static_cast<std::streamsize>(bytes.size()));
    if (!input || !HasTag(bytes, 0, "RIFF") ||
        !HasTag(bytes, 8, "WAVE"))
        return false;

    bool hasFormat = false;
    bool hasSamples = false;
    for (std::size_t offset = 12; offset + 8 <= bytes.size();) {
        const std::uint32_t chunkSize = Read32(bytes, offset + 4);
        const std::size_t dataOffset = offset + 8;
        if (dataOffset + chunkSize > bytes.size()) return false;
        if (HasTag(bytes, offset, "fmt ") && chunkSize >= 16) {
            wave.format.wFormatTag = Read16(bytes, dataOffset);
            wave.format.nChannels = Read16(bytes, dataOffset + 2);
            wave.format.nSamplesPerSec = Read32(bytes, dataOffset + 4);
            wave.format.nAvgBytesPerSec = Read32(bytes, dataOffset + 8);
            wave.format.nBlockAlign = Read16(bytes, dataOffset + 12);
            wave.format.wBitsPerSample = Read16(bytes, dataOffset + 14);
            wave.format.cbSize = chunkSize >= 18
                ? Read16(bytes, dataOffset + 16) : 0;
            hasFormat = true;
        } else if (HasTag(bytes, offset, "data")) {
            wave.samples.assign(
                bytes.begin() + static_cast<std::ptrdiff_t>(dataOffset),
                bytes.begin() +
                    static_cast<std::ptrdiff_t>(dataOffset + chunkSize));
            hasSamples = !wave.samples.empty();
        }
        offset = dataOffset + chunkSize + (chunkSize & 1u);
    }
    return hasFormat && hasSamples;
}

bool LoadCachedSound(CachedSound& cached) {
    DecodedWave wave;
    if (!DecodeWave(
            alteredAudioDirectory / FilenameFor(cached.sound), wave))
        return false;
    DSBUFFERDESC description{};
    description.dwSize = sizeof(description);
    description.dwFlags = DSBCAPS_CTRLVOLUME | DSBCAPS_GLOBALFOCUS;
    description.dwBufferBytes = static_cast<DWORD>(wave.samples.size());
    description.lpwfxFormat = &wave.format;
    IDirectSoundBuffer* buffer = nullptr;
    if (!soundDevice ||
        FAILED(soundDevice->CreateSoundBuffer(
            &description, &buffer, nullptr)))
        return false;

    void* first = nullptr;
    void* second = nullptr;
    DWORD firstSize = 0;
    DWORD secondSize = 0;
    if (FAILED(buffer->Lock(
            0, description.dwBufferBytes,
            &first, &firstSize, &second, &secondSize, 0))) {
        buffer->Release();
        return false;
    }
    std::memcpy(first, wave.samples.data(), firstSize);
    if (secondSize > 0)
        std::memcpy(
            second, wave.samples.data() + firstSize, secondSize);
    buffer->Unlock(first, firstSize, second, secondSize);
    if (cached.buffer) cached.buffer->Release();
    cached.buffer = buffer;
    cached.format = wave.format;
    cached.samples = std::move(wave.samples);
    cached.dirty = false;
    cached.dirtySeconds = 0;
    return true;
}

bool ReloadCachedSounds() {
    bool loaded = true;
    for (CachedSound& cached : cachedSounds)
        loaded = LoadCachedSound(cached) && loaded;
    return loaded;
}

void CollectFinishedVoices() {
    voices.erase(
        std::remove_if(
            voices.begin(), voices.end(),
            [](IDirectSoundBuffer* voice) {
                DWORD status = 0;
                if (SUCCEEDED(voice->GetStatus(&status)) &&
                    (status & DSBSTATUS_PLAYING) != 0)
                    return false;
                voice->Release();
                return true;
            }),
        voices.end());
}

CachedSound* FindCachedSound(Sound sound) {
    for (CachedSound& cached : cachedSounds)
        if (cached.sound == sound) return &cached;
    return nullptr;
}

bool UpdateCachedRange(
    CachedSound& cached, std::size_t offset, std::size_t count) {
    if (!cached.buffer || count == 0 ||
        offset + count > cached.samples.size())
        return false;
    void* first = nullptr;
    void* second = nullptr;
    DWORD firstSize = 0;
    DWORD secondSize = 0;
    if (FAILED(cached.buffer->Lock(
            static_cast<DWORD>(offset), static_cast<DWORD>(count),
            &first, &firstSize, &second, &secondSize, 0)))
        return false;
    std::memcpy(first, cached.samples.data() + offset, firstSize);
    if (secondSize > 0)
        std::memcpy(
            second, cached.samples.data() + offset + firstSize,
            secondSize);
    cached.buffer->Unlock(first, firstSize, second, secondSize);
    return true;
}

void Write16(std::ostream& output, std::uint16_t value) {
    const char bytes[2]{
        static_cast<char>(value & 0xFF),
        static_cast<char>((value >> 8) & 0xFF)};
    output.write(bytes, 2);
}

void Write32(std::ostream& output, std::uint32_t value) {
    const char bytes[4]{
        static_cast<char>(value & 0xFF),
        static_cast<char>((value >> 8) & 0xFF),
        static_cast<char>((value >> 16) & 0xFF),
        static_cast<char>((value >> 24) & 0xFF)};
    output.write(bytes, 4);
}

bool SaveCachedSound(const CachedSound& cached) {
    const std::filesystem::path destination =
        alteredAudioDirectory / FilenameFor(cached.sound);
    std::filesystem::path temporary = destination;
    temporary += L".tmp";
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    if (!output) return false;
    const std::uint32_t dataSize =
        static_cast<std::uint32_t>(cached.samples.size());
    const std::uint32_t paddedDataSize = dataSize + (dataSize & 1u);
    output.write("RIFF", 4);
    Write32(output, 36u + paddedDataSize);
    output.write("WAVEfmt ", 8);
    Write32(output, 16);
    Write16(output, cached.format.wFormatTag);
    Write16(output, cached.format.nChannels);
    Write32(output, cached.format.nSamplesPerSec);
    Write32(output, cached.format.nAvgBytesPerSec);
    Write16(output, cached.format.nBlockAlign);
    Write16(output, cached.format.wBitsPerSample);
    output.write("data", 4);
    Write32(output, dataSize);
    output.write(
        reinterpret_cast<const char*>(cached.samples.data()),
        static_cast<std::streamsize>(cached.samples.size()));
    if (dataSize & 1u) output.put('\0');
    output.close();
    if (!output) return false;
    return MoveFileExW(
               temporary.c_str(), destination.c_str(),
               MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
}

void FlushDirtySounds() {
    for (CachedSound& cached : cachedSounds) {
        if (!cached.dirty) continue;
        if (SaveCachedSound(cached)) {
            cached.dirty = false;
            cached.dirtySeconds = 0;
        }
    }
}

bool CopyGolden(bool overwrite) {
    std::error_code error;
    std::filesystem::create_directories(alteredAudioDirectory, error);
    if (error) return false;
    for (const SoundFile& file : kSoundFiles) {
        const auto source = goldenAudioDirectory / file.filename;
        const auto destination = alteredAudioDirectory / file.filename;
        if (!std::filesystem::exists(source)) return false;
        if (overwrite || !std::filesystem::exists(destination)) {
            error.clear();
            std::filesystem::copy_file(
                source, destination,
                std::filesystem::copy_options::overwrite_existing, error);
            if (error) return false;
        }
    }
    return true;
}

}  // namespace

bool InitializeAudio(
    HWND window,
    const std::filesystem::path& goldenDirectory,
    const std::filesystem::path& alteredDirectory) {
    goldenAudioDirectory = goldenDirectory;
    alteredAudioDirectory = alteredDirectory;
    if (!CopyGolden(false)) return false;
    if (FAILED(DirectSoundCreate8(
            nullptr, &soundDevice, nullptr)))
        return false;
    if (FAILED(soundDevice->SetCooperativeLevel(
            window, DSSCL_NORMAL))) {
        soundDevice->Release();
        soundDevice = nullptr;
        return false;
    }
    return ReloadCachedSounds();
}

bool ResetAudio() {
    return CopyGolden(true) && ReloadCachedSounds();
}

void PlaySoundEffect(Sound sound) {
    CollectFinishedVoices();
    IDirectSoundBuffer* source = nullptr;
    for (const CachedSound& cached : cachedSounds)
        if (cached.sound == sound) source = cached.buffer;
    if (!source || !soundDevice) return;
    IDirectSoundBuffer* voice = nullptr;
    if (FAILED(soundDevice->DuplicateSoundBuffer(source, &voice)))
        return;
    voice->SetCurrentPosition(0);
    if (FAILED(voice->Play(0, 0, 0))) {
        voice->Release();
        return;
    }
    voices.push_back(voice);
}

std::size_t AudioSampleCount(Sound sound) {
    const CachedSound* cached = FindCachedSound(sound);
    if (!cached || cached->format.wFormatTag != WAVE_FORMAT_PCM ||
        cached->format.nChannels != 1 ||
        cached->format.wBitsPerSample != 8)
        return 0;
    return cached->samples.size();
}

std::uint8_t AudioPixelAverage(Sound sound, std::size_t pixel) {
    const CachedSound* cached = FindCachedSound(sound);
    const std::size_t first = pixel * 2;
    if (!cached || first >= cached->samples.size()) return 0;
    const std::size_t second =
        std::min(first + 1, cached->samples.size() - 1);
    return static_cast<std::uint8_t>(
        (static_cast<int>(cached->samples[first]) +
         static_cast<int>(cached->samples[second])) /
        2);
}

bool DamageAudioPixel(Sound sound, std::size_t pixel, int damage) {
    CachedSound* cached = FindCachedSound(sound);
    if (!cached || pixel * 2 >= cached->samples.size() || damage <= 0)
        return false;
    constexpr std::size_t damagePixelRadius = 2;
    const std::size_t pixelCount = (cached->samples.size() + 1) / 2;
    const std::size_t firstPixel =
        pixel > damagePixelRadius ? pixel - damagePixelRadius : 0;
    const std::size_t lastPixel = std::min(
        pixelCount - 1, pixel + damagePixelRadius);
    const std::size_t first = firstPixel * 2;
    const std::size_t end = std::min(
        cached->samples.size(), (lastPixel + 1) * 2);
    const std::size_t count = end - first;
    bool changed = false;
    for (std::size_t index = first; index < first + count; ++index) {
        const std::uint8_t previous = cached->samples[index];
        cached->samples[index] = static_cast<std::uint8_t>(
            std::max(0, static_cast<int>(previous) - damage));
        changed = changed || cached->samples[index] != previous;
    }
    if (!changed) return false;
    UpdateCachedRange(*cached, first, count);
    if (!cached->dirty) cached->dirtySeconds = 0;
    cached->dirty = true;
    return true;
}

void UpdateAudio(float dt) {
    CollectFinishedVoices();
    bool flush = false;
    for (CachedSound& cached : cachedSounds)
        if (cached.dirty) {
            cached.dirtySeconds += dt;
            flush = flush || cached.dirtySeconds >= 3.0f;
        }
    if (flush) FlushDirtySounds();
}

void ShutdownAudio() {
    FlushDirtySounds();
    for (IDirectSoundBuffer* voice : voices) {
        voice->Stop();
        voice->Release();
    }
    voices.clear();
    for (CachedSound& cached : cachedSounds) {
        if (cached.buffer) cached.buffer->Release();
        cached.buffer = nullptr;
    }
    if (soundDevice) soundDevice->Release();
    soundDevice = nullptr;
}

}  // namespace game
