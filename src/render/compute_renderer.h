#pragma once

#include "render/shader_program.h"

#include <cstdint>
#include <vector>

struct CameraParams {
    float pos[3];
    float forward[3];
    float right[3];
    float up[3];
    float fovScale;
};

class ComputeRenderer {
public:
    bool initialize(int width, int height, const char* shaderPath);
    void shutdown();

    void render(float timeSeconds, const CameraParams& camera);

    bool isWallAt(float worldX, float worldZ) const;
    float spawnX() const { return spawnX_; }
    float spawnZ() const { return spawnZ_; }

    unsigned int outputTexture() const { return outputTexture_; }
    int width() const { return width_; }
    int height() const { return height_; }

private:
    bool loadDoomMap();
    bool uploadMapTexture();
    bool uploadSurfaceTextures();
    bool uploadWallSegments();
    void updateActiveWallSegments(float camX, float camZ);

    ShaderProgram computeProgram_;
    unsigned int outputTexture_ = 0;
    unsigned int dungeonTexture_ = 0;
    unsigned int floorTexture_ = 0;
    unsigned int ceilingTexture_ = 0;
    unsigned int wallTexture_ = 0;
    unsigned int floorLayerMapTexture_ = 0;
    unsigned int ceilingLayerMapTexture_ = 0;
    unsigned int lightMapTexture_ = 0;
    unsigned int wallSegmentBuffer_ = 0;
    unsigned int wallSegmentAttribBuffer_ = 0;
    int width_ = 0;
    int height_ = 0;

    int dungeonWidth_ = 0;
    int dungeonHeight_ = 0;
    float dungeonOriginX_ = 0.0f;
    float dungeonOriginZ_ = 0.0f;
    float dungeonCellSize_ = 1.0f;
    float spawnX_ = 0.0f;
    float spawnZ_ = 0.0f;
    std::vector<std::uint8_t> dungeonCells_;
    std::vector<std::uint8_t> floorRgba_;
    std::vector<std::uint8_t> ceilingRgba_;
    std::vector<std::uint8_t> wallRgba_;
    std::vector<std::uint8_t> floorAtlasRgba_;
    std::vector<std::uint8_t> ceilingAtlasRgba_;
    std::vector<std::uint8_t> floorLayerMap_;
    std::vector<std::uint8_t> ceilingLayerMap_;
    std::vector<std::uint8_t> lightLevelMap_;
    std::vector<std::uint8_t> wallAtlasRgba_;
    int floorTexW_ = 0;
    int floorTexH_ = 0;
    int floorTexLayers_ = 1;
    int ceilingTexW_ = 0;
    int ceilingTexH_ = 0;
    int ceilingTexLayers_ = 1;
    int wallTexW_ = 0;
    int wallTexH_ = 0;
    int wallTexLayers_ = 1;
    std::vector<float> wallSegments_;
    std::vector<float> wallSegmentAttribs_;
    std::vector<float> activeWallSegments_;
    std::vector<float> activeWallSegmentAttribs_;
};
