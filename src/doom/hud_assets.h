#pragma once

#include "doom/wad_loader.h"

#include <string>
#include <vector>

namespace doom {

struct HudAssets {
    RgbaTexture statusBar;
    std::vector<RgbaTexture> weaponSprites;
    std::vector<RgbaTexture> muzzleFlashSprites;
    std::vector<RgbaTexture> statGlyphs;
    std::vector<RgbaTexture> faceSprites;
};

bool loadHudAssetsFromWad(const std::string& wadPath, HudAssets& outAssets, std::string& outError);

}  // namespace doom
