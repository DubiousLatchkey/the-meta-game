#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace game {

bool InitializeBackBuffer(HWND window);
void ResizeBackBuffer(int width, int height);
void DestroyBackBuffer();
void ClientToGameCoordinates(
    HWND window, int clientX, int clientY, int& gameX, int& gameY);
void Render(HWND window);

}  // namespace game
