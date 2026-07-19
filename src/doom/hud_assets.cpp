#include "doom/hud_assets.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace doom {
namespace {

struct LumpInfo {
    std::int32_t offset = 0;
    std::int32_t size = 0;
    std::string name;
};

std::uint16_t readU16(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    return static_cast<std::uint16_t>(bytes[offset]) |
           (static_cast<std::uint16_t>(bytes[offset + 1]) << 8u);
}

std::int16_t readS16(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    return static_cast<std::int16_t>(readU16(bytes, offset));
}

std::uint32_t readU32(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    return static_cast<std::uint32_t>(bytes[offset]) |
           (static_cast<std::uint32_t>(bytes[offset + 1]) << 8u) |
           (static_cast<std::uint32_t>(bytes[offset + 2]) << 16u) |
           (static_cast<std::uint32_t>(bytes[offset + 3]) << 24u);
}

std::int32_t readS32(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    return static_cast<std::int32_t>(readU32(bytes, offset));
}

std::string normalizeName(const std::string& s) {
    std::string out;
    out.reserve(8);
    for (char c : s) {
        if (c == '\0') {
            break;
        }
        char up = (c >= 'a' && c <= 'z') ? static_cast<char>(c - ('a' - 'A')) : c;
        out.push_back(up);
    }
    return out;
}

std::string readLumpName(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    std::string out;
    out.reserve(8);
    for (int i = 0; i < 8; ++i) {
        char c = static_cast<char>(bytes[offset + static_cast<std::size_t>(i)]);
        if (c == '\0') {
            break;
        }
        out.push_back(c);
    }
    return normalizeName(out);
}

bool loadFile(const std::string& path, std::vector<std::uint8_t>& outBytes) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        return false;
    }
    f.seekg(0, std::ios::end);
    const std::streamsize size = f.tellg();
    if (size <= 0) {
        return false;
    }
    f.seekg(0, std::ios::beg);
    outBytes.resize(static_cast<std::size_t>(size));
    f.read(reinterpret_cast<char*>(outBytes.data()), size);
    return static_cast<std::streamsize>(f.gcount()) == size;
}

bool parseDirectory(
    const std::vector<std::uint8_t>& wad,
    std::vector<LumpInfo>& lumps,
    std::unordered_map<std::string, int>& firstLumpByName,
    std::string& outError) {
    if (wad.size() < 12u) {
        outError = "WAD too small";
        return false;
    }

    const std::string id(reinterpret_cast<const char*>(wad.data()), 4);
    if (id != "IWAD" && id != "PWAD") {
        outError = "Invalid WAD magic";
        return false;
    }

    const std::int32_t numLumps = readS32(wad, 4);
    const std::int32_t dirOffset = readS32(wad, 8);
    if (numLumps <= 0 || dirOffset < 0) {
        outError = "Invalid WAD header values";
        return false;
    }

    const std::size_t dirBytes = static_cast<std::size_t>(numLumps) * 16u;
    if (static_cast<std::size_t>(dirOffset) + dirBytes > wad.size()) {
        outError = "WAD directory out of bounds";
        return false;
    }

    lumps.clear();
    lumps.reserve(static_cast<std::size_t>(numLumps));
    firstLumpByName.clear();

    for (std::int32_t i = 0; i < numLumps; ++i) {
        const std::size_t base = static_cast<std::size_t>(dirOffset) + static_cast<std::size_t>(i) * 16u;
        LumpInfo lump{};
        lump.offset = readS32(wad, base + 0u);
        lump.size = readS32(wad, base + 4u);
        lump.name = readLumpName(wad, base + 8u);
        if (lump.offset < 0 || lump.size < 0) {
            outError = "WAD has negative lump bounds";
            return false;
        }
        if (static_cast<std::size_t>(lump.offset) + static_cast<std::size_t>(lump.size) > wad.size()) {
            outError = "WAD lump out of bounds";
            return false;
        }
        if (!lump.name.empty() && firstLumpByName.find(lump.name) == firstLumpByName.end()) {
            firstLumpByName[lump.name] = i;
        }
        lumps.push_back(lump);
    }

    return true;
}

bool readFirstPalette(
    const std::vector<std::uint8_t>& wad,
    const std::vector<LumpInfo>& lumps,
    const std::unordered_map<std::string, int>& firstLumpByName,
    std::array<std::uint8_t, 768>& outPalette) {
    auto it = firstLumpByName.find("PLAYPAL");
    if (it == firstLumpByName.end()) {
        return false;
    }
    const LumpInfo& lump = lumps[static_cast<std::size_t>(it->second)];
    if (lump.size < 768) {
        return false;
    }
    std::size_t base = static_cast<std::size_t>(lump.offset);
    for (std::size_t i = 0; i < 768u; ++i) {
        outPalette[i] = wad[base + i];
    }
    return true;
}

bool decodePatchLump(
    const std::vector<std::uint8_t>& wad,
    const LumpInfo& lump,
    const std::array<std::uint8_t, 768>& palette,
    RgbaTexture& outTex) {
    if (lump.size < 8) {
        return false;
    }

    const std::size_t base = static_cast<std::size_t>(lump.offset);
    const std::int16_t patchW = readS16(wad, base + 0u);
    const std::int16_t patchH = readS16(wad, base + 2u);
    if (patchW <= 0 || patchH <= 0 || patchW > 1024 || patchH > 1024) {
        return false;
    }

    const std::size_t colTableBytes = static_cast<std::size_t>(patchW) * 4u;
    if (8u + colTableBytes > static_cast<std::size_t>(lump.size)) {
        return false;
    }

    outTex.width = patchW;
    outTex.height = patchH;
    outTex.rgba.assign(static_cast<std::size_t>(patchW * patchH * 4), 0u);

    for (int x = 0; x < patchW; ++x) {
        const std::size_t relColOff = readU32(wad, base + 8u + static_cast<std::size_t>(x) * 4u);
        if (relColOff >= static_cast<std::size_t>(lump.size)) {
            continue;
        }

        std::size_t colOff = base + relColOff;
        const std::size_t lumpEnd = base + static_cast<std::size_t>(lump.size);
        while (colOff < lumpEnd) {
            const std::uint8_t topDelta = wad[colOff++];
            if (topDelta == 255u) {
                break;
            }
            if (colOff + 2u > lumpEnd) {
                break;
            }

            const std::uint8_t length = wad[colOff++];
            colOff++;  // unused
            if (colOff + static_cast<std::size_t>(length) + 1u > lumpEnd) {
                break;
            }

            for (std::uint8_t i = 0; i < length; ++i) {
                const int dx = x;
                const int dy = static_cast<int>(topDelta) + static_cast<int>(i);
                if (dy < 0 || dy >= patchH) {
                    continue;
                }

                const std::uint8_t idx = wad[colOff + static_cast<std::size_t>(i)];
                const std::size_t pi = static_cast<std::size_t>(idx) * 3u;
                const std::size_t di = static_cast<std::size_t>((dy * patchW + dx) * 4);
                outTex.rgba[di + 0] = palette[pi + 0];
                outTex.rgba[di + 1] = palette[pi + 1];
                outTex.rgba[di + 2] = palette[pi + 2];
                outTex.rgba[di + 3] = 255u;
            }

            colOff += static_cast<std::size_t>(length);
            colOff++;  // trailing unused
        }
    }

    return true;
}

bool tryLoadPatch(
    const std::vector<std::uint8_t>& wad,
    const std::vector<LumpInfo>& lumps,
    const std::unordered_map<std::string, int>& firstLumpByName,
    const std::array<std::uint8_t, 768>& palette,
    const std::string& lumpName,
    RgbaTexture& outTex) {
    auto it = firstLumpByName.find(normalizeName(lumpName));
    if (it == firstLumpByName.end()) {
        return false;
    }
    return decodePatchLump(wad, lumps[static_cast<std::size_t>(it->second)], palette, outTex);
}

void makeFallbackWeapon(RgbaTexture& tex, int slot) {
    tex.width = 96;
    tex.height = 64;
    tex.rgba.assign(static_cast<std::size_t>(tex.width * tex.height * 4), 0u);
    const std::uint8_t colors[7][3] = {
        {220u, 200u, 180u}, {190u, 190u, 200u}, {180u, 170u, 120u}, {150u, 160u, 170u},
        {140u, 120u, 100u}, {120u, 170u, 190u}, {170u, 150u, 120u}};
    const std::uint8_t* c = colors[slot % 7];
    for (int y = tex.height / 2; y < tex.height; ++y) {
        for (int x = tex.width / 5; x < tex.width - tex.width / 5; ++x) {
            const std::size_t i = static_cast<std::size_t>((y * tex.width + x) * 4);
            tex.rgba[i + 0] = c[0];
            tex.rgba[i + 1] = c[1];
            tex.rgba[i + 2] = c[2];
            tex.rgba[i + 3] = 255u;
        }
    }
}

void makeTransparentPatch(RgbaTexture& tex) {
    tex.width = 1;
    tex.height = 1;
    tex.rgba.assign(4u, 0u);
}

void makeFallbackStatGlyph(RgbaTexture& tex, int glyphIndex) {
    static const char* kPatterns[11][5] = {
        {"###", "# #", "# #", "# #", "###"},
        {" ##", "  #", "  #", "  #", " ###"},
        {"###", "  #", "###", "#  ", "###"},
        {"###", "  #", " ##", "  #", "###"},
        {"# #", "# #", "###", "  #", "  #"},
        {"###", "#  ", "###", "  #", "###"},
        {"###", "#  ", "###", "# #", "###"},
        {"###", "  #", "  #", "  #", "  #"},
        {"###", "# #", "###", "# #", "###"},
        {"###", "# #", "###", "  #", "###"},
        {"# #", "  #", " # ", "#  ", "# #"},
    };

    constexpr int kScale = 2;
    constexpr int kCols = 3;
    constexpr int kRows = 5;
    tex.width = kCols * kScale;
    tex.height = kRows * kScale;
    tex.rgba.assign(static_cast<std::size_t>(tex.width * tex.height * 4), 0u);

    const int idx = std::clamp(glyphIndex, 0, 10);
    for (int y = 0; y < kRows; ++y) {
        for (int x = 0; x < kCols; ++x) {
            if (kPatterns[idx][y][x] != '#') {
                continue;
            }
            for (int sy = 0; sy < kScale; ++sy) {
                for (int sx = 0; sx < kScale; ++sx) {
                    const int px = x * kScale + sx;
                    const int py = y * kScale + sy;
                    const std::size_t di = static_cast<std::size_t>((py * tex.width + px) * 4);
                    tex.rgba[di + 0] = 228u;
                    tex.rgba[di + 1] = 214u;
                    tex.rgba[di + 2] = 176u;
                    tex.rgba[di + 3] = 255u;
                }
            }
        }
    }
}

void makeFallbackFace(RgbaTexture& tex) {
    tex.width = 24;
    tex.height = 24;
    tex.rgba.assign(static_cast<std::size_t>(tex.width * tex.height * 4), 0u);
    for (int y = 4; y < 22; ++y) {
        for (int x = 4; x < 20; ++x) {
            const std::size_t i = static_cast<std::size_t>((y * tex.width + x) * 4);
            tex.rgba[i + 0] = 196u;
            tex.rgba[i + 1] = 154u;
            tex.rgba[i + 2] = 112u;
            tex.rgba[i + 3] = 255u;
        }
    }
}

}  // namespace

bool loadHudAssetsFromWad(const std::string& wadPath, HudAssets& outAssets, std::string& outError) {
    std::vector<std::uint8_t> wad;
    if (!loadFile(wadPath, wad)) {
        outError = "Failed to read WAD file: " + wadPath;
        return false;
    }

    std::vector<LumpInfo> lumps;
    std::unordered_map<std::string, int> firstLumpByName;
    if (!parseDirectory(wad, lumps, firstLumpByName, outError)) {
        return false;
    }

    std::array<std::uint8_t, 768> palette{};
    if (!readFirstPalette(wad, lumps, firstLumpByName, palette)) {
        outError = "PLAYPAL not found";
        return false;
    }

    RgbaTexture statusBar{};
    const char* statusCandidates[] = {"STBAR", "STBAR0", "STBAR1"};
    bool foundStatus = false;
    for (const char* candidate : statusCandidates) {
        if (tryLoadPatch(wad, lumps, firstLumpByName, palette, candidate, statusBar)) {
            foundStatus = true;
            break;
        }
    }

    if (!foundStatus) {
        statusBar.width = 320;
        statusBar.height = 32;
        statusBar.rgba.assign(static_cast<std::size_t>(statusBar.width * statusBar.height * 4), 0u);
        for (int y = 0; y < statusBar.height; ++y) {
            for (int x = 0; x < statusBar.width; ++x) {
                const std::size_t i = static_cast<std::size_t>((y * statusBar.width + x) * 4);
                statusBar.rgba[i + 0] = 40u;
                statusBar.rgba[i + 1] = 34u;
                statusBar.rgba[i + 2] = 28u;
                statusBar.rgba[i + 3] = 220u;
            }
        }
    }

    outAssets.statusBar = std::move(statusBar);

    const std::array<std::array<const char*, 4>, 7> weaponCandidates = {
        std::array<const char*, 4>{"PUNGA0", "CSAWA0", "SAWGA0", ""},
        std::array<const char*, 4>{"PISGA0", "PISFA0", "", ""},
        std::array<const char*, 4>{"SHTGA0", "SHTFA0", "SHT2A0", ""},
        std::array<const char*, 4>{"CHGGA0", "CHGFA0", "", ""},
        std::array<const char*, 4>{"MISGA0", "MISFA0", "LAUNA0", ""},
        std::array<const char*, 4>{"PLSGA0", "PLSFA0", "", ""},
        std::array<const char*, 4>{"BFGGA0", "BFGFA0", "", ""}};

    const std::array<std::array<const char*, 4>, 7> flashCandidates = {
        std::array<const char*, 4>{"SAWFA0", "PUNFA0", "", ""},
        std::array<const char*, 4>{"PISFA0", "PISFB0", "", ""},
        std::array<const char*, 4>{"SHTFA0", "SHTFB0", "SHT2F0", ""},
        std::array<const char*, 4>{"CHGFA0", "CHGFB0", "", ""},
        std::array<const char*, 4>{"MISFA0", "MISFB0", "", ""},
        std::array<const char*, 4>{"PLSFA0", "PLSFB0", "", ""},
        std::array<const char*, 4>{"BFGFA0", "BFGFB0", "", ""}};

    outAssets.weaponSprites.clear();
    outAssets.weaponSprites.reserve(7);
    outAssets.muzzleFlashSprites.clear();
    outAssets.muzzleFlashSprites.reserve(7);
    for (int slot = 0; slot < 7; ++slot) {
        RgbaTexture weapon{};
        RgbaTexture flash{};
        bool found = false;
        for (const char* cand : weaponCandidates[static_cast<std::size_t>(slot)]) {
            if (cand[0] == '\0') {
                continue;
            }
            if (tryLoadPatch(wad, lumps, firstLumpByName, palette, cand, weapon)) {
                found = true;
                break;
            }
        }
        if (!found) {
            makeFallbackWeapon(weapon, slot);
        }
        bool foundFlash = false;
        for (const char* cand : flashCandidates[static_cast<std::size_t>(slot)]) {
            if (cand[0] == '\0') {
                continue;
            }
            if (tryLoadPatch(wad, lumps, firstLumpByName, palette, cand, flash)) {
                foundFlash = true;
                break;
            }
        }
        if (!foundFlash) {
            makeTransparentPatch(flash);
        }
        outAssets.weaponSprites.push_back(std::move(weapon));
        outAssets.muzzleFlashSprites.push_back(std::move(flash));
    }

    outAssets.statGlyphs.clear();
    outAssets.statGlyphs.reserve(11);
    for (int i = 0; i < 10; ++i) {
        RgbaTexture glyph{};
        const std::string lumpName = "STTNUM" + std::to_string(i);
        if (!tryLoadPatch(wad, lumps, firstLumpByName, palette, lumpName, glyph)) {
            makeFallbackStatGlyph(glyph, i);
        }
        outAssets.statGlyphs.push_back(std::move(glyph));
    }

    RgbaTexture percentGlyph{};
    if (!tryLoadPatch(wad, lumps, firstLumpByName, palette, "STTPRCNT", percentGlyph)) {
        makeFallbackStatGlyph(percentGlyph, 10);
    }
    outAssets.statGlyphs.push_back(std::move(percentGlyph));

    const std::array<std::array<const char*, 3>, 6> faceCandidates = {
        std::array<const char*, 3>{"STFST00", "STFST01", "STFST02"},
        std::array<const char*, 3>{"STFST10", "STFST11", "STFST12"},
        std::array<const char*, 3>{"STFST20", "STFST21", "STFST22"},
        std::array<const char*, 3>{"STFST30", "STFST31", "STFST32"},
        std::array<const char*, 3>{"STFST40", "STFST41", "STFST42"},
        std::array<const char*, 3>{"STFDEAD0", "STFGOD0", "STFST00"},
    };

    outAssets.faceSprites.clear();
    outAssets.faceSprites.reserve(faceCandidates.size());
    for (const auto& candidates : faceCandidates) {
        RgbaTexture face{};
        bool found = false;
        for (const char* cand : candidates) {
            if (cand[0] == '\0') {
                continue;
            }
            if (tryLoadPatch(wad, lumps, firstLumpByName, palette, cand, face)) {
                found = true;
                break;
            }
        }
        if (!found) {
            makeFallbackFace(face);
        }
        outAssets.faceSprites.push_back(std::move(face));
    }

    return true;
}

}  // namespace doom
