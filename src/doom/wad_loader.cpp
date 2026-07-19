#include "doom/wad_loader.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <limits>
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

struct Vertex {
    std::int16_t x = 0;
    std::int16_t y = 0;
};

struct Linedef {
    std::uint16_t v1 = 0;
    std::uint16_t v2 = 0;
    std::uint16_t flags = 0;
    std::uint16_t type = 0;
    std::uint16_t tag = 0;
    std::uint16_t rightSidedef = 0;
    std::uint16_t leftSidedef = 0;
};

struct Sidedef {
    std::int16_t xOffset = 0;
    std::int16_t yOffset = 0;
    std::string upperTex;
    std::string lowerTex;
    std::string middleTex;
    std::uint16_t sector = 0;
};

struct Sector {
    std::int16_t floorHeight = 0;
    std::int16_t ceilHeight = 0;
    std::string floorFlat;
    std::string ceilFlat;
    std::int16_t lightLevel = 0;
    std::int16_t type = 0;
    std::int16_t tag = 0;
};

struct Thing {
    std::int16_t x = 0;
    std::int16_t y = 0;
    std::int16_t angle = 0;
    std::uint16_t type = 0;
    std::uint16_t flags = 0;
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

int findMapMarker(const std::vector<LumpInfo>& lumps, const std::string& mapName) {
    const std::string target = normalizeName(mapName);
    for (std::size_t i = 0; i < lumps.size(); ++i) {
        if (lumps[i].name == target) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

const LumpInfo* mapLumpByName(const std::vector<LumpInfo>& lumps, int mapMarker, const std::string& name) {
    if (mapMarker < 0 || mapMarker >= static_cast<int>(lumps.size())) {
        return nullptr;
    }

    const std::string target = normalizeName(name);
    const int end = std::min(static_cast<int>(lumps.size()), mapMarker + 12);
    for (int i = mapMarker + 1; i < end; ++i) {
        if (lumps[static_cast<std::size_t>(i)].name == target) {
            return &lumps[static_cast<std::size_t>(i)];
        }
    }
    return nullptr;
}

bool parseVertexes(const std::vector<std::uint8_t>& wad, const LumpInfo& lump, std::vector<Vertex>& out) {
    if ((lump.size % 4) != 0) {
        return false;
    }
    const int count = lump.size / 4;
    out.clear();
    out.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        const std::size_t off = static_cast<std::size_t>(lump.offset) + static_cast<std::size_t>(i) * 4u;
        Vertex v{};
        v.x = readS16(wad, off + 0u);
        v.y = readS16(wad, off + 2u);
        out.push_back(v);
    }
    return true;
}

bool parseLinedefs(const std::vector<std::uint8_t>& wad, const LumpInfo& lump, std::vector<Linedef>& out) {
    if ((lump.size % 14) != 0) {
        return false;
    }
    const int count = lump.size / 14;
    out.clear();
    out.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        const std::size_t off = static_cast<std::size_t>(lump.offset) + static_cast<std::size_t>(i) * 14u;
        Linedef ld{};
        ld.v1 = readU16(wad, off + 0u);
        ld.v2 = readU16(wad, off + 2u);
        ld.flags = readU16(wad, off + 4u);
        ld.type = readU16(wad, off + 6u);
        ld.tag = readU16(wad, off + 8u);
        ld.rightSidedef = readU16(wad, off + 10u);
        ld.leftSidedef = readU16(wad, off + 12u);
        out.push_back(ld);
    }
    return true;
}

bool parseSidedefs(const std::vector<std::uint8_t>& wad, const LumpInfo& lump, std::vector<Sidedef>& out) {
    if ((lump.size % 30) != 0) {
        return false;
    }
    const int count = lump.size / 30;
    out.clear();
    out.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        const std::size_t off = static_cast<std::size_t>(lump.offset) + static_cast<std::size_t>(i) * 30u;
        Sidedef sd{};
        sd.xOffset = readS16(wad, off + 0u);
        sd.yOffset = readS16(wad, off + 2u);
        sd.upperTex = readLumpName(wad, off + 4u);
        sd.lowerTex = readLumpName(wad, off + 12u);
        sd.middleTex = readLumpName(wad, off + 20u);
        sd.sector = readU16(wad, off + 28u);
        out.push_back(sd);
    }
    return true;
}

bool parseSectors(const std::vector<std::uint8_t>& wad, const LumpInfo& lump, std::vector<Sector>& out) {
    if ((lump.size % 26) != 0) {
        return false;
    }
    const int count = lump.size / 26;
    out.clear();
    out.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        const std::size_t off = static_cast<std::size_t>(lump.offset) + static_cast<std::size_t>(i) * 26u;
        Sector s{};
        s.floorHeight = readS16(wad, off + 0u);
        s.ceilHeight = readS16(wad, off + 2u);
        s.floorFlat = readLumpName(wad, off + 4u);
        s.ceilFlat = readLumpName(wad, off + 12u);
        s.lightLevel = readS16(wad, off + 20u);
        s.type = readS16(wad, off + 22u);
        s.tag = readS16(wad, off + 24u);
        out.push_back(s);
    }
    return true;
}

bool parseThings(const std::vector<std::uint8_t>& wad, const LumpInfo& lump, std::vector<Thing>& out) {
    if ((lump.size % 10) != 0) {
        return false;
    }
    const int count = lump.size / 10;
    out.clear();
    out.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        const std::size_t off = static_cast<std::size_t>(lump.offset) + static_cast<std::size_t>(i) * 10u;
        Thing t{};
        t.x = readS16(wad, off + 0u);
        t.y = readS16(wad, off + 2u);
        t.angle = readS16(wad, off + 4u);
        t.type = readU16(wad, off + 6u);
        t.flags = readU16(wad, off + 8u);
        out.push_back(t);
    }
    return true;
}

void createFallbackTexture(RgbaTexture& tex, std::uint8_t r, std::uint8_t g, std::uint8_t b) {
    tex.width = 64;
    tex.height = 64;
    tex.rgba.assign(static_cast<std::size_t>(tex.width * tex.height * 4), 0u);

    for (int y = 0; y < tex.height; ++y) {
        for (int x = 0; x < tex.width; ++x) {
            const bool checker = (((x / 8) + (y / 8)) & 1) != 0;
            const std::size_t i = static_cast<std::size_t>((y * tex.width + x) * 4);
            tex.rgba[i + 0] = checker ? r : static_cast<std::uint8_t>(r / 2);
            tex.rgba[i + 1] = checker ? g : static_cast<std::uint8_t>(g / 2);
            tex.rgba[i + 2] = checker ? b : static_cast<std::uint8_t>(b / 2);
            tex.rgba[i + 3] = 255u;
        }
    }
}

bool readFirstPalette(
    const std::vector<std::uint8_t>& wad,
    const std::vector<LumpInfo>& lumps,
    const std::unordered_map<std::string, int>& firstLumpByName,
    std::array<std::uint8_t, 768>& outPalette) {
    const auto it = firstLumpByName.find("PLAYPAL");
    if (it == firstLumpByName.end()) {
        return false;
    }
    const LumpInfo& lump = lumps[static_cast<std::size_t>(it->second)];
    if (lump.size < 768) {
        return false;
    }

    std::memcpy(outPalette.data(), wad.data() + lump.offset, 768u);
    return true;
}

bool findFlatBounds(const std::vector<LumpInfo>& lumps, int& outStart, int& outEnd) {
    outStart = -1;
    outEnd = -1;
    for (std::size_t i = 0; i < lumps.size(); ++i) {
        const std::string& name = lumps[i].name;
        if (name == "F_START" || name == "FF_START") {
            outStart = static_cast<int>(i) + 1;
        } else if (name == "F_END" || name == "FF_END") {
            outEnd = static_cast<int>(i);
            break;
        }
    }
    return outStart >= 0 && outEnd > outStart;
}

bool findFlatLump(
    const std::vector<LumpInfo>& lumps,
    int flatStart,
    int flatEnd,
    const std::string& flatName,
    LumpInfo& outLump) {
    if (flatName.empty() || flatName == "-") {
        return false;
    }

    const std::string target = normalizeName(flatName);
    for (int i = flatStart; i < flatEnd; ++i) {
        if (lumps[static_cast<std::size_t>(i)].name == target) {
            outLump = lumps[static_cast<std::size_t>(i)];
            return true;
        }
    }
    return false;
}

bool buildFlatTexture(
    const std::vector<std::uint8_t>& wad,
    const LumpInfo& flatLump,
    const std::array<std::uint8_t, 768>& palette,
    RgbaTexture& outTex) {
    if (flatLump.size < 4096) {
        return false;
    }

    outTex.width = 64;
    outTex.height = 64;
    outTex.rgba.assign(static_cast<std::size_t>(64 * 64 * 4), 0u);

    const std::uint8_t* src = wad.data() + flatLump.offset;
    for (int i = 0; i < 64 * 64; ++i) {
        const std::uint8_t idx = src[i];
        const std::size_t di = static_cast<std::size_t>(i) * 4u;
        const std::size_t pi = static_cast<std::size_t>(idx) * 3u;
        outTex.rgba[di + 0] = palette[pi + 0];
        outTex.rgba[di + 1] = palette[pi + 1];
        outTex.rgba[di + 2] = palette[pi + 2];
        outTex.rgba[di + 3] = 255u;
    }

    return true;
}

void blitTiledLayer(
    const RgbaTexture& src,
    int dstW,
    int dstH,
    int layer,
    std::vector<std::uint8_t>& dst) {
    for (int y = 0; y < dstH; ++y) {
        for (int x = 0; x < dstW; ++x) {
            const int sx = x % src.width;
            const int sy = y % src.height;
            const std::size_t si = static_cast<std::size_t>((sy * src.width + sx) * 4);
            const std::size_t di = static_cast<std::size_t>((((layer * dstH + y) * dstW + x) * 4));
            dst[di + 0] = src.rgba[si + 0];
            dst[di + 1] = src.rgba[si + 1];
            dst[di + 2] = src.rgba[si + 2];
            dst[di + 3] = 255u;
        }
    }
}

float pointSegDist2(float px, float py, float ax, float ay, float bx, float by) {
    const float vx = bx - ax;
    const float vy = by - ay;
    const float wx = px - ax;
    const float wy = py - ay;
    const float vv = vx * vx + vy * vy;
    if (vv <= 1e-8f) {
        const float dx = px - ax;
        const float dy = py - ay;
        return dx * dx + dy * dy;
    }
    float t = (wx * vx + wy * vy) / vv;
    t = std::clamp(t, 0.0f, 1.0f);
    const float cx = ax + t * vx;
    const float cy = ay + t * vy;
    const float dx = px - cx;
    const float dy = py - cy;
    return dx * dx + dy * dy;
}

void buildSectorMaps(
    const std::vector<Vertex>& vertexes,
    const std::vector<Linedef>& linedefs,
    const std::vector<Sidedef>& sidedefs,
    const std::vector<Sector>& sectors,
    const std::unordered_map<std::string, int>& floorLayerByName,
    const std::unordered_map<std::string, int>& ceilLayerByName,
    MapData& outMap) {
    outMap.floorLayerMap.assign(static_cast<std::size_t>(outMap.gridWidth * outMap.gridHeight), 0u);
    outMap.ceilingLayerMap.assign(static_cast<std::size_t>(outMap.gridWidth * outMap.gridHeight), 0u);
    outMap.lightLevelMap.assign(static_cast<std::size_t>(outMap.gridWidth * outMap.gridHeight), 160u);

    for (int y = 0; y < outMap.gridHeight; ++y) {
        for (int x = 0; x < outMap.gridWidth; ++x) {
            const std::size_t idx = static_cast<std::size_t>(y * outMap.gridWidth + x);
            const float wx = outMap.originX + (static_cast<float>(x) + 0.5f) * outMap.cellSize;
            const float wy = outMap.originZ + (static_cast<float>(y) + 0.5f) * outMap.cellSize;

            float bestD2 = std::numeric_limits<float>::max();
            int bestSector = -1;

            for (const Linedef& ld : linedefs) {
                if (ld.v1 >= vertexes.size() || ld.v2 >= vertexes.size()) {
                    continue;
                }
                const Vertex& a = vertexes[ld.v1];
                const Vertex& b = vertexes[ld.v2];
                const float ax = static_cast<float>(a.x);
                const float ay = static_cast<float>(a.y);
                const float bx = static_cast<float>(b.x);
                const float by = static_cast<float>(b.y);

                const float d2 = pointSegDist2(wx, wy, ax, ay, bx, by);
                if (d2 >= bestD2) {
                    continue;
                }

                const float cross = (bx - ax) * (wy - ay) - (by - ay) * (wx - ax);
                const std::uint16_t sideIndex =
                    (cross < 0.0f) ? ld.rightSidedef : ld.leftSidedef;  // right side is "front" in Doom
                if (sideIndex >= sidedefs.size()) {
                    continue;
                }
                const std::uint16_t sec = sidedefs[sideIndex].sector;
                if (sec >= sectors.size()) {
                    continue;
                }

                bestD2 = d2;
                bestSector = static_cast<int>(sec);
            }

            if (bestSector < 0) {
                continue;
            }

            const Sector& s = sectors[static_cast<std::size_t>(bestSector)];
            auto fitFloor = floorLayerByName.find(s.floorFlat);
            auto fitCeil = ceilLayerByName.find(s.ceilFlat);
            if (fitFloor != floorLayerByName.end()) {
                outMap.floorLayerMap[idx] = static_cast<std::uint8_t>(std::clamp(fitFloor->second, 0, 255));
            }
            if (fitCeil != ceilLayerByName.end()) {
                outMap.ceilingLayerMap[idx] = static_cast<std::uint8_t>(std::clamp(fitCeil->second, 0, 255));
            }
            outMap.lightLevelMap[idx] = static_cast<std::uint8_t>(
                std::clamp(static_cast<int>(s.lightLevel), 0, 255));
        }
    }
}

bool findLumpByName(
    const std::vector<LumpInfo>& lumps,
    const std::unordered_map<std::string, int>& firstLumpByName,
    const std::string& name,
    LumpInfo& outLump) {
    const std::string key = normalizeName(name);
    auto it = firstLumpByName.find(key);
    if (it == firstLumpByName.end()) {
        return false;
    }
    outLump = lumps[static_cast<std::size_t>(it->second)];
    return true;
}

bool parsePatchNames(
    const std::vector<std::uint8_t>& wad,
    const std::vector<LumpInfo>& lumps,
    const std::unordered_map<std::string, int>& firstLumpByName,
    std::vector<std::string>& outPatchNames) {
    LumpInfo pnames{};
    if (!findLumpByName(lumps, firstLumpByName, "PNAMES", pnames) || pnames.size < 4) {
        return false;
    }

    const std::size_t base = static_cast<std::size_t>(pnames.offset);
    const std::int32_t count = readS32(wad, base);
    if (count < 0) {
        return false;
    }

    const std::size_t bytesNeeded = 4u + static_cast<std::size_t>(count) * 8u;
    if (bytesNeeded > static_cast<std::size_t>(pnames.size)) {
        return false;
    }

    outPatchNames.clear();
    outPatchNames.reserve(static_cast<std::size_t>(count));
    for (std::int32_t i = 0; i < count; ++i) {
        const std::size_t off = base + 4u + static_cast<std::size_t>(i) * 8u;
        outPatchNames.push_back(readLumpName(wad, off));
    }
    return true;
}

struct TexturePatchRef {
    std::int16_t originX = 0;
    std::int16_t originY = 0;
    std::int16_t patchIndex = -1;
};

struct TextureDef {
    std::string name;
    std::int16_t width = 0;
    std::int16_t height = 0;
    std::vector<TexturePatchRef> patches;
};

bool parseTextureDirectoryLump(
    const std::vector<std::uint8_t>& wad,
    const LumpInfo& lump,
    std::unordered_map<std::string, TextureDef>& outDefs) {
    if (lump.size < 4) {
        return false;
    }

    const std::size_t base = static_cast<std::size_t>(lump.offset);
    const std::int32_t count = readS32(wad, base);
    if (count < 0) {
        return false;
    }

    const std::size_t offsetTableBytes = 4u + static_cast<std::size_t>(count) * 4u;
    if (offsetTableBytes > static_cast<std::size_t>(lump.size)) {
        return false;
    }

    for (std::int32_t i = 0; i < count; ++i) {
        const std::size_t rel = 4u + static_cast<std::size_t>(i) * 4u;
        const std::int32_t texOff = readS32(wad, base + rel);
        if (texOff < 0) {
            continue;
        }

        const std::size_t tbase = base + static_cast<std::size_t>(texOff);
        if (tbase + 22u > base + static_cast<std::size_t>(lump.size)) {
            continue;
        }

        TextureDef def{};
        def.name = readLumpName(wad, tbase + 0u);
        def.width = readS16(wad, tbase + 12u);
        def.height = readS16(wad, tbase + 14u);
        const std::int16_t patchCount = readS16(wad, tbase + 20u);
        if (def.name.empty() || def.width <= 0 || def.height <= 0 || patchCount < 0) {
            continue;
        }

        const std::size_t patchBytes = static_cast<std::size_t>(patchCount) * 10u;
        if (tbase + 22u + patchBytes > base + static_cast<std::size_t>(lump.size)) {
            continue;
        }

        def.patches.reserve(static_cast<std::size_t>(patchCount));
        for (int p = 0; p < patchCount; ++p) {
            const std::size_t poff = tbase + 22u + static_cast<std::size_t>(p) * 10u;
            TexturePatchRef pref{};
            pref.originX = readS16(wad, poff + 0u);
            pref.originY = readS16(wad, poff + 2u);
            pref.patchIndex = readS16(wad, poff + 4u);
            def.patches.push_back(pref);
        }

        outDefs[def.name] = def;
    }

    return !outDefs.empty();
}

bool parseTextureDefs(
    const std::vector<std::uint8_t>& wad,
    const std::vector<LumpInfo>& lumps,
    const std::unordered_map<std::string, int>& firstLumpByName,
    std::unordered_map<std::string, TextureDef>& outDefs) {
    outDefs.clear();

    LumpInfo tex1{};
    if (findLumpByName(lumps, firstLumpByName, "TEXTURE1", tex1)) {
        parseTextureDirectoryLump(wad, tex1, outDefs);
    }

    LumpInfo tex2{};
    if (findLumpByName(lumps, firstLumpByName, "TEXTURE2", tex2)) {
        parseTextureDirectoryLump(wad, tex2, outDefs);
    }

    return !outDefs.empty();
}

bool decodePatchInto(
    const std::vector<std::uint8_t>& wad,
    const LumpInfo& lump,
    const std::array<std::uint8_t, 768>& palette,
    int dstW,
    int dstH,
    int originX,
    int originY,
    std::vector<std::uint8_t>& outRgba) {
    if (lump.size < 8) {
        return false;
    }

    const std::size_t base = static_cast<std::size_t>(lump.offset);
    const std::int16_t patchW = readS16(wad, base + 0u);
    const std::int16_t patchH = readS16(wad, base + 2u);
    if (patchW <= 0 || patchH <= 0) {
        return false;
    }

    const std::size_t colTableBytes = static_cast<std::size_t>(patchW) * 4u;
    if (8u + colTableBytes > static_cast<std::size_t>(lump.size)) {
        return false;
    }

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
            colOff++;  // unused byte

            if (colOff + static_cast<std::size_t>(length) + 1u > lumpEnd) {
                break;
            }

            for (std::uint8_t i = 0; i < length; ++i) {
                const int sx = x;
                const int sy = static_cast<int>(topDelta) + static_cast<int>(i);
                const int dx = originX + sx;
                const int dy = originY + sy;
                if (dx < 0 || dy < 0 || dx >= dstW || dy >= dstH) {
                    continue;
                }

                const std::uint8_t idx = wad[colOff + static_cast<std::size_t>(i)];
                const std::size_t pi = static_cast<std::size_t>(idx) * 3u;
                const std::size_t di = static_cast<std::size_t>((dy * dstW + dx) * 4);
                outRgba[di + 0] = palette[pi + 0];
                outRgba[di + 1] = palette[pi + 1];
                outRgba[di + 2] = palette[pi + 2];
                outRgba[di + 3] = 255u;
            }

            colOff += static_cast<std::size_t>(length);
            colOff++;  // trailing unused byte
        }
    }

    return true;
}

bool buildWallTextureFromPatches(
    const std::vector<std::uint8_t>& wad,
    const std::vector<LumpInfo>& lumps,
    const std::unordered_map<std::string, int>& firstLumpByName,
    const std::array<std::uint8_t, 768>& palette,
    const std::string& textureName,
    RgbaTexture& outTex) {
    std::vector<std::string> patchNames;
    if (!parsePatchNames(wad, lumps, firstLumpByName, patchNames)) {
        return false;
    }

    std::unordered_map<std::string, TextureDef> defs;
    if (!parseTextureDefs(wad, lumps, firstLumpByName, defs)) {
        return false;
    }

    const std::string key = normalizeName(textureName);
    const auto it = defs.find(key);
    if (it == defs.end()) {
        return false;
    }

    const TextureDef& def = it->second;
    if (def.width <= 0 || def.height <= 0 || def.width > 1024 || def.height > 1024) {
        return false;
    }

    outTex.width = def.width;
    outTex.height = def.height;
    outTex.rgba.assign(static_cast<std::size_t>(outTex.width * outTex.height * 4), 0u);

    bool decodedAnyPatch = false;

    for (const TexturePatchRef& pref : def.patches) {
        if (pref.patchIndex < 0 || static_cast<std::size_t>(pref.patchIndex) >= patchNames.size()) {
            continue;
        }
        const std::string& patchName = patchNames[static_cast<std::size_t>(pref.patchIndex)];
        LumpInfo patchLump{};
        if (!findLumpByName(lumps, firstLumpByName, patchName, patchLump)) {
            continue;
        }

        if (decodePatchInto(
            wad,
            patchLump,
            palette,
            outTex.width,
            outTex.height,
            static_cast<int>(pref.originX),
            static_cast<int>(pref.originY),
            outTex.rgba)) {
            decodedAnyPatch = true;
        }
    }

    if (!decodedAnyPatch) {
        return false;
    }

    bool hasColor = false;
    for (std::size_t i = 0; i + 3u < outTex.rgba.size(); i += 4u) {
        if (outTex.rgba[i + 0u] != 0u || outTex.rgba[i + 1u] != 0u || outTex.rgba[i + 2u] != 0u) {
            hasColor = true;
            break;
        }
    }
    if (!hasColor) {
        return false;
    }

    return true;
}

void drawLineOnGrid(
    int x0,
    int y0,
    int x1,
    int y1,
    int w,
    int h,
    std::vector<std::uint8_t>& cells,
    std::uint8_t value) {
    int dx = std::abs(x1 - x0);
    int dy = -std::abs(y1 - y0);
    int sx = (x0 < x1) ? 1 : -1;
    int sy = (y0 < y1) ? 1 : -1;
    int err = dx + dy;

    while (true) {
        if (x0 >= 0 && y0 >= 0 && x0 < w && y0 < h) {
            cells[static_cast<std::size_t>(y0 * w + x0)] = value;
            if (x0 > 0) {
                cells[static_cast<std::size_t>(y0 * w + (x0 - 1))] = value;
            }
            if (x0 + 1 < w) {
                cells[static_cast<std::size_t>(y0 * w + (x0 + 1))] = value;
            }
            if (y0 > 0) {
                cells[static_cast<std::size_t>((y0 - 1) * w + x0)] = value;
            }
            if (y0 + 1 < h) {
                cells[static_cast<std::size_t>((y0 + 1) * w + x0)] = value;
            }
        }

        if (x0 == x1 && y0 == y1) {
            break;
        }
        const int e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

bool buildMapGrid(
    const std::vector<Vertex>& vertexes,
    const std::vector<Linedef>& linedefs,
    const std::vector<Sidedef>& sidedefs,
    const std::vector<Sector>& sectors,
    const std::vector<Thing>& things,
    MapData& outMap,
    std::string& outError) {
    (void)sidedefs;
    (void)sectors;
    if (vertexes.empty() || linedefs.empty()) {
        outError = "Map has no geometry";
        return false;
    }

    int minX = std::numeric_limits<int>::max();
    int minY = std::numeric_limits<int>::max();
    int maxX = std::numeric_limits<int>::min();
    int maxY = std::numeric_limits<int>::min();

    for (const Vertex& v : vertexes) {
        minX = std::min(minX, static_cast<int>(v.x));
        minY = std::min(minY, static_cast<int>(v.y));
        maxX = std::max(maxX, static_cast<int>(v.x));
        maxY = std::max(maxY, static_cast<int>(v.y));
    }

    constexpr float kCellSize = 16.0f;
    constexpr int kPaddingCells = 12;
    const float invCell = 1.0f / kCellSize;
    const float worldW = static_cast<float>(maxX - minX);
    const float worldH = static_cast<float>(maxY - minY);

    int gridW = static_cast<int>(std::ceil(worldW * invCell)) + kPaddingCells * 2 + 1;
    int gridH = static_cast<int>(std::ceil(worldH * invCell)) + kPaddingCells * 2 + 1;
    gridW = std::clamp(gridW, 64, 1024);
    gridH = std::clamp(gridH, 64, 1024);

    outMap.gridWidth = gridW;
    outMap.gridHeight = gridH;
    outMap.cellSize = kCellSize;
    outMap.originX = static_cast<float>(minX) - static_cast<float>(kPaddingCells) * kCellSize;
    outMap.originZ = static_cast<float>(minY) - static_cast<float>(kPaddingCells) * kCellSize;
    outMap.cells.assign(static_cast<std::size_t>(gridW * gridH), 0u);

    auto toCell = [&](float wx, float wz, int& cx, int& cy) {
        cx = static_cast<int>(std::floor((wx - outMap.originX) * invCell));
        cy = static_cast<int>(std::floor((wz - outMap.originZ) * invCell));
    };

    auto isSolidLinedef = [&](const Linedef& ld) {
        constexpr std::uint16_t kLineFlagBlocking = 0x0001u;
        constexpr std::uint16_t kNoSidedef = 0xFFFFu;

        if ((ld.flags & kLineFlagBlocking) != 0u) {
            return true;
        }
        if (ld.leftSidedef == kNoSidedef || ld.rightSidedef == kNoSidedef) {
            return true;
        }
        return false;
    };

    for (const Linedef& ld : linedefs) {
        if (ld.v1 >= vertexes.size() || ld.v2 >= vertexes.size()) {
            continue;
        }
        if (!isSolidLinedef(ld)) {
            continue;
        }
        const Vertex& a = vertexes[ld.v1];
        const Vertex& b = vertexes[ld.v2];

        int x0 = 0;
        int y0 = 0;
        int x1 = 0;
        int y1 = 0;
        toCell(static_cast<float>(a.x), static_cast<float>(a.y), x0, y0);
        toCell(static_cast<float>(b.x), static_cast<float>(b.y), x1, y1);
        drawLineOnGrid(x0, y0, x1, y1, gridW, gridH, outMap.cells, 1u);
    }

    for (int x = 0; x < gridW; ++x) {
        outMap.cells[static_cast<std::size_t>(x)] = 2u;
        outMap.cells[static_cast<std::size_t>((gridH - 1) * gridW + x)] = 2u;
    }
    for (int y = 0; y < gridH; ++y) {
        outMap.cells[static_cast<std::size_t>(y * gridW)] = 2u;
        outMap.cells[static_cast<std::size_t>(y * gridW + (gridW - 1))] = 2u;
    }

    bool foundSpawn = false;
    for (const Thing& t : things) {
        if (t.type == 1u || t.type == 2u || t.type == 3u || t.type == 4u) {
            outMap.spawnX = static_cast<float>(t.x);
            outMap.spawnZ = static_cast<float>(t.y);
            foundSpawn = true;
            break;
        }
    }

    if (!foundSpawn) {
        outMap.spawnX = 0.5f * static_cast<float>(minX + maxX);
        outMap.spawnZ = 0.5f * static_cast<float>(minY + maxY);
    }

    return true;
}

void buildWallSegments(
    const std::vector<Vertex>& vertexes,
    const std::vector<Linedef>& linedefs,
    const std::vector<Sidedef>& sidedefs,
    const std::vector<Sector>& sectors,
    const std::unordered_map<std::string, int>& wallLayerByName,
    MapData& outMap) {
    auto isSolidLinedef = [&](const Linedef& ld) {
        constexpr std::uint16_t kLineFlagBlocking = 0x0001u;
        constexpr std::uint16_t kNoSidedef = 0xFFFFu;

        if ((ld.flags & kLineFlagBlocking) != 0u) {
            return true;
        }
        if (ld.leftSidedef == kNoSidedef || ld.rightSidedef == kNoSidedef) {
            return true;
        }
        return false;
    };

    outMap.wallSegments.clear();
    outMap.wallSegments.reserve(linedefs.size() * 4u);
    outMap.wallSegmentAttribs.clear();
    outMap.wallSegmentAttribs.reserve(linedefs.size() * 4u);

    for (const Linedef& ld : linedefs) {
        if (ld.v1 >= vertexes.size() || ld.v2 >= vertexes.size()) {
            continue;
        }
        if (!isSolidLinedef(ld)) {
            continue;
        }

        const Vertex& a = vertexes[ld.v1];
        const Vertex& b = vertexes[ld.v2];
        if (a.x == b.x && a.y == b.y) {
            continue;
        }

        int wallLayer = 0;
        auto resolveSideTexture = [&](std::uint16_t sideIndex, std::string& outName) {
            if (sideIndex >= sidedefs.size()) {
                return false;
            }
            const Sidedef& sd = sidedefs[sideIndex];
            if (!sd.middleTex.empty() && sd.middleTex != "-") {
                outName = sd.middleTex;
                return true;
            }
            if (!sd.upperTex.empty() && sd.upperTex != "-") {
                outName = sd.upperTex;
                return true;
            }
            if (!sd.lowerTex.empty() && sd.lowerTex != "-") {
                outName = sd.lowerTex;
                return true;
            }
            return false;
        };
        std::string texName;
        if (!resolveSideTexture(ld.rightSidedef, texName)) {
            resolveSideTexture(ld.leftSidedef, texName);
        }
        if (!texName.empty()) {
            auto itLayer = wallLayerByName.find(texName);
            if (itLayer != wallLayerByName.end()) {
                wallLayer = itLayer->second;
            }
        }

        float lightNorm = 160.0f / 255.0f;
        const auto sampleLightFromSidedef = [&](std::uint16_t sideIndex, float& outLight) {
            if (sideIndex >= sidedefs.size()) {
                return false;
            }
            const std::uint16_t sec = sidedefs[sideIndex].sector;
            if (sec >= sectors.size()) {
                return false;
            }
            const int ll = std::clamp(static_cast<int>(sectors[sec].lightLevel), 0, 255);
            outLight = static_cast<float>(ll) / 255.0f;
            return true;
        };

        float rightLight = lightNorm;
        float leftLight = lightNorm;
        const bool hasRight = sampleLightFromSidedef(ld.rightSidedef, rightLight);
        const bool hasLeft = sampleLightFromSidedef(ld.leftSidedef, leftLight);
        if (hasRight && hasLeft) {
            lightNorm = 0.5f * (rightLight + leftLight);
        } else if (hasRight) {
            lightNorm = rightLight;
        } else if (hasLeft) {
            lightNorm = leftLight;
        }

        outMap.wallSegments.push_back(static_cast<float>(a.x));
        outMap.wallSegments.push_back(static_cast<float>(a.y));
        outMap.wallSegments.push_back(static_cast<float>(b.x));
        outMap.wallSegments.push_back(static_cast<float>(b.y));

        outMap.wallSegmentAttribs.push_back(lightNorm);
        outMap.wallSegmentAttribs.push_back(static_cast<float>(wallLayer));
        outMap.wallSegmentAttribs.push_back(0.0f);
        outMap.wallSegmentAttribs.push_back(0.0f);
    }
}

std::string chooseWallTextureName(const std::vector<Sidedef>& sidedefs) {
    std::unordered_map<std::string, int> counts;
    auto addName = [&](const std::string& tex) {
        if (tex.empty() || tex == "-") {
            return;
        }
        counts[tex] += 1;
    };
    for (const Sidedef& sd : sidedefs) {
        addName(sd.middleTex);
        addName(sd.upperTex);
        addName(sd.lowerTex);
    }

    int bestCount = -1;
    std::string best;
    for (const auto& kv : counts) {
        if (kv.second > bestCount) {
            bestCount = kv.second;
            best = kv.first;
        }
    }
    return best;
}

}  // namespace

bool loadMapFromWad(
    const std::string& wadPath,
    const std::string& mapName,
    MapData& outMap,
    std::string& outError) {
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

    const int marker = findMapMarker(lumps, mapName);
    if (marker < 0) {
        outError = "Map not found in WAD: " + mapName;
        return false;
    }

    const LumpInfo* thingsLump = mapLumpByName(lumps, marker, "THINGS");
    const LumpInfo* linedefsLump = mapLumpByName(lumps, marker, "LINEDEFS");
    const LumpInfo* sidedefsLump = mapLumpByName(lumps, marker, "SIDEDEFS");
    const LumpInfo* vertexesLump = mapLumpByName(lumps, marker, "VERTEXES");
    const LumpInfo* sectorsLump = mapLumpByName(lumps, marker, "SECTORS");

    if (!thingsLump || !linedefsLump || !sidedefsLump || !vertexesLump || !sectorsLump) {
        outError = "Map is missing required classic DOOM lumps";
        return false;
    }

    std::vector<Thing> things;
    std::vector<Linedef> linedefs;
    std::vector<Sidedef> sidedefs;
    std::vector<Vertex> vertexes;
    std::vector<Sector> sectors;

    if (!parseThings(wad, *thingsLump, things) ||
        !parseLinedefs(wad, *linedefsLump, linedefs) ||
        !parseSidedefs(wad, *sidedefsLump, sidedefs) ||
        !parseVertexes(wad, *vertexesLump, vertexes) ||
        !parseSectors(wad, *sectorsLump, sectors)) {
        outError = "Failed to parse map lump data";
        return false;
    }

    if (!buildMapGrid(vertexes, linedefs, sidedefs, sectors, things, outMap, outError)) {
        return false;
    }

    std::array<std::uint8_t, 768> palette{};
    const bool hasPalette = readFirstPalette(wad, lumps, firstLumpByName, palette);

    int flatStart = -1;
    int flatEnd = -1;
    const bool hasFlatRange = findFlatBounds(lumps, flatStart, flatEnd);

    createFallbackTexture(outMap.floorTexture, 92u, 86u, 80u);
    createFallbackTexture(outMap.ceilingTexture, 70u, 76u, 82u);
    createFallbackTexture(outMap.wallTexture, 114u, 73u, 62u);
    outMap.wallAtlasWidth = 128;
    outMap.wallAtlasHeight = 128;
    outMap.wallAtlasLayers = 1;
    outMap.wallAtlasRgba.assign(static_cast<std::size_t>(128 * 128 * 4), 0u);
    for (int y = 0; y < 128; ++y) {
        for (int x = 0; x < 128; ++x) {
            const int sx = x % outMap.wallTexture.width;
            const int sy = y % outMap.wallTexture.height;
            const std::size_t si = static_cast<std::size_t>((sy * outMap.wallTexture.width + sx) * 4);
            const std::size_t di = static_cast<std::size_t>((y * 128 + x) * 4);
            outMap.wallAtlasRgba[di + 0] = outMap.wallTexture.rgba[si + 0];
            outMap.wallAtlasRgba[di + 1] = outMap.wallTexture.rgba[si + 1];
            outMap.wallAtlasRgba[di + 2] = outMap.wallTexture.rgba[si + 2];
            outMap.wallAtlasRgba[di + 3] = 255u;
        }
    }

    std::unordered_map<std::string, int> wallLayerByName;
    std::unordered_map<std::string, int> floorLayerByName;
    std::unordered_map<std::string, int> ceilLayerByName;

    if (hasPalette && hasFlatRange && !sectors.empty()) {
        LumpInfo floorFlat{};
        LumpInfo ceilFlat{};
        const std::string floorName = sectors[0].floorFlat;
        const std::string ceilName = sectors[0].ceilFlat;

        if (findFlatLump(lumps, flatStart, flatEnd, floorName, floorFlat)) {
            buildFlatTexture(wad, floorFlat, palette, outMap.floorTexture);
        }
        if (findFlatLump(lumps, flatStart, flatEnd, ceilName, ceilFlat)) {
            buildFlatTexture(wad, ceilFlat, palette, outMap.ceilingTexture);
        }

        // DOOM wall textures are patch-composited from PNAMES + TEXTURE1/2.
        const std::string wallName = chooseWallTextureName(sidedefs);
        if (!wallName.empty()) {
            if (!buildWallTextureFromPatches(wad, lumps, firstLumpByName, palette, wallName, outMap.wallTexture)) {
                LumpInfo wallFlat{};
                if (findFlatLump(lumps, flatStart, flatEnd, wallName, wallFlat)) {
                    buildFlatTexture(wad, wallFlat, palette, outMap.wallTexture);
                } else if (!floorName.empty() && findFlatLump(lumps, flatStart, flatEnd, floorName, wallFlat)) {
                    buildFlatTexture(wad, wallFlat, palette, outMap.wallTexture);
                }
            }
        }

        std::unordered_map<std::string, int> counts;
        auto addWallTex = [&](const std::string& n) {
            if (n.empty() || n == "-") {
                return;
            }
            counts[n] += 1;
        };
        for (const Sidedef& sd : sidedefs) {
            addWallTex(sd.middleTex);
            addWallTex(sd.upperTex);
            addWallTex(sd.lowerTex);
        }

        std::vector<std::pair<std::string, int>> ranked;
        ranked.reserve(counts.size());
        for (const auto& kv : counts) {
            ranked.push_back(kv);
        }
        std::sort(ranked.begin(), ranked.end(), [](const auto& a, const auto& b) {
            return a.second > b.second;
        });

        constexpr int kMaxLayers = 16;
        std::vector<RgbaTexture> layers;
        layers.reserve(kMaxLayers);

        RgbaTexture fallback = outMap.wallTexture;
        layers.push_back(fallback);
        wallLayerByName["__FALLBACK__"] = 0;

        for (const auto& kv : ranked) {
            if (static_cast<int>(layers.size()) >= kMaxLayers) {
                break;
            }
            RgbaTexture tex{};
            if (!buildWallTextureFromPatches(wad, lumps, firstLumpByName, palette, kv.first, tex)) {
                continue;
            }
            if (tex.width <= 0 || tex.height <= 0 || tex.rgba.empty()) {
                continue;
            }
            wallLayerByName[kv.first] = static_cast<int>(layers.size());
            layers.push_back(std::move(tex));
        }

        outMap.wallAtlasWidth = 128;
        outMap.wallAtlasHeight = 128;
        outMap.wallAtlasLayers = static_cast<int>(layers.size());
        outMap.wallAtlasRgba.assign(
            static_cast<std::size_t>(outMap.wallAtlasWidth * outMap.wallAtlasHeight * outMap.wallAtlasLayers * 4),
            0u);

        for (int layer = 0; layer < outMap.wallAtlasLayers; ++layer) {
            const RgbaTexture& src = layers[static_cast<std::size_t>(layer)];
            for (int y = 0; y < outMap.wallAtlasHeight; ++y) {
                for (int x = 0; x < outMap.wallAtlasWidth; ++x) {
                    const int sx = x % src.width;
                    const int sy = y % src.height;
                    const std::size_t si = static_cast<std::size_t>((sy * src.width + sx) * 4);
                    const std::size_t di = static_cast<std::size_t>(
                        (((layer * outMap.wallAtlasHeight + y) * outMap.wallAtlasWidth + x) * 4));
                    outMap.wallAtlasRgba[di + 0] = src.rgba[si + 0];
                    outMap.wallAtlasRgba[di + 1] = src.rgba[si + 1];
                    outMap.wallAtlasRgba[di + 2] = src.rgba[si + 2];
                    outMap.wallAtlasRgba[di + 3] = 255u;
                }
            }
        }

        constexpr int kMaxFlatLayers = 16;
        std::unordered_map<std::string, int> floorCounts;
        std::unordered_map<std::string, int> ceilCounts;
        for (const Sector& s : sectors) {
            if (!s.floorFlat.empty() && s.floorFlat != "-") {
                floorCounts[s.floorFlat] += 1;
            }
            if (!s.ceilFlat.empty() && s.ceilFlat != "-") {
                ceilCounts[s.ceilFlat] += 1;
            }
        }

        auto rankByCount = [](const std::unordered_map<std::string, int>& counts) {
            std::vector<std::pair<std::string, int>> ranked;
            ranked.reserve(counts.size());
            for (const auto& kv : counts) {
                ranked.push_back(kv);
            }
            std::sort(ranked.begin(), ranked.end(), [](const auto& a, const auto& b) {
                return a.second > b.second;
            });
            return ranked;
        };

        std::vector<RgbaTexture> floorLayers;
        std::vector<RgbaTexture> ceilLayers;
        floorLayers.push_back(outMap.floorTexture);
        ceilLayers.push_back(outMap.ceilingTexture);

        floorLayerByName["__FALLBACK__"] = 0;
        ceilLayerByName["__FALLBACK__"] = 0;

        const auto rankedFloors = rankByCount(floorCounts);
        for (const auto& kv : rankedFloors) {
            if (static_cast<int>(floorLayers.size()) >= kMaxFlatLayers) {
                break;
            }
            LumpInfo flatLump{};
            if (!findFlatLump(lumps, flatStart, flatEnd, kv.first, flatLump)) {
                continue;
            }
            RgbaTexture tex{};
            if (!buildFlatTexture(wad, flatLump, palette, tex)) {
                continue;
            }
            floorLayerByName[kv.first] = static_cast<int>(floorLayers.size());
            floorLayers.push_back(std::move(tex));
        }

        const auto rankedCeils = rankByCount(ceilCounts);
        for (const auto& kv : rankedCeils) {
            if (static_cast<int>(ceilLayers.size()) >= kMaxFlatLayers) {
                break;
            }
            LumpInfo flatLump{};
            if (!findFlatLump(lumps, flatStart, flatEnd, kv.first, flatLump)) {
                continue;
            }
            RgbaTexture tex{};
            if (!buildFlatTexture(wad, flatLump, palette, tex)) {
                continue;
            }
            ceilLayerByName[kv.first] = static_cast<int>(ceilLayers.size());
            ceilLayers.push_back(std::move(tex));
        }

        outMap.floorAtlasWidth = 64;
        outMap.floorAtlasHeight = 64;
        outMap.floorAtlasLayers = static_cast<int>(floorLayers.size());
        outMap.floorAtlasRgba.assign(
            static_cast<std::size_t>(64 * 64 * outMap.floorAtlasLayers * 4),
            0u);
        for (int i = 0; i < outMap.floorAtlasLayers; ++i) {
            blitTiledLayer(floorLayers[static_cast<std::size_t>(i)], 64, 64, i, outMap.floorAtlasRgba);
        }

        outMap.ceilingAtlasWidth = 64;
        outMap.ceilingAtlasHeight = 64;
        outMap.ceilingAtlasLayers = static_cast<int>(ceilLayers.size());
        outMap.ceilingAtlasRgba.assign(
            static_cast<std::size_t>(64 * 64 * outMap.ceilingAtlasLayers * 4),
            0u);
        for (int i = 0; i < outMap.ceilingAtlasLayers; ++i) {
            blitTiledLayer(ceilLayers[static_cast<std::size_t>(i)], 64, 64, i, outMap.ceilingAtlasRgba);
        }
    }

    buildWallSegments(vertexes, linedefs, sidedefs, sectors, wallLayerByName, outMap);
    buildSectorMaps(vertexes, linedefs, sidedefs, sectors, floorLayerByName, ceilLayerByName, outMap);

    return true;
}

}  // namespace doom
