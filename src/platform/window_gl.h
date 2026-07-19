#pragma once

#include <SDL3/SDL.h>

class WindowGL {
public:
    bool initialize(int width, int height);
    void shutdown();

    SDL_Window* window() const { return window_; }
    bool isRunning() const { return running_; }

    void requestQuit() { running_ = false; }
    void toggleFullscreenDesktop();

private:
    SDL_Window* window_ = nullptr;
    SDL_GLContext context_ = nullptr;
    bool running_ = true;
    bool fullscreenDesktop_ = false;
};
