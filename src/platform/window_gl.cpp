#include "platform/window_gl.h"

#include <glad/gl.h>

#include <iostream>

namespace {
constexpr int kGLMajor = 4;
constexpr int kGLMinor = 3;
}

bool WindowGL::initialize(int width, int height) {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::cerr << "SDL_Init failed: " << SDL_GetError() << '\n';
        return false;
    }

    if (!SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, kGLMajor) ||
        !SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, kGLMinor) ||
        !SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE) ||
        !SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1) ||
        !SDL_GL_SetAttribute(SDL_GL_ACCELERATED_VISUAL, 1)) {
        std::cerr << "SDL_GL_SetAttribute failed: " << SDL_GetError() << '\n';
        return false;
    }

    window_ = SDL_CreateWindow("rtboom", width, height, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    if (!window_) {
        std::cerr << "SDL_CreateWindow failed: " << SDL_GetError() << '\n';
        return false;
    }

    context_ = SDL_GL_CreateContext(window_);
    if (!context_) {
        std::cerr << "SDL_GL_CreateContext failed: " << SDL_GetError() << '\n';
        return false;
    }

    if (!SDL_GL_MakeCurrent(window_, context_)) {
        std::cerr << "SDL_GL_MakeCurrent failed: " << SDL_GetError() << '\n';
        return false;
    }

    if (!gladLoadGL((GLADloadfunc)SDL_GL_GetProcAddress)) {
        std::cerr << "gladLoadGL failed" << '\n';
        return false;
    }

    std::cout << "OpenGL renderer: " << glGetString(GL_RENDERER) << '\n';
    std::cout << "OpenGL version : " << glGetString(GL_VERSION) << '\n';

    GLint maxImageUnits = 0;
    GLint maxWorkGroupSizeX = 0;
    GLint maxWorkGroupSizeY = 0;
    glGetIntegerv(GL_MAX_IMAGE_UNITS, &maxImageUnits);
    glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_SIZE, 0, &maxWorkGroupSizeX);
    glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_SIZE, 1, &maxWorkGroupSizeY);

    std::cout << "Max image units: " << maxImageUnits << '\n';
    std::cout << "Max compute WG: " << maxWorkGroupSizeX << "x" << maxWorkGroupSizeY << '\n';

    if (!SDL_GL_SetSwapInterval(1)) {
        std::cerr << "Warning: vsync not enabled: " << SDL_GetError() << '\n';
    }

    return true;
}

void WindowGL::shutdown() {
    if (context_) {
        SDL_GL_DestroyContext(context_);
        context_ = nullptr;
    }
    if (window_) {
        SDL_DestroyWindow(window_);
        window_ = nullptr;
    }
    SDL_Quit();
}

void WindowGL::toggleFullscreenDesktop() {
    fullscreenDesktop_ = !fullscreenDesktop_;
    if (!SDL_SetWindowFullscreen(window_, fullscreenDesktop_)) {
        std::cerr << "SDL_SetWindowFullscreen failed: " << SDL_GetError() << '\n';
    }
}
