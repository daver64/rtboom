#include "platform/window_gl.h"
#include "doom/sound_assets.h"
#include "render/compute_renderer.h"
#include "render/present_pass.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <vector>

namespace {
constexpr int kInternalW = 320;
constexpr int kInternalH = 200;

struct ShotProjectile {
    float x = 0.5f;
    float y = 0.56f;
    float size = 3.0f;
    float shrink = 8.0f;
    float life = 0.2f;
    float maxLife = 0.2f;
    float r = 1.0f;
    float g = 0.9f;
    float b = 0.6f;
};

struct WeaponFireProfile {
    float cooldown = 0.2f;
    bool repeatWhileHeld = true;
};

WeaponFireProfile fireProfileForSlot(int weaponSlot) {
    switch (weaponSlot) {
        case 0:
            return WeaponFireProfile{0.18f, true};   // fist/chainsaw-like
        case 1:
            return WeaponFireProfile{0.24f, false};  // pistol-style single tap
        case 2:
            return WeaponFireProfile{0.95f, false};  // shotgun pump delay
        case 3:
            return WeaponFireProfile{0.10f, true};   // chaingun rapid repeat
        case 4:
            return WeaponFireProfile{0.85f, false};  // rocket launcher reload
        case 5:
            return WeaponFireProfile{0.12f, true};   // plasma rapid repeat
        case 6:
            return WeaponFireProfile{1.35f, false};  // BFG wind-up/reload
        default:
            return WeaponFireProfile{};
    }
}

int ammoCostForSlot(int weaponSlot) {
    switch (weaponSlot) {
        case 0:
            return 0;
        case 1:
            return 1;
        case 2:
            return 1;
        case 3:
            return 1;
        case 4:
            return 1;
        case 5:
            return 1;
        case 6:
            return 40;
        default:
            return 1;
    }
}

struct MusicState {
    int sampleRate = 22050;
    std::uint64_t frame = 0;
    double leadPhase = 0.0;
    double bassPhase = 0.0;
    std::vector<std::int16_t> scratch;
};

double midiToHz(int midi) {
    return 440.0 * std::pow(2.0, (static_cast<double>(midi) - 69.0) / 12.0);
}

void SDLCALL musicStreamCallback(void* userdata, SDL_AudioStream* stream, int additional_amount, int total_amount) {
    (void)total_amount;
    auto* state = static_cast<MusicState*>(userdata);
    if (!state || additional_amount <= 0) {
        return;
    }

    const int samplesRequested = additional_amount / static_cast<int>(sizeof(std::int16_t));
    if (samplesRequested <= 0) {
        return;
    }

    state->scratch.resize(static_cast<std::size_t>(samplesRequested));
    const int melody[8] = {64, 67, 71, 72, 71, 67, 64, 62};
    const int bass[4] = {40, 40, 43, 38};
    const int stepSamples = state->sampleRate / 4;
    const int bassStepSamples = state->sampleRate / 2;

    for (int i = 0; i < samplesRequested; ++i) {
        const int melStep = static_cast<int>((state->frame / static_cast<std::uint64_t>(stepSamples)) % 8u);
        const int bassStep = static_cast<int>((state->frame / static_cast<std::uint64_t>(bassStepSamples)) % 4u);
        const double fLead = midiToHz(melody[melStep]);
        const double fBass = midiToHz(bass[bassStep]);

        state->leadPhase += (2.0 * 3.14159265358979323846 * fLead) / static_cast<double>(state->sampleRate);
        state->bassPhase += (2.0 * 3.14159265358979323846 * fBass) / static_cast<double>(state->sampleRate);
        if (state->leadPhase > 2.0 * 3.14159265358979323846) {
            state->leadPhase -= 2.0 * 3.14159265358979323846;
        }
        if (state->bassPhase > 2.0 * 3.14159265358979323846) {
            state->bassPhase -= 2.0 * 3.14159265358979323846;
        }

        const double lead = std::sin(state->leadPhase) * 0.13;
        const double bassWave = (std::sin(state->bassPhase) > 0.0 ? 1.0 : -1.0) * 0.06;
        const double mix = std::clamp((lead + bassWave) * 0.25, -0.95, 0.95);
        state->scratch[static_cast<std::size_t>(i)] = static_cast<std::int16_t>(mix * 32767.0);
        state->frame++;
    }

    SDL_PutAudioStreamData(stream, state->scratch.data(), samplesRequested * static_cast<int>(sizeof(std::int16_t)));
}

}

int main() {
    WindowGL window;
    if (!window.initialize(1280, 800)) {
        return 1;
    }

    ComputeRenderer compute;
    if (!compute.initialize(kInternalW, kInternalH, "shaders/raytrace.comp")) {
        std::cerr << "Failed to initialize compute renderer" << '\n';
        window.shutdown();
        return 1;
    }

    PresentPass present;
    if (!present.initialize("shaders/present.vert", "shaders/present.frag")) {
        std::cerr << "Failed to initialize present pass" << '\n';
        compute.shutdown();
        window.shutdown();
        return 1;
    }

    if (!SDL_SetWindowRelativeMouseMode(window.window(), true)) {
        std::cerr << "Warning: failed to enable relative mouse mode: " << SDL_GetError() << '\n';
    }

    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        std::cerr << "Warning: failed to initialize audio subsystem: " << SDL_GetError() << '\n';
    }

    std::vector<doom::PcmSound> fireSounds;
    std::string fireSoundError;
    if (!doom::loadWeaponFireSoundsFromWad("assets/freedoom1.wad", fireSounds, fireSoundError)) {
        if (!doom::loadWeaponFireSoundsFromWad("assets/freedoom2.wad", fireSounds, fireSoundError)) {
            fireSounds.resize(7);
            for (std::size_t i = 0; i < fireSounds.size(); ++i) {
                doom::makeFallbackFireSound(fireSounds[i]);
            }
            std::cerr << "Warning: using fallback fire sound: " << fireSoundError << '\n';
        }
    }
    for (auto& snd : fireSounds) {
        for (auto& s : snd.samples) {
            s = static_cast<std::int16_t>(static_cast<int>(s) / 4);
        }
    }

    SDL_AudioStream* fireAudioStream = nullptr;
    SDL_AudioStream* musicAudioStream = nullptr;
    SDL_AudioSpec fireSpec{};
    fireSpec.format = SDL_AUDIO_S16;
    fireSpec.channels = 1;
    fireSpec.freq = 22050;
    fireAudioStream =
        SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &fireSpec, nullptr, nullptr);
    if (!fireAudioStream) {
        std::cerr << "Warning: failed to open audio stream: " << SDL_GetError() << '\n';
    } else if (!SDL_ResumeAudioStreamDevice(fireAudioStream)) {
        std::cerr << "Warning: failed to start audio stream: " << SDL_GetError() << '\n';
        SDL_DestroyAudioStream(fireAudioStream);
        fireAudioStream = nullptr;
    }

    MusicState musicState{};
    musicState.sampleRate = fireSpec.freq;
    musicAudioStream = SDL_OpenAudioDeviceStream(
        SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,
        &fireSpec,
        musicStreamCallback,
        &musicState);
    if (!musicAudioStream) {
        std::cerr << "Warning: failed to open music stream: " << SDL_GetError() << '\n';
    } else if (!SDL_ResumeAudioStreamDevice(musicAudioStream)) {
        std::cerr << "Warning: failed to start music stream: " << SDL_GetError() << '\n';
        SDL_DestroyAudioStream(musicAudioStream);
        musicAudioStream = nullptr;
    }

    const Uint64 perfFreq = SDL_GetPerformanceFrequency();
    const Uint64 startCounter = SDL_GetPerformanceCounter();
    Uint64 prevCounter = startCounter;
    double avgFrameMs = 16.6;
    double avgFps = 60.0;

    float camX = compute.spawnX();
    float camY = 1.15f;
    float camZ = compute.spawnZ();
    float yaw = 0.0f;
    float pitch = 0.02f;
    int health = 100;
    int ammo = 200;
    int weaponSlot = 1;
    bool fireHeld = false;
    bool firePressed = false;
    float fireCooldown = 0.0f;
    float weaponRecoil = 0.0f;
    float muzzleFlash = 0.0f;
    std::vector<ShotProjectile> shotProjectiles;
    shotProjectiles.reserve(64);

    constexpr float kMouseSensitivity = 0.0024f;
    constexpr float kMoveSpeed = 4.5f;
    constexpr float kPitchLimit = 1.52f;

    bool running = true;
    while (running) {
        float mouseDX = 0.0f;
        float mouseDY = 0.0f;
        firePressed = false;

        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_EVENT_QUIT) {
                running = false;
            }
            if (e.type == SDL_EVENT_MOUSE_MOTION && SDL_GetWindowRelativeMouseMode(window.window())) {
                mouseDX += e.motion.xrel;
                mouseDY += e.motion.yrel;
            }
            if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_LEFT) {
                fireHeld = true;
                firePressed = true;
            }
            if (e.type == SDL_EVENT_MOUSE_BUTTON_UP && e.button.button == SDL_BUTTON_LEFT) {
                fireHeld = false;
            }
            if (e.type == SDL_EVENT_KEY_DOWN) {
                if (e.key.key == SDLK_ESCAPE) {
                    running = false;
                }
                if (e.key.key == SDLK_F11) {
                    window.toggleFullscreenDesktop();
                }
                if (e.key.key == SDLK_TAB) {
                    const bool nowRelative = !SDL_GetWindowRelativeMouseMode(window.window());
                    if (!SDL_SetWindowRelativeMouseMode(window.window(), nowRelative)) {
                        std::cerr << "Warning: failed to toggle relative mouse mode: " << SDL_GetError() << '\n';
                    }
                }
                if (e.key.key == SDLK_1) {
                    weaponSlot = 0;
                }
                if (e.key.key == SDLK_2) {
                    weaponSlot = 1;
                }
                if (e.key.key == SDLK_3) {
                    weaponSlot = 2;
                }
                if (e.key.key == SDLK_4) {
                    weaponSlot = 3;
                }
                if (e.key.key == SDLK_5) {
                    weaponSlot = 4;
                }
                if (e.key.key == SDLK_6) {
                    weaponSlot = 5;
                }
                if (e.key.key == SDLK_7) {
                    weaponSlot = 6;
                }
            }
        }

        const Uint64 nowCounter = SDL_GetPerformanceCounter();
        const double dt = static_cast<double>(nowCounter - prevCounter) / static_cast<double>(perfFreq);
        prevCounter = nowCounter;

        const double clampedDt = (dt > 0.25) ? 0.25 : dt;
        const double timeSeconds =
            static_cast<double>(nowCounter - startCounter) / static_cast<double>(perfFreq);

        const double frameMs = clampedDt * 1000.0;
        const double fps = clampedDt > 0.0 ? (1.0 / clampedDt) : 0.0;
        constexpr double smoothing = 0.12;
        avgFrameMs += (frameMs - avgFrameMs) * smoothing;
        avgFps += (fps - avgFps) * smoothing;

        fireCooldown = std::max(0.0f, fireCooldown - static_cast<float>(clampedDt));
        weaponRecoil = std::max(0.0f, weaponRecoil - static_cast<float>(clampedDt) * 8.0f);
        muzzleFlash = std::max(0.0f, muzzleFlash - static_cast<float>(clampedDt) * 14.0f);
        const WeaponFireProfile fireProfile = fireProfileForSlot(weaponSlot);
        const int ammoCost = ammoCostForSlot(weaponSlot);
        const bool hasAmmo = ammoCost <= ammo;
        const bool wantFire =
            fireCooldown <= 0.0f && hasAmmo &&
            ((fireProfile.repeatWhileHeld && fireHeld) || (!fireProfile.repeatWhileHeld && firePressed));
        if (wantFire) {
            fireCooldown = fireProfile.cooldown;
            weaponRecoil = 1.0f;
            muzzleFlash = 1.0f;
            ammo = std::max(0, ammo - ammoCost);
            const std::size_t soundIndex = static_cast<std::size_t>(std::max(0, std::min(weaponSlot, 6)));
            if (fireAudioStream && soundIndex < fireSounds.size() && !fireSounds[soundIndex].samples.empty()) {
                const doom::PcmSound& fireSound = fireSounds[soundIndex];
                SDL_ClearAudioStream(fireAudioStream);
                if (!SDL_PutAudioStreamData(
                        fireAudioStream,
                        fireSound.samples.data(),
                        static_cast<int>(fireSound.samples.size() * sizeof(std::int16_t)))) {
                    std::cerr << "Warning: failed to queue weapon sound: " << SDL_GetError() << '\n';
                }
            }

            auto spawnProjectile = [&](float xOffset, float size, float shrink, float life, float r, float g, float b) {
                ShotProjectile p{};
                p.x = 0.5f + xOffset;
                p.y = 0.56f;
                p.size = size;
                p.shrink = shrink;
                p.life = life;
                p.maxLife = life;
                p.r = r;
                p.g = g;
                p.b = b;
                shotProjectiles.push_back(p);
            };

            const int shotCycle = static_cast<int>((timeSeconds * 100.0));
            if (weaponSlot == 2) {
                for (int i = 0; i < 6; ++i) {
                    const float spread = static_cast<float>(i - 3) * 0.055f;
                    spawnProjectile(spread, 2.3f, 3.0f, 0.45f, 1.0f, 0.85f, 0.5f);
                }
            } else if (weaponSlot == 4) {
                spawnProjectile(0.0f, 5.4f, 1.7f, 1.00f, 1.0f, 0.45f, 0.22f);
            } else if (weaponSlot == 5) {
                const float j = ((shotCycle % 5) - 2) * 0.018f;
                spawnProjectile(j, 3.4f, 2.6f, 0.65f, 0.2f, 0.95f, 1.0f);
            } else if (weaponSlot == 6) {
                spawnProjectile(0.0f, 7.2f, 1.5f, 1.20f, 0.35f, 1.0f, 0.35f);
            } else {
                const float j = ((shotCycle % 7) - 3) * 0.012f;
                spawnProjectile(j, 2.1f, 3.4f, 0.45f, 1.0f, 0.92f, 0.66f);
            }
        }

        for (auto& p : shotProjectiles) {
            p.life -= static_cast<float>(clampedDt);
            p.size = std::max(0.8f, p.size - p.shrink * static_cast<float>(clampedDt));
        }
        shotProjectiles.erase(
            std::remove_if(
                shotProjectiles.begin(),
                shotProjectiles.end(),
                [](const ShotProjectile& p) { return p.life <= 0.0f || p.x < -0.2f || p.x > 1.2f; }),
            shotProjectiles.end());

        yaw += mouseDX * kMouseSensitivity;
        pitch -= mouseDY * kMouseSensitivity;
        if (pitch > kPitchLimit) {
            pitch = kPitchLimit;
        }
        if (pitch < -kPitchLimit) {
            pitch = -kPitchLimit;
        }

        const float cp = std::cos(pitch);
        const float sp = std::sin(pitch);
        const float sy = std::sin(yaw);
        const float cy = std::cos(yaw);

        float forwardX = cp * sy;
        float forwardY = sp;
        float forwardZ = cp * cy;

        const float forwardLen = std::sqrt(forwardX * forwardX + forwardY * forwardY + forwardZ * forwardZ);
        if (forwardLen > 0.0f) {
            forwardX /= forwardLen;
            forwardY /= forwardLen;
            forwardZ /= forwardLen;
        }

        float rightX = cy;
        float rightY = 0.0f;
        float rightZ = -sy;

        const float rightLen = std::sqrt(rightX * rightX + rightY * rightY + rightZ * rightZ);
        if (rightLen > 0.0f) {
            rightX /= rightLen;
            rightY /= rightLen;
            rightZ /= rightLen;
        }

        float upX = forwardY * rightZ - forwardZ * rightY;
        float upY = forwardZ * rightX - forwardX * rightZ;
        float upZ = forwardX * rightY - forwardY * rightX;

        const float upLen = std::sqrt(upX * upX + upY * upY + upZ * upZ);
        if (upLen > 0.0f) {
            upX /= upLen;
            upY /= upLen;
            upZ /= upLen;
        }

        float moveForwardX = forwardX;
        float moveForwardZ = forwardZ;
        const float moveForwardLen = std::sqrt(moveForwardX * moveForwardX + moveForwardZ * moveForwardZ);
        if (moveForwardLen > 0.0f) {
            moveForwardX /= moveForwardLen;
            moveForwardZ /= moveForwardLen;
        }

        float wishX = 0.0f;
        float wishZ = 0.0f;

        const bool* keys = SDL_GetKeyboardState(nullptr);
        if (keys[SDL_SCANCODE_W]) {
            wishX += moveForwardX;
            wishZ += moveForwardZ;
        }
        if (keys[SDL_SCANCODE_S]) {
            wishX -= moveForwardX;
            wishZ -= moveForwardZ;
        }
        if (keys[SDL_SCANCODE_D]) {
            wishX += rightX;
            wishZ += rightZ;
        }
        if (keys[SDL_SCANCODE_A]) {
            wishX -= rightX;
            wishZ -= rightZ;
        }

        const float wishLen = std::sqrt(wishX * wishX + wishZ * wishZ);
        if (wishLen > 0.0f) {
            wishX /= wishLen;
            wishZ /= wishLen;
        }

        const float moveStep = kMoveSpeed * static_cast<float>(clampedDt);
        const float targetX = camX + wishX * moveStep;
        const float targetZ = camZ + wishZ * moveStep;

        constexpr float kPlayerRadius = 0.22f;
        auto isBlocked = [&](float x, float z) {
            return compute.isWallAt(x - kPlayerRadius, z - kPlayerRadius) ||
                   compute.isWallAt(x + kPlayerRadius, z - kPlayerRadius) ||
                   compute.isWallAt(x - kPlayerRadius, z + kPlayerRadius) ||
                   compute.isWallAt(x + kPlayerRadius, z + kPlayerRadius);
        };

        if (!isBlocked(targetX, camZ)) {
            camX = targetX;
        }
        if (!isBlocked(camX, targetZ)) {
            camZ = targetZ;
        }

        CameraParams camera{};
        camera.pos[0] = camX;
        camera.pos[1] = camY;
        camera.pos[2] = camZ;
        camera.forward[0] = forwardX;
        camera.forward[1] = forwardY;
        camera.forward[2] = forwardZ;
        camera.right[0] = rightX;
        camera.right[1] = rightY;
        camera.right[2] = rightZ;
        camera.up[0] = upX;
        camera.up[1] = upY;
        camera.up[2] = upZ;
        camera.fovScale = 0.95f;

        compute.render(static_cast<float>(timeSeconds), camera);

        int windowW = 0;
        int windowH = 0;
        SDL_GetWindowSizeInPixels(window.window(), &windowW, &windowH);

        std::array<OverlayProjectile, PresentPass::kMaxOverlayProjectiles> overlayProjectiles{};
        int overlayCount = std::min(
            static_cast<int>(shotProjectiles.size()),
            static_cast<int>(PresentPass::kMaxOverlayProjectiles));
        for (int i = 0; i < overlayCount; ++i) {
            const ShotProjectile& src = shotProjectiles[static_cast<std::size_t>(i)];
            OverlayProjectile dst{};
            dst.x = src.x;
            dst.y = src.y;
            dst.size = src.size;
            dst.alpha = std::clamp(src.life / std::max(0.001f, src.maxLife), 0.0f, 1.0f);
            dst.r = src.r;
            dst.g = src.g;
            dst.b = src.b;
            overlayProjectiles[static_cast<std::size_t>(i)] = dst;
        }

        present.draw(
            compute.outputTexture(),
            windowW,
            windowH,
            kInternalW,
            kInternalH,
            static_cast<float>(avgFps),
            static_cast<float>(avgFrameMs),
            weaponSlot,
            weaponRecoil,
            muzzleFlash,
            overlayProjectiles,
            overlayCount,
            health,
            ammo,
            static_cast<int>(timeSeconds * 14.0));
        SDL_GL_SwapWindow(window.window());
    }

    if (fireAudioStream) {
        SDL_DestroyAudioStream(fireAudioStream);
        fireAudioStream = nullptr;
    }
    if (musicAudioStream) {
        SDL_DestroyAudioStream(musicAudioStream);
        musicAudioStream = nullptr;
    }

    present.shutdown();
    compute.shutdown();
    window.shutdown();

    return 0;
}
