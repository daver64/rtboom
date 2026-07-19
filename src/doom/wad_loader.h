#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace doom {

struct RgbaTexture {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> rgba;
};

struct MapData {
    int gridWidth = 0;
    int gridHeight = 0;
    float originX = 0.0f;
    float originZ = 0.0f;
    float cellSize = 1.0f;
    float spawnX = 0.0f;
    float spawnZ = 0.0f;
    std::vector<std::uint8_t> cells;
    std::vector<std::uint8_t> floorLayerMap;
    std::vector<std::uint8_t> ceilingLayerMap;
    std::vector<std::uint8_t> lightLevelMap;
    // Packed as x0, z0, x1, z1 for each segment.
    std::vector<float> wallSegments;
    // Packed as texLayer, xOffset, yOffset, reserved per segment.
    std::vector<float> wallSegmentAttribs;
    int wallAtlasWidth = 0;
    int wallAtlasHeight = 0;
    int wallAtlasLayers = 0;
    std::vector<std::uint8_t> wallAtlasRgba;
    int floorAtlasWidth = 0;
    int floorAtlasHeight = 0;
    int floorAtlasLayers = 0;
    std::vector<std::uint8_t> floorAtlasRgba;
    int ceilingAtlasWidth = 0;
    int ceilingAtlasHeight = 0;
    int ceilingAtlasLayers = 0;
    std::vector<std::uint8_t> ceilingAtlasRgba;
    RgbaTexture floorTexture;
    RgbaTexture ceilingTexture;
    RgbaTexture wallTexture;
};

bool loadMapFromWad(
    const std::string& wadPath,
    const std::string& mapName,
    MapData& outMap,
    std::string& outError);

}  // namespace doom
