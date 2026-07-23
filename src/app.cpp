#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>

#include <algorithm>
#include <cstring>
#include <filesystem>

#include "audio.h"
#include "gameplay.h"
#include "rendering.h"
#include "state.h"
#include "world.h"

namespace game {
namespace {

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
            if (down && wParam == VK_F5) ReloadWorld(false);
            if (down && wParam == 'R') {
                if (ReloadWorld(true)) ResetAudio();
            }
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
            shooting = true;
            shotCooldown = kShotInterval;
            ShootToward(aimX, aimY);
            SetCapture(window);
            return 0;
        case WM_LBUTTONUP:
            shooting = false;
            ReleaseCapture();
            return 0;
        case WM_RBUTTONDOWN:
            aimX = GET_X_LPARAM(lParam);
            aimY = GET_Y_LPARAM(lParam);
            LaunchBomb(aimX, aimY);
            return 0;
        case WM_CAPTURECHANGED:
            shooting = false;
            return 0;
        case WM_KILLFOCUS:
            shooting = false;
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
    HINSTANCE instance, HINSTANCE, PWSTR, int showCommand) {
    using namespace game;
    SetProcessDPIAware();
    wchar_t executablePath[MAX_PATH]{};
    GetModuleFileNameW(nullptr, executablePath, MAX_PATH);
    const auto root =
        std::filesystem::path(executablePath).parent_path();
    goldenDirectory = root / "golden_scripts";
    gameDirectory = root / "game";

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

    ShowWindow(window, showCommand);
    LARGE_INTEGER frequency{}, previous{};
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&previous);
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
            Update(dt);
            UpdateAudio();
            Render(window);
        }
        Sleep(1);
    }
    ShutdownAudio();
    DestroyBackBuffer();
    return 0;
}
