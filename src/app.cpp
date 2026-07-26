#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <xinput.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>

#include "arena_level.h"
#include "audio.h"
#include "gameplay.h"
#include "rendering.h"
#include "state.h"
#include "world.h"

namespace game {
namespace {

void UpdateController(HWND window) {
    XINPUT_STATE state{};
    if (XInputGetState(0, &state) != ERROR_SUCCESS) {
        static bool firing = false;
        if (firing) ReleasePrimaryFire(aimX, aimY);
        firing = false;
        return;
    }
    const XINPUT_GAMEPAD& pad = state.Gamepad;
    const auto axis = [](SHORT value) {
        const float normalized = static_cast<float>(value) / 32767.0f;
        return std::abs(normalized) < 0.22f ? 0.0f : normalized;
    };
    keys[0] = axis(pad.sThumbLX) < 0;
    keys[1] = axis(pad.sThumbLX) > 0;
    keys[2] = axis(pad.sThumbLY) > 0;
    keys[3] = axis(pad.sThumbLY) < 0;

    const float rightX = axis(pad.sThumbRX);
    const float rightY = axis(pad.sThumbRY);
    if (rightX != 0 || rightY != 0) {
        RECT client{};
        GetClientRect(window, &client);
        aimX = (client.right - client.left) / 2 +
            static_cast<int>(rightX * 1000.0f);
        aimY = (client.bottom - client.top) / 2 -
            static_cast<int>(rightY * 1000.0f);
    }
    static bool firing = false;
    const bool primary = pad.bRightTrigger > XINPUT_GAMEPAD_TRIGGER_THRESHOLD;
    if (primary) BeginPrimaryFire(aimX, aimY);
    else if (firing) ReleasePrimaryFire(aimX, aimY);
    firing = primary;
    static bool secondaryHeld = false;
    const bool secondary = pad.bLeftTrigger >
        XINPUT_GAMEPAD_TRIGGER_THRESHOLD;
    if (secondary && !secondaryHeld) FireSecondary(aimX, aimY);
    secondaryHeld = secondary;
}

LRESULT CALLBACK WindowProcedure(
    HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_SIZE:
            ResizeBackBuffer(LOWORD(lParam), HIWORD(lParam));
            return 0;
        case WM_KEYDOWN:
        case WM_KEYUP: {
            const bool down = message == WM_KEYDOWN;
            if (wParam == 'A' || wParam == VK_LEFT) keys[0] = down;
            if (wParam == 'D' || wParam == VK_RIGHT) keys[1] = down;
            if (wParam == 'W' || wParam == VK_UP) keys[2] = down;
            if (wParam == 'S' || wParam == VK_DOWN) keys[3] = down;
            if (down && wParam == VK_F5 && ReloadWorld(false))
                EnterMainMenu();
            if (down && wParam == 'P') EnterDebugRoom();
            if (down && wParam == 'O') DebugClearCurrentNode();
            if (down && wParam == 'R') EnterMainMenu();
            if (down && wParam == VK_ESCAPE) DestroyWindow(window);
            return 0;
        }
        case WM_MOUSEMOVE:
            aimX = GET_X_LPARAM(lParam);
            aimY = GET_Y_LPARAM(lParam);
            return 0;
        case WM_LBUTTONDOWN:
            aimX = GET_X_LPARAM(lParam);
            aimY = GET_Y_LPARAM(lParam);
            BeginPrimaryFire(aimX, aimY);
            SetCapture(window);
            return 0;
        case WM_LBUTTONUP:
            ReleasePrimaryFire(
                GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            ReleaseCapture();
            return 0;
        case WM_RBUTTONDOWN:
            aimX = GET_X_LPARAM(lParam);
            aimY = GET_Y_LPARAM(lParam);
            FireSecondary(aimX, aimY);
            return 0;
        case WM_CAPTURECHANGED:
            shooting = false;
            run.primaryCharge = 0;
            return 0;
        case WM_KILLFOCUS:
            shooting = false;
            run.primaryCharge = 0;
            std::memset(keys, 0, sizeof(keys));
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT paint{};
            BeginPaint(window, &paint);
            EndPaint(window, &paint);
            return 0;
        }
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

}  // namespace
}  // namespace game

int WINAPI wWinMain(
    HINSTANCE instance, HINSTANCE, PWSTR commandLine, int showCommand) {
    using namespace game;
    SetProcessDPIAware();
    wchar_t executablePath[MAX_PATH]{};
    GetModuleFileNameW(nullptr, executablePath, MAX_PATH);
    const auto root =
        std::filesystem::path(executablePath).parent_path();
    goldenDirectory = root / "golden_scripts";
    gameDirectory = root / "game";
    const bool testAudioWallSelection = commandLine &&
        std::wstring(commandLine).find(
            L"--test-audio-wall-selection") != std::wstring::npos;
    if (commandLine &&
        std::wstring(commandLine).find(L"--interior-graph-tuner") !=
            std::wstring::npos) {
        const auto output =
            root.parent_path() / "test" / "interior_graph_tuner.html";
        if (!LoadGoldenWorldForTools() ||
            !GenerateInteriorGraphTuner(output))
            return 1;
        const std::wstring page = output.wstring();
        ShellExecuteW(
            nullptr, L"open", page.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        return 0;
    }
    if (commandLine &&
        std::wstring(commandLine).find(L"--generate-interior-graphs") !=
            std::wstring::npos) {
        const auto output =
            root.parent_path() / "test" / "interior_graphs";
        if (!LoadGoldenWorldForTools() ||
            !GenerateInteriorGraphGallery(output, 20))
            return 1;
        const std::wstring index =
            (output / "index.html").wstring();
        ShellExecuteW(
            nullptr, L"open", index.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        return 0;
    }
    if (commandLine &&
        std::wstring(commandLine).find(L"--generate-boss-interior-graphs") !=
            std::wstring::npos) {
        const auto output =
            root.parent_path() / "test" / "boss_interior_graphs";
        if (!LoadGoldenWorldForTools() ||
            !GenerateBossInteriorGraphGallery(output, 20))
            return 1;
        const std::wstring index =
            (output / "index.html").wstring();
        ShellExecuteW(
            nullptr, L"open", index.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        return 0;
    }

    WNDCLASSW windowClass{};
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = WindowProcedure;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_CROSS);
    windowClass.hbrBackground =
        reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    windowClass.lpszClassName = L"MetaGameGiantInterior";
    if (!RegisterClassW(&windowClass)) return 1;

    RECT rectangle{0, 0, kInitialWidth, kInitialHeight};
    AdjustWindowRect(&rectangle, WS_OVERLAPPEDWINDOW, FALSE);
    HWND window = CreateWindowExW(
        0, windowClass.lpszClassName,
        L"The Meta Game - Giant Enemy Interior",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
        rectangle.right - rectangle.left,
        rectangle.bottom - rectangle.top,
        nullptr, nullptr, instance, nullptr);
    if (!window) return 1;
    if (!InitializeAudio(
            window, root / "golden_audio", gameDirectory / "audio")) {
        ShutdownAudio();
        DestroyWindow(window);
        return 1;
    }
    if (!InitializeBackBuffer(window)) {
        ShutdownAudio();
        DestroyWindow(window);
        return 1;
    }
    if (!ReloadWorld(false)) {
        ShutdownAudio();
        DestroyBackBuffer();
        DestroyWindow(window);
        return 1;
    }
    if (testAudioWallSelection) {
        std::size_t audioPixels = 0;
        std::size_t invalidPixels = 0;
        for (std::uint64_t seed = 0; seed < 100; ++seed) {
            const ArenaLevel arena = BuildArenaLevel(
                {0, 0, kRunArenaWidth, kRunArenaHeight},
                seed, {}, 12, kWall);
            for (const ArenaMotifPixel& pixel : BuildArenaMotifPixels(arena)) {
                if (pixel.source != ArenaMotifSource::Audio) continue;
                ++audioPixels;
                const Sound sound =
                    static_cast<Sound>(std::stoi(pixel.id));
                if (pixel.audioPixel >=
                    (AudioSampleCount(sound) + 1) / 2)
                    ++invalidPixels;
            }
        }
        const ArenaLevel debugArena = BuildArenaLevel(
            {0, 0, kRunArenaWidth, kRunArenaHeight},
            0x4445425547524f4fULL, {}, 12, kWall, false, true,
            Sound::LaserShoot, PortalDirection::South);
        const std::size_t fullWaveformPixels =
            (AudioSampleCount(Sound::LaserShoot) + 1) / 2;
        const bool debugHasFullWaveform = std::any_of(
            debugArena.audioWalls.begin(), debugArena.audioWalls.end(),
            [](const AudioPanel& panel) {
                return panel.sound == Sound::LaserShoot &&
                    panel.representation == AudioRepresentation::Waveform &&
                    static_cast<std::size_t>(panel.rect.width) ==
                        (AudioSampleCount(Sound::LaserShoot) + 1) / 2;
            });
        const auto output =
            root.parent_path() / "test" / "audio_wall_selection.txt";
        std::filesystem::create_directories(output.parent_path());
        std::ofstream report(output, std::ios::trunc);
        report << "audio motif pixels: " << audioPixels << "\n"
               << "invalid 2-sample pixel mappings: " << invalidPixels
               << "\ndebug audio wall: "
               << (debugHasFullWaveform ? "full waveform" : "missing")
               << " (" << fullWaveformPixels << " pixels)\n";
        ShutdownAudio();
        DestroyBackBuffer();
        DestroyWindow(window);
        return audioPixels > 0 && invalidPixels == 0 && debugHasFullWaveform
            ? 0 : 1;
    }
    EnterMainMenu();

    ShowWindow(window, showCommand);
    LARGE_INTEGER frequency{}, previous{};
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&previous);
    const LONGLONG targetFrameTicks =
        frequency.QuadPart / kTargetFrameRate;
    bool running = true;
    while (running) {
        MSG message{};
        while (PeekMessageW(
                   &message, nullptr, 0, 0, PM_REMOVE)) {
            if (message.message == WM_QUIT) {
                running = false;
                break;
            }
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        LARGE_INTEGER current{};
        QueryPerformanceCounter(&current);
        const float dt = std::min(
            0.05f,
            static_cast<float>(
                static_cast<double>(
                    current.QuadPart - previous.QuadPart) /
                frequency.QuadPart));
        previous = current;
        if (running && !IsIconic(window)) {
            UpdateController(window);
            Update(dt);
            UpdateAudio(dt);
            Render(window);
        }
        LARGE_INTEGER frameEnd{};
        for (;;) {
            QueryPerformanceCounter(&frameEnd);
            const LONGLONG remaining =
                targetFrameTicks -
                (frameEnd.QuadPart - current.QuadPart);
            if (remaining <= 0) break;
            if (remaining > frequency.QuadPart / 1000)
                Sleep(1);
            else
                SwitchToThread();
        }
    }
    ShutdownAudio();
    DestroyBackBuffer();
    return 0;
}
