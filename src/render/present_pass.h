#pragma once

#include "render/shader_program.h"

#include <array>

struct OverlayProjectile {
    float x = 0.0f;
    float y = 0.0f;
    float size = 0.0f;
    float alpha = 0.0f;
    float r = 1.0f;
    float g = 1.0f;
    float b = 1.0f;
};

class PresentPass {
public:
    static constexpr int kMaxOverlayProjectiles = 24;
    static constexpr int kStatGlyphCount = 11;

    bool initialize(const char* vertexShaderPath, const char* fragmentShaderPath);
    void shutdown();

    void draw(
        unsigned int sourceTexture,
        int windowW,
        int windowH,
        int internalW,
        int internalH,
        float fps,
        float frameMs,
        int weaponSlot,
        float weaponRecoil,
        float muzzleFlash,
        const std::array<OverlayProjectile, kMaxOverlayProjectiles>& projectiles,
        int projectileCount,
        int health,
        int ammo,
        int hudAnimTick);

private:
    bool loadHudTextures();

    ShaderProgram presentProgram_;
    unsigned int vao_ = 0;
    unsigned int hudTexture_ = 0;
    unsigned int weaponArrayTexture_ = 0;
    unsigned int flashArrayTexture_ = 0;
    unsigned int statGlyphArrayTexture_ = 0;
    unsigned int faceArrayTexture_ = 0;
    int hudW_ = 0;
    int hudH_ = 0;
    int weaponTexW_ = 0;
    int weaponTexH_ = 0;
    int weaponLayers_ = 0;
    int statGlyphTexW_ = 1;
    int statGlyphTexH_ = 1;
    int faceTexW_ = 1;
    int faceTexH_ = 1;
    int faceLayers_ = 0;
    int weaponSpriteW_[7] = {0, 0, 0, 0, 0, 0, 0};
    int weaponSpriteH_[7] = {0, 0, 0, 0, 0, 0, 0};
    int flashSpriteW_[7] = {0, 0, 0, 0, 0, 0, 0};
    int flashSpriteH_[7] = {0, 0, 0, 0, 0, 0, 0};
    int statGlyphW_[kStatGlyphCount] = {0};
    int statGlyphH_[kStatGlyphCount] = {0};
    int faceSpriteW_[8] = {0};
    int faceSpriteH_[8] = {0};
};
