#include "render/present_pass.h"

#include "doom/hud_assets.h"

#include <glad/gl.h>

#include <algorithm>
#include <array>
#include <iostream>
#include <string>
#include <vector>

bool PresentPass::loadHudTextures() {
    doom::HudAssets assets{};
    std::string err;

    const char* wadCandidates[] = {
        "assets/freedoom2.wad",
        "assets/freedoom1.wad",
    };

    bool loaded = false;
    for (const char* wad : wadCandidates) {
        if (doom::loadHudAssetsFromWad(wad, assets, err)) {
            loaded = true;
            break;
        }
    }
    if (!loaded) {
        std::cerr << "Failed to load HUD assets: " << err << '\n';
        return false;
    }

    hudW_ = assets.statusBar.width;
    hudH_ = assets.statusBar.height;
    if (hudW_ <= 0 || hudH_ <= 0 || assets.statusBar.rgba.empty()) {
        return false;
    }

    glCreateTextures(GL_TEXTURE_2D, 1, &hudTexture_);
    glTextureStorage2D(hudTexture_, 1, GL_RGBA8, hudW_, hudH_);
    glTextureSubImage2D(
        hudTexture_, 0, 0, 0, hudW_, hudH_, GL_RGBA, GL_UNSIGNED_BYTE, assets.statusBar.rgba.data());
    glTextureParameteri(hudTexture_, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTextureParameteri(hudTexture_, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTextureParameteri(hudTexture_, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameteri(hudTexture_, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    weaponLayers_ = static_cast<int>(assets.weaponSprites.size());
    if (weaponLayers_ <= 0) {
        return false;
    }

    weaponTexW_ = 1;
    weaponTexH_ = 1;
    for (int i = 0; i < weaponLayers_ && i < 7; ++i) {
        weaponSpriteW_[i] = assets.weaponSprites[static_cast<std::size_t>(i)].width;
        weaponSpriteH_[i] = assets.weaponSprites[static_cast<std::size_t>(i)].height;
        if (i < static_cast<int>(assets.muzzleFlashSprites.size())) {
            flashSpriteW_[i] = assets.muzzleFlashSprites[static_cast<std::size_t>(i)].width;
            flashSpriteH_[i] = assets.muzzleFlashSprites[static_cast<std::size_t>(i)].height;
        }
        weaponTexW_ = std::max(weaponTexW_, weaponSpriteW_[i]);
        weaponTexH_ = std::max(weaponTexH_, weaponSpriteH_[i]);
    }

    std::vector<std::uint8_t> packed(
        static_cast<std::size_t>(weaponTexW_ * weaponTexH_ * weaponLayers_ * 4),
        0u);

    for (int layer = 0; layer < weaponLayers_ && layer < 7; ++layer) {
        const auto& src = assets.weaponSprites[static_cast<std::size_t>(layer)];
        for (int y = 0; y < src.height; ++y) {
            for (int x = 0; x < src.width; ++x) {
                const std::size_t si = static_cast<std::size_t>((y * src.width + x) * 4);
                const std::size_t di = static_cast<std::size_t>(
                    (((layer * weaponTexH_ + y) * weaponTexW_ + x) * 4));
                packed[di + 0] = src.rgba[si + 0];
                packed[di + 1] = src.rgba[si + 1];
                packed[di + 2] = src.rgba[si + 2];
                packed[di + 3] = src.rgba[si + 3];
            }
        }
    }

    glCreateTextures(GL_TEXTURE_2D_ARRAY, 1, &weaponArrayTexture_);
    glTextureStorage3D(weaponArrayTexture_, 1, GL_RGBA8, weaponTexW_, weaponTexH_, weaponLayers_);
    glTextureSubImage3D(
        weaponArrayTexture_,
        0,
        0,
        0,
        0,
        weaponTexW_,
        weaponTexH_,
        weaponLayers_,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        packed.data());
    glTextureParameteri(weaponArrayTexture_, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTextureParameteri(weaponArrayTexture_, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTextureParameteri(weaponArrayTexture_, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameteri(weaponArrayTexture_, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTextureParameteri(weaponArrayTexture_, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

    std::vector<std::uint8_t> flashPacked(
        static_cast<std::size_t>(weaponTexW_ * weaponTexH_ * weaponLayers_ * 4),
        0u);
    for (int layer = 0; layer < weaponLayers_ && layer < static_cast<int>(assets.muzzleFlashSprites.size()); ++layer) {
        const auto& src = assets.muzzleFlashSprites[static_cast<std::size_t>(layer)];
        for (int y = 0; y < src.height; ++y) {
            for (int x = 0; x < src.width; ++x) {
                const std::size_t si = static_cast<std::size_t>((y * src.width + x) * 4);
                const std::size_t di = static_cast<std::size_t>(
                    (((layer * weaponTexH_ + y) * weaponTexW_ + x) * 4));
                flashPacked[di + 0] = src.rgba[si + 0];
                flashPacked[di + 1] = src.rgba[si + 1];
                flashPacked[di + 2] = src.rgba[si + 2];
                flashPacked[di + 3] = src.rgba[si + 3];
            }
        }
    }

    glCreateTextures(GL_TEXTURE_2D_ARRAY, 1, &flashArrayTexture_);
    glTextureStorage3D(flashArrayTexture_, 1, GL_RGBA8, weaponTexW_, weaponTexH_, weaponLayers_);
    glTextureSubImage3D(
        flashArrayTexture_,
        0,
        0,
        0,
        0,
        weaponTexW_,
        weaponTexH_,
        weaponLayers_,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        flashPacked.data());
    glTextureParameteri(flashArrayTexture_, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTextureParameteri(flashArrayTexture_, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTextureParameteri(flashArrayTexture_, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameteri(flashArrayTexture_, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTextureParameteri(flashArrayTexture_, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

    if (static_cast<int>(assets.statGlyphs.size()) < kStatGlyphCount) {
        return false;
    }

    statGlyphTexW_ = 1;
    statGlyphTexH_ = 1;
    for (int i = 0; i < kStatGlyphCount; ++i) {
        const auto& g = assets.statGlyphs[static_cast<std::size_t>(i)];
        statGlyphW_[i] = g.width;
        statGlyphH_[i] = g.height;
        statGlyphTexW_ = std::max(statGlyphTexW_, statGlyphW_[i]);
        statGlyphTexH_ = std::max(statGlyphTexH_, statGlyphH_[i]);
    }

    std::vector<std::uint8_t> statPacked(
        static_cast<std::size_t>(statGlyphTexW_ * statGlyphTexH_ * kStatGlyphCount * 4),
        0u);

    for (int layer = 0; layer < kStatGlyphCount; ++layer) {
        const auto& src = assets.statGlyphs[static_cast<std::size_t>(layer)];
        for (int y = 0; y < src.height; ++y) {
            for (int x = 0; x < src.width; ++x) {
                const std::size_t si = static_cast<std::size_t>((y * src.width + x) * 4);
                const std::size_t di = static_cast<std::size_t>(
                    (((layer * statGlyphTexH_ + y) * statGlyphTexW_ + x) * 4));
                statPacked[di + 0] = src.rgba[si + 0];
                statPacked[di + 1] = src.rgba[si + 1];
                statPacked[di + 2] = src.rgba[si + 2];
                statPacked[di + 3] = src.rgba[si + 3];
            }
        }
    }

    glCreateTextures(GL_TEXTURE_2D_ARRAY, 1, &statGlyphArrayTexture_);
    glTextureStorage3D(statGlyphArrayTexture_, 1, GL_RGBA8, statGlyphTexW_, statGlyphTexH_, kStatGlyphCount);
    glTextureSubImage3D(
        statGlyphArrayTexture_,
        0,
        0,
        0,
        0,
        statGlyphTexW_,
        statGlyphTexH_,
        kStatGlyphCount,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        statPacked.data());
    glTextureParameteri(statGlyphArrayTexture_, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTextureParameteri(statGlyphArrayTexture_, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTextureParameteri(statGlyphArrayTexture_, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameteri(statGlyphArrayTexture_, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTextureParameteri(statGlyphArrayTexture_, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

    faceLayers_ = static_cast<int>(assets.faceSprites.size());
    if (faceLayers_ <= 0) {
        return false;
    }
    faceTexW_ = 1;
    faceTexH_ = 1;
    for (int i = 0; i < faceLayers_ && i < 8; ++i) {
        faceSpriteW_[i] = assets.faceSprites[static_cast<std::size_t>(i)].width;
        faceSpriteH_[i] = assets.faceSprites[static_cast<std::size_t>(i)].height;
        faceTexW_ = std::max(faceTexW_, faceSpriteW_[i]);
        faceTexH_ = std::max(faceTexH_, faceSpriteH_[i]);
    }

    std::vector<std::uint8_t> facePacked(
        static_cast<std::size_t>(faceTexW_ * faceTexH_ * faceLayers_ * 4),
        0u);

    for (int layer = 0; layer < faceLayers_; ++layer) {
        const auto& src = assets.faceSprites[static_cast<std::size_t>(layer)];
        for (int y = 0; y < src.height; ++y) {
            for (int x = 0; x < src.width; ++x) {
                const std::size_t si = static_cast<std::size_t>((y * src.width + x) * 4);
                const std::size_t di = static_cast<std::size_t>(
                    (((layer * faceTexH_ + y) * faceTexW_ + x) * 4));
                facePacked[di + 0] = src.rgba[si + 0];
                facePacked[di + 1] = src.rgba[si + 1];
                facePacked[di + 2] = src.rgba[si + 2];
                facePacked[di + 3] = src.rgba[si + 3];
            }
        }
    }

    glCreateTextures(GL_TEXTURE_2D_ARRAY, 1, &faceArrayTexture_);
    glTextureStorage3D(faceArrayTexture_, 1, GL_RGBA8, faceTexW_, faceTexH_, faceLayers_);
    glTextureSubImage3D(
        faceArrayTexture_,
        0,
        0,
        0,
        0,
        faceTexW_,
        faceTexH_,
        faceLayers_,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        facePacked.data());
    glTextureParameteri(faceArrayTexture_, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTextureParameteri(faceArrayTexture_, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTextureParameteri(faceArrayTexture_, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameteri(faceArrayTexture_, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTextureParameteri(faceArrayTexture_, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

    return true;
}

bool PresentPass::initialize(const char* vertexShaderPath, const char* fragmentShaderPath) {
    if (!presentProgram_.loadGraphicsFromFiles(vertexShaderPath, fragmentShaderPath)) {
        return false;
    }

    glCreateVertexArrays(1, &vao_);
    return loadHudTextures();
}

void PresentPass::shutdown() {
    if (faceArrayTexture_) {
        glDeleteTextures(1, &faceArrayTexture_);
        faceArrayTexture_ = 0;
    }
    if (statGlyphArrayTexture_) {
        glDeleteTextures(1, &statGlyphArrayTexture_);
        statGlyphArrayTexture_ = 0;
    }
    if (flashArrayTexture_) {
        glDeleteTextures(1, &flashArrayTexture_);
        flashArrayTexture_ = 0;
    }
    if (weaponArrayTexture_) {
        glDeleteTextures(1, &weaponArrayTexture_);
        weaponArrayTexture_ = 0;
    }
    if (hudTexture_) {
        glDeleteTextures(1, &hudTexture_);
        hudTexture_ = 0;
    }
    if (vao_) {
        glDeleteVertexArrays(1, &vao_);
        vao_ = 0;
    }
}

void PresentPass::draw(
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
    int hudAnimTick) {
    if (!presentProgram_.isValid() || !vao_) {
        return;
    }

    glViewport(0, 0, windowW, windowH);
    glClearColor(0.02f, 0.02f, 0.03f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    const int scaleX = windowW / internalW;
    const int scaleY = windowH / internalH;
    const int integerScale = std::min(scaleX, scaleY);

    int drawW = internalW;
    int drawH = internalH;

    if (integerScale >= 1) {
        drawW = internalW * integerScale;
        drawH = internalH * integerScale;
    } else {
        const float fitScale = std::min(windowW / static_cast<float>(internalW), windowH / static_cast<float>(internalH));
        drawW = std::max(1, static_cast<int>(internalW * fitScale));
        drawH = std::max(1, static_cast<int>(internalH * fitScale));
    }

    const int offsetX = (windowW - drawW) / 2;
    const int offsetY = (windowH - drawH) / 2;
    glViewport(offsetX, offsetY, drawW, drawH);

    presentProgram_.use();

    glBindVertexArray(vao_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, sourceTexture);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, hudTexture_);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D_ARRAY, weaponArrayTexture_);
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D_ARRAY, flashArrayTexture_);
    glActiveTexture(GL_TEXTURE4);
    glBindTexture(GL_TEXTURE_2D_ARRAY, statGlyphArrayTexture_);
    glActiveTexture(GL_TEXTURE5);
    glBindTexture(GL_TEXTURE_2D_ARRAY, faceArrayTexture_);

    const GLint samplerLoc = glGetUniformLocation(presentProgram_.id(), "uSource");
    const GLint hudSamplerLoc = glGetUniformLocation(presentProgram_.id(), "uHudTex");
    const GLint weaponSamplerLoc = glGetUniformLocation(presentProgram_.id(), "uWeaponTex");
    const GLint flashSamplerLoc = glGetUniformLocation(presentProgram_.id(), "uFlashTex");
    const GLint statGlyphSamplerLoc = glGetUniformLocation(presentProgram_.id(), "uStatGlyphTex");
    const GLint faceSamplerLoc = glGetUniformLocation(presentProgram_.id(), "uFaceTex");
    const GLint fpsLoc = glGetUniformLocation(presentProgram_.id(), "uFps");
    const GLint frameMsLoc = glGetUniformLocation(presentProgram_.id(), "uFrameMs");
    const GLint windowSizeLoc = glGetUniformLocation(presentProgram_.id(), "uWindowSize");
    const GLint hudSizeLoc = glGetUniformLocation(presentProgram_.id(), "uHudSize");
    const GLint weaponLayerLoc = glGetUniformLocation(presentProgram_.id(), "uWeaponLayer");
    const GLint weaponSizeLoc = glGetUniformLocation(presentProgram_.id(), "uWeaponSize");
    const GLint weaponRecoilLoc = glGetUniformLocation(presentProgram_.id(), "uWeaponRecoil");
    const GLint muzzleFlashLoc = glGetUniformLocation(presentProgram_.id(), "uMuzzleFlash");
    const GLint flashSizeLoc = glGetUniformLocation(presentProgram_.id(), "uFlashSize");
    const GLint projectileCountLoc = glGetUniformLocation(presentProgram_.id(), "uProjectileCount");
    const GLint projectileDataLoc = glGetUniformLocation(presentProgram_.id(), "uProjectiles");
    const GLint projectileColorLoc = glGetUniformLocation(presentProgram_.id(), "uProjectileColors");
    const GLint statGlyphSizeLoc = glGetUniformLocation(presentProgram_.id(), "uStatGlyphSize");
    const GLint statGlyphCellLoc = glGetUniformLocation(presentProgram_.id(), "uStatGlyphCell");
    const GLint healthLoc = glGetUniformLocation(presentProgram_.id(), "uHealth");
    const GLint ammoLoc = glGetUniformLocation(presentProgram_.id(), "uAmmo");
    const GLint hudAnimTickLoc = glGetUniformLocation(presentProgram_.id(), "uHudAnimTick");
    const GLint faceLayerCountLoc = glGetUniformLocation(presentProgram_.id(), "uFaceLayerCount");
    const GLint faceSizeLoc = glGetUniformLocation(presentProgram_.id(), "uFaceSize");
    glUniform1i(samplerLoc, 0);
    glUniform1i(hudSamplerLoc, 1);
    glUniform1i(weaponSamplerLoc, 2);
    glUniform1i(flashSamplerLoc, 3);
    glUniform1i(statGlyphSamplerLoc, 4);
    glUniform1i(faceSamplerLoc, 5);
    glUniform1f(fpsLoc, fps);
    glUniform1f(frameMsLoc, frameMs);
    glUniform2f(windowSizeLoc, static_cast<float>(windowW), static_cast<float>(windowH));
    glUniform2i(hudSizeLoc, hudW_, hudH_);

    int clampedWeapon = std::clamp(weaponSlot, 0, weaponLayers_ - 1);
    int weaponW = weaponSpriteW_[clampedWeapon];
    int weaponH = weaponSpriteH_[clampedWeapon];
    if (weaponW <= 0 || weaponH <= 0) {
        weaponW = weaponTexW_;
        weaponH = weaponTexH_;
    }
    glUniform1i(weaponLayerLoc, clampedWeapon);
    glUniform2i(weaponSizeLoc, weaponW, weaponH);
    glUniform1f(weaponRecoilLoc, std::clamp(weaponRecoil, 0.0f, 1.0f));
    int flashW = flashSpriteW_[clampedWeapon];
    int flashH = flashSpriteH_[clampedWeapon];
    if (flashW <= 0 || flashH <= 0) {
        flashW = 1;
        flashH = 1;
    }
    glUniform2i(flashSizeLoc, flashW, flashH);
    glUniform1f(muzzleFlashLoc, std::clamp(muzzleFlash, 0.0f, 1.0f));

    const int clampedProjectileCount = std::clamp(projectileCount, 0, kMaxOverlayProjectiles);
    std::array<float, kMaxOverlayProjectiles * 4> projectileData{};
    std::array<float, kMaxOverlayProjectiles * 4> projectileColor{};
    for (int i = 0; i < clampedProjectileCount; ++i) {
        projectileData[static_cast<std::size_t>(i * 4 + 0)] = projectiles[static_cast<std::size_t>(i)].x;
        projectileData[static_cast<std::size_t>(i * 4 + 1)] = projectiles[static_cast<std::size_t>(i)].y;
        projectileData[static_cast<std::size_t>(i * 4 + 2)] = projectiles[static_cast<std::size_t>(i)].size;
        projectileData[static_cast<std::size_t>(i * 4 + 3)] = projectiles[static_cast<std::size_t>(i)].alpha;
        projectileColor[static_cast<std::size_t>(i * 4 + 0)] = projectiles[static_cast<std::size_t>(i)].r;
        projectileColor[static_cast<std::size_t>(i * 4 + 1)] = projectiles[static_cast<std::size_t>(i)].g;
        projectileColor[static_cast<std::size_t>(i * 4 + 2)] = projectiles[static_cast<std::size_t>(i)].b;
        projectileColor[static_cast<std::size_t>(i * 4 + 3)] = 1.0f;
    }
    glUniform1i(projectileCountLoc, clampedProjectileCount);
    glUniform4fv(projectileDataLoc, clampedProjectileCount, projectileData.data());
    glUniform4fv(projectileColorLoc, clampedProjectileCount, projectileColor.data());

    std::array<int, kStatGlyphCount * 2> statGlyphSize{};
    for (int i = 0; i < kStatGlyphCount; ++i) {
        statGlyphSize[static_cast<std::size_t>(i * 2 + 0)] = statGlyphW_[i];
        statGlyphSize[static_cast<std::size_t>(i * 2 + 1)] = statGlyphH_[i];
    }
    glUniform2iv(statGlyphSizeLoc, kStatGlyphCount, statGlyphSize.data());
    glUniform2i(statGlyphCellLoc, statGlyphTexW_, statGlyphTexH_);
    glUniform1i(healthLoc, std::clamp(health, 0, 999));
    glUniform1i(ammoLoc, std::clamp(ammo, 0, 999));
    glUniform1i(hudAnimTickLoc, std::max(0, hudAnimTick));
    glUniform1i(faceLayerCountLoc, faceLayers_);
    glUniform2i(faceSizeLoc, faceTexW_, faceTexH_);

    glDrawArrays(GL_TRIANGLES, 0, 3);

    glBindVertexArray(0);
}
