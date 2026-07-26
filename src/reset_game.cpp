#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <filesystem>
#include <system_error>

namespace {

bool CopyFileReplacing(
    const std::filesystem::path& source,
    const std::filesystem::path& destination,
    std::error_code& error) {
    std::filesystem::create_directories(destination.parent_path(), error);
    if (error) return false;
    std::filesystem::copy_file(
        source, destination,
        std::filesystem::copy_options::overwrite_existing, error);
    return !error;
}

bool ResetGameData(const std::filesystem::path& root) {
    std::error_code error;
    const auto goldenScripts = root / L"golden_scripts";
    const auto goldenAudio = root / L"golden_audio";
    const auto game = root / L"game";
    if (!CopyFileReplacing(
            goldenScripts / L"world.lua", game / L"world.lua", error))
        return false;

    std::filesystem::remove_all(game / L"audio", error);
    if (error) return false;
    std::filesystem::create_directories(game / L"audio", error);
    if (error) return false;
    for (const auto& entry :
         std::filesystem::directory_iterator(goldenAudio, error)) {
        if (error) return false;
        if (!entry.is_regular_file()) continue;
        if (!CopyFileReplacing(
                entry.path(), game / L"audio" / entry.path().filename(),
                error))
            return false;
    }

    std::filesystem::remove(game / L"mutations.lua", error);
    return !error;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    wchar_t executable[MAX_PATH]{};
    GetModuleFileNameW(nullptr, executable, MAX_PATH);
    const std::filesystem::path root =
        std::filesystem::path(executable).parent_path();
    const bool reset = ResetGameData(root);
    MessageBoxW(
        nullptr,
        reset
            ? L"World and runtime audio have been restored to golden defaults."
            : L"Could not restore the game data. Close the game and try again.",
        L"The Meta Game Reset",
        MB_OK | (reset ? MB_ICONINFORMATION : MB_ICONERROR));
    return reset ? 0 : 1;
}
