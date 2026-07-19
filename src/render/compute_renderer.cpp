#include "render/compute_renderer.h"

#include "doom/wad_loader.h"

#include <glad/gl.h>

#include <cmath>
#include <iostream>
#include <limits>
#include <algorithm>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace {
int idx2d(int x, int y, int w) {
    return y * w + x;
}
}  // namespace

bool ComputeRenderer::loadDoomMap() {
    doom::MapData map{};
    std::string err;

    struct Candidate {
        const char* wadPath;
        const char* mapName;
    };

    const Candidate candidates[] = {
        {"assets/freedoom2.wad", "MAP01"},
        {"assets/freedoom1.wad", "E1M1"},
        {"assets/freedoom1.wad", "E1M2"},
        {"assets/freedoom2.wad", "MAP02"},
    };

    bool loaded = false;
    for (const Candidate& c : candidates) {
        if (doom::loadMapFromWad(c.wadPath, c.mapName, map, err)) {
            loaded = true;
            std::cout << "Loaded WAD map: " << c.wadPath << " " << c.mapName << '\n';
            break;
        }
    }

    if (!loaded) {
        std::cerr << "Failed to load FreeDoom map: " << err << '\n';
        return false;
    }

    // DOOM map units are much larger than this renderer's world scale.
    constexpr float kWorldScale = 1.0f / 32.0f;

    dungeonWidth_ = map.gridWidth;
    dungeonHeight_ = map.gridHeight;
    dungeonOriginX_ = map.originX * kWorldScale;
    dungeonOriginZ_ = map.originZ * kWorldScale;
    dungeonCellSize_ = map.cellSize * kWorldScale;
    spawnX_ = map.spawnX * kWorldScale;
    spawnZ_ = map.spawnZ * kWorldScale;
    dungeonCells_ = std::move(map.cells);

    floorTexW_ = map.floorTexture.width;
    floorTexH_ = map.floorTexture.height;
    floorRgba_ = std::move(map.floorTexture.rgba);
    floorTexW_ = map.floorAtlasWidth > 0 ? map.floorAtlasWidth : floorTexW_;
    floorTexH_ = map.floorAtlasHeight > 0 ? map.floorAtlasHeight : floorTexH_;
    floorTexLayers_ = map.floorAtlasLayers > 0 ? map.floorAtlasLayers : 1;
    floorAtlasRgba_ = std::move(map.floorAtlasRgba);

    ceilingTexW_ = map.ceilingTexture.width;
    ceilingTexH_ = map.ceilingTexture.height;
    ceilingRgba_ = std::move(map.ceilingTexture.rgba);
    ceilingTexW_ = map.ceilingAtlasWidth > 0 ? map.ceilingAtlasWidth : ceilingTexW_;
    ceilingTexH_ = map.ceilingAtlasHeight > 0 ? map.ceilingAtlasHeight : ceilingTexH_;
    ceilingTexLayers_ = map.ceilingAtlasLayers > 0 ? map.ceilingAtlasLayers : 1;
    ceilingAtlasRgba_ = std::move(map.ceilingAtlasRgba);

    floorLayerMap_ = std::move(map.floorLayerMap);
    ceilingLayerMap_ = std::move(map.ceilingLayerMap);
    lightLevelMap_ = std::move(map.lightLevelMap);

    wallTexW_ = map.wallTexture.width;
    wallTexH_ = map.wallTexture.height;
    wallRgba_ = std::move(map.wallTexture.rgba);
    wallTexW_ = map.wallAtlasWidth > 0 ? map.wallAtlasWidth : wallTexW_;
    wallTexH_ = map.wallAtlasHeight > 0 ? map.wallAtlasHeight : wallTexH_;
    wallTexLayers_ = map.wallAtlasLayers > 0 ? map.wallAtlasLayers : 1;
    wallAtlasRgba_ = std::move(map.wallAtlasRgba);
    wallSegments_ = std::move(map.wallSegments);
    wallSegmentAttribs_ = std::move(map.wallSegmentAttribs);

    if (wallSegmentAttribs_.size() != wallSegments_.size()) {
        wallSegmentAttribs_.assign(wallSegments_.size(), 0.0f);
        for (std::size_t i = 0; i + 3u < wallSegmentAttribs_.size(); i += 4u) {
            wallSegmentAttribs_[i] = 160.0f / 255.0f;
        }
    }

    for (std::size_t i = 0; i < wallSegments_.size(); ++i) {
        wallSegments_[i] *= kWorldScale;
    }

    std::cout << "Wall segments: " << (wallSegments_.size() / 4u) << " scale=" << kWorldScale << '\n';
    std::cout << "Wall texture layers: " << wallTexLayers_ << " size=" << wallTexW_ << "x" << wallTexH_
              << '\n';
    std::unordered_set<int> usedLayers;
    for (std::size_t i = 0; i + 1u < wallSegmentAttribs_.size(); i += 4u) {
        usedLayers.insert(static_cast<int>(wallSegmentAttribs_[i + 1u]));
    }
    std::cout << "Wall layers used by segments: " << usedLayers.size() << '\n';

    return true;
}

bool ComputeRenderer::uploadMapTexture() {
    if (dungeonCells_.empty() || dungeonWidth_ <= 0 || dungeonHeight_ <= 0) {
        return false;
    }

    glCreateTextures(GL_TEXTURE_2D, 1, &dungeonTexture_);
    glTextureStorage2D(dungeonTexture_, 1, GL_R8UI, dungeonWidth_, dungeonHeight_);
    glTextureSubImage2D(
        dungeonTexture_,
        0,
        0,
        0,
        dungeonWidth_,
        dungeonHeight_,
        GL_RED_INTEGER,
        GL_UNSIGNED_BYTE,
        dungeonCells_.data());
    glTextureParameteri(dungeonTexture_, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTextureParameteri(dungeonTexture_, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTextureParameteri(dungeonTexture_, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameteri(dungeonTexture_, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    return true;
}

bool ComputeRenderer::uploadSurfaceTextures() {
    if (floorRgba_.empty() || ceilingRgba_.empty() || wallRgba_.empty()) {
        return false;
    }

    glCreateTextures(GL_TEXTURE_2D_ARRAY, 1, &floorTexture_);
    glTextureStorage3D(floorTexture_, 1, GL_RGBA8, floorTexW_, floorTexH_, floorTexLayers_);
    if (!floorAtlasRgba_.empty()) {
        glTextureSubImage3D(
            floorTexture_,
            0,
            0,
            0,
            0,
            floorTexW_,
            floorTexH_,
            floorTexLayers_,
            GL_RGBA,
            GL_UNSIGNED_BYTE,
            floorAtlasRgba_.data());
    } else {
        glTextureSubImage3D(
            floorTexture_,
            0,
            0,
            0,
            0,
            floorTexW_,
            floorTexH_,
            1,
            GL_RGBA,
            GL_UNSIGNED_BYTE,
            floorRgba_.data());
    }
    glTextureParameteri(floorTexture_, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTextureParameteri(floorTexture_, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(floorTexture_, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTextureParameteri(floorTexture_, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTextureParameteri(floorTexture_, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

    glCreateTextures(GL_TEXTURE_2D_ARRAY, 1, &ceilingTexture_);
    glTextureStorage3D(ceilingTexture_, 1, GL_RGBA8, ceilingTexW_, ceilingTexH_, ceilingTexLayers_);
    if (!ceilingAtlasRgba_.empty()) {
        glTextureSubImage3D(
            ceilingTexture_,
            0,
            0,
            0,
            0,
            ceilingTexW_,
            ceilingTexH_,
            ceilingTexLayers_,
            GL_RGBA,
            GL_UNSIGNED_BYTE,
            ceilingAtlasRgba_.data());
    } else {
        glTextureSubImage3D(
            ceilingTexture_,
            0,
            0,
            0,
            0,
            ceilingTexW_,
            ceilingTexH_,
            1,
            GL_RGBA,
            GL_UNSIGNED_BYTE,
            ceilingRgba_.data());
    }
    glTextureParameteri(ceilingTexture_, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTextureParameteri(ceilingTexture_, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(ceilingTexture_, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTextureParameteri(ceilingTexture_, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTextureParameteri(ceilingTexture_, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

    glCreateTextures(GL_TEXTURE_2D_ARRAY, 1, &wallTexture_);
    glTextureStorage3D(wallTexture_, 1, GL_RGBA8, wallTexW_, wallTexH_, wallTexLayers_);
    if (!wallAtlasRgba_.empty()) {
        glTextureSubImage3D(
            wallTexture_,
            0,
            0,
            0,
            0,
            wallTexW_,
            wallTexH_,
            wallTexLayers_,
            GL_RGBA,
            GL_UNSIGNED_BYTE,
            wallAtlasRgba_.data());
    } else {
        glTextureSubImage3D(
            wallTexture_,
            0,
            0,
            0,
            0,
            wallTexW_,
            wallTexH_,
            1,
            GL_RGBA,
            GL_UNSIGNED_BYTE,
            wallRgba_.data());
    }
    glTextureParameteri(wallTexture_, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTextureParameteri(wallTexture_, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(wallTexture_, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTextureParameteri(wallTexture_, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTextureParameteri(wallTexture_, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

    if (!floorLayerMap_.empty() && !ceilingLayerMap_.empty() && !lightLevelMap_.empty()) {
        glCreateTextures(GL_TEXTURE_2D, 1, &floorLayerMapTexture_);
        glTextureStorage2D(floorLayerMapTexture_, 1, GL_R8UI, dungeonWidth_, dungeonHeight_);
        glTextureSubImage2D(
            floorLayerMapTexture_,
            0,
            0,
            0,
            dungeonWidth_,
            dungeonHeight_,
            GL_RED_INTEGER,
            GL_UNSIGNED_BYTE,
            floorLayerMap_.data());
        glTextureParameteri(floorLayerMapTexture_, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTextureParameteri(floorLayerMapTexture_, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTextureParameteri(floorLayerMapTexture_, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTextureParameteri(floorLayerMapTexture_, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        glCreateTextures(GL_TEXTURE_2D, 1, &ceilingLayerMapTexture_);
        glTextureStorage2D(ceilingLayerMapTexture_, 1, GL_R8UI, dungeonWidth_, dungeonHeight_);
        glTextureSubImage2D(
            ceilingLayerMapTexture_,
            0,
            0,
            0,
            dungeonWidth_,
            dungeonHeight_,
            GL_RED_INTEGER,
            GL_UNSIGNED_BYTE,
            ceilingLayerMap_.data());
        glTextureParameteri(ceilingLayerMapTexture_, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTextureParameteri(ceilingLayerMapTexture_, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTextureParameteri(ceilingLayerMapTexture_, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTextureParameteri(ceilingLayerMapTexture_, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        glCreateTextures(GL_TEXTURE_2D, 1, &lightMapTexture_);
        glTextureStorage2D(lightMapTexture_, 1, GL_R8UI, dungeonWidth_, dungeonHeight_);
        glTextureSubImage2D(
            lightMapTexture_,
            0,
            0,
            0,
            dungeonWidth_,
            dungeonHeight_,
            GL_RED_INTEGER,
            GL_UNSIGNED_BYTE,
            lightLevelMap_.data());
        glTextureParameteri(lightMapTexture_, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTextureParameteri(lightMapTexture_, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTextureParameteri(lightMapTexture_, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTextureParameteri(lightMapTexture_, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }

    return true;
}

bool ComputeRenderer::uploadWallSegments() {
    if (wallSegments_.empty() || (wallSegments_.size() % 4u) != 0u) {
        return false;
    }

    glCreateBuffers(1, &wallSegmentBuffer_);
    glCreateBuffers(1, &wallSegmentAttribBuffer_);
    activeWallSegments_.clear();
    activeWallSegmentAttribs_.clear();
    activeWallSegments_.reserve(4u * 256u);
    activeWallSegmentAttribs_.reserve(4u * 256u);
    glNamedBufferData(wallSegmentBuffer_, static_cast<GLsizeiptr>(4u * 256u * sizeof(float)), nullptr, GL_DYNAMIC_DRAW);
    glNamedBufferData(
        wallSegmentAttribBuffer_,
        static_cast<GLsizeiptr>(4u * 256u * sizeof(float)),
        nullptr,
        GL_DYNAMIC_DRAW);
    return true;
}

void ComputeRenderer::updateActiveWallSegments(float camX, float camZ) {
    if (wallSegments_.empty()) {
        activeWallSegments_.clear();
        activeWallSegmentAttribs_.clear();
        return;
    }

    struct Candidate {
        float dist2;
        std::size_t base;
    };

    std::vector<Candidate> candidates;
    candidates.reserve(wallSegments_.size() / 4u);

    constexpr float kCullRadius = 36.0f;
    const float maxDist2 = kCullRadius * kCullRadius;

    for (std::size_t i = 0; i + 3u < wallSegments_.size(); i += 4u) {
        const float x0 = wallSegments_[i + 0u];
        const float z0 = wallSegments_[i + 1u];
        const float x1 = wallSegments_[i + 2u];
        const float z1 = wallSegments_[i + 3u];
        const float mx = 0.5f * (x0 + x1);
        const float mz = 0.5f * (z0 + z1);
        const float dx = mx - camX;
        const float dz = mz - camZ;
        const float d2 = dx * dx + dz * dz;
        if (d2 <= maxDist2) {
            candidates.push_back({d2, i});
        }
    }

    if (candidates.empty()) {
        // Keep a small fallback set so rendering never collapses entirely.
        constexpr std::size_t kFallbackSegs = 128u;
        const std::size_t total = wallSegments_.size() / 4u;
        const std::size_t take = std::min(kFallbackSegs, total);
        activeWallSegments_.assign(wallSegments_.begin(), wallSegments_.begin() + take * 4u);
        activeWallSegmentAttribs_.assign(
            wallSegmentAttribs_.begin(), wallSegmentAttribs_.begin() + take * 4u);
        return;
    }

    constexpr std::size_t kMaxSegments = 256u;
    const std::size_t take = std::min(kMaxSegments, candidates.size());
    std::partial_sort(
        candidates.begin(),
        candidates.begin() + static_cast<std::ptrdiff_t>(take),
        candidates.end(),
        [](const Candidate& a, const Candidate& b) { return a.dist2 < b.dist2; });

    activeWallSegments_.clear();
    activeWallSegmentAttribs_.clear();
    activeWallSegments_.reserve(take * 4u);
    activeWallSegmentAttribs_.reserve(take * 4u);
    for (std::size_t i = 0; i < take; ++i) {
        const std::size_t base = candidates[i].base;
        activeWallSegments_.push_back(wallSegments_[base + 0u]);
        activeWallSegments_.push_back(wallSegments_[base + 1u]);
        activeWallSegments_.push_back(wallSegments_[base + 2u]);
        activeWallSegments_.push_back(wallSegments_[base + 3u]);

        activeWallSegmentAttribs_.push_back(wallSegmentAttribs_[base + 0u]);
        activeWallSegmentAttribs_.push_back(wallSegmentAttribs_[base + 1u]);
        activeWallSegmentAttribs_.push_back(wallSegmentAttribs_[base + 2u]);
        activeWallSegmentAttribs_.push_back(wallSegmentAttribs_[base + 3u]);
    }
}

bool ComputeRenderer::initialize(int width, int height, const char* shaderPath) {
    width_ = width;
    height_ = height;

    if (!computeProgram_.loadComputeFromFile(shaderPath)) {
        return false;
    }

    glCreateTextures(GL_TEXTURE_2D, 1, &outputTexture_);
    glTextureStorage2D(outputTexture_, 1, GL_RGBA8, width_, height_);
    glTextureParameteri(outputTexture_, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTextureParameteri(outputTexture_, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(outputTexture_, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameteri(outputTexture_, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    if (!loadDoomMap()) {
        std::cerr << "Failed to load DOOM map" << '\n';
        return false;
    }

    if (!uploadMapTexture()) {
        std::cerr << "Failed to upload map texture" << '\n';
        return false;
    }

    if (!uploadSurfaceTextures()) {
        std::cerr << "Failed to upload surface textures" << '\n';
        return false;
    }

    if (!uploadWallSegments()) {
        std::cerr << "Failed to upload wall segment buffer" << '\n';
        return false;
    }

    return true;
}

void ComputeRenderer::shutdown() {
    if (lightMapTexture_) {
        glDeleteTextures(1, &lightMapTexture_);
        lightMapTexture_ = 0;
    }
    if (ceilingLayerMapTexture_) {
        glDeleteTextures(1, &ceilingLayerMapTexture_);
        ceilingLayerMapTexture_ = 0;
    }
    if (floorLayerMapTexture_) {
        glDeleteTextures(1, &floorLayerMapTexture_);
        floorLayerMapTexture_ = 0;
    }
    if (wallSegmentAttribBuffer_) {
        glDeleteBuffers(1, &wallSegmentAttribBuffer_);
        wallSegmentAttribBuffer_ = 0;
    }
    if (wallSegmentBuffer_) {
        glDeleteBuffers(1, &wallSegmentBuffer_);
        wallSegmentBuffer_ = 0;
    }
    if (wallTexture_) {
        glDeleteTextures(1, &wallTexture_);
        wallTexture_ = 0;
    }
    if (ceilingTexture_) {
        glDeleteTextures(1, &ceilingTexture_);
        ceilingTexture_ = 0;
    }
    if (floorTexture_) {
        glDeleteTextures(1, &floorTexture_);
        floorTexture_ = 0;
    }
    if (dungeonTexture_) {
        glDeleteTextures(1, &dungeonTexture_);
        dungeonTexture_ = 0;
    }
    if (outputTexture_) {
        glDeleteTextures(1, &outputTexture_);
        outputTexture_ = 0;
    }
}

void ComputeRenderer::render(float timeSeconds, const CameraParams& camera) {
    if (!computeProgram_.isValid() || !outputTexture_ || !dungeonTexture_ || !floorTexture_ ||
        !ceilingTexture_ || !wallTexture_ || !wallSegmentBuffer_ || !wallSegmentAttribBuffer_ ||
        !floorLayerMapTexture_ || !ceilingLayerMapTexture_ || !lightMapTexture_) {
        return;
    }

    computeProgram_.use();

    updateActiveWallSegments(camera.pos[0], camera.pos[2]);
    if (activeWallSegments_.empty()) {
        return;
    }
    glNamedBufferSubData(
        wallSegmentBuffer_,
        0,
        static_cast<GLsizeiptr>(activeWallSegments_.size() * sizeof(float)),
        activeWallSegments_.data());
    glNamedBufferSubData(
        wallSegmentAttribBuffer_,
        0,
        static_cast<GLsizeiptr>(activeWallSegmentAttribs_.size() * sizeof(float)),
        activeWallSegmentAttribs_.data());

    const GLint timeLoc = glGetUniformLocation(computeProgram_.id(), "uTime");
    const GLint sizeLoc = glGetUniformLocation(computeProgram_.id(), "uTargetSize");
    const GLint camPosLoc = glGetUniformLocation(computeProgram_.id(), "uCamPos");
    const GLint camForwardLoc = glGetUniformLocation(computeProgram_.id(), "uCamForward");
    const GLint camRightLoc = glGetUniformLocation(computeProgram_.id(), "uCamRight");
    const GLint camUpLoc = glGetUniformLocation(computeProgram_.id(), "uCamUp");
    const GLint camFovLoc = glGetUniformLocation(computeProgram_.id(), "uCamFovScale");
    const GLint mapTexLoc = glGetUniformLocation(computeProgram_.id(), "uDungeon");
    const GLint mapSizeLoc = glGetUniformLocation(computeProgram_.id(), "uDungeonSize");
    const GLint mapOriginLoc = glGetUniformLocation(computeProgram_.id(), "uDungeonOrigin");
    const GLint mapCellSizeLoc = glGetUniformLocation(computeProgram_.id(), "uDungeonCellSize");
    const GLint floorTexLoc = glGetUniformLocation(computeProgram_.id(), "uFloorTex");
    const GLint ceilTexLoc = glGetUniformLocation(computeProgram_.id(), "uCeilTex");
    const GLint wallTexLoc = glGetUniformLocation(computeProgram_.id(), "uWallTex");
    const GLint floorTexSizeLoc = glGetUniformLocation(computeProgram_.id(), "uFloorTexSize");
    const GLint ceilTexSizeLoc = glGetUniformLocation(computeProgram_.id(), "uCeilTexSize");
    const GLint wallTexSizeLoc = glGetUniformLocation(computeProgram_.id(), "uWallTexSize");
    const GLint floorTexLayersLoc = glGetUniformLocation(computeProgram_.id(), "uFloorTexLayers");
    const GLint ceilTexLayersLoc = glGetUniformLocation(computeProgram_.id(), "uCeilTexLayers");
    const GLint wallTexLayersLoc = glGetUniformLocation(computeProgram_.id(), "uWallTexLayers");
    const GLint floorLayerMapLoc = glGetUniformLocation(computeProgram_.id(), "uFloorLayerMap");
    const GLint ceilLayerMapLoc = glGetUniformLocation(computeProgram_.id(), "uCeilLayerMap");
    const GLint lightMapLoc = glGetUniformLocation(computeProgram_.id(), "uLightMap");
    const GLint wallSegCountLoc = glGetUniformLocation(computeProgram_.id(), "uWallSegmentCount");
    glUniform1f(timeLoc, timeSeconds);
    glUniform2i(sizeLoc, width_, height_);
    glUniform3f(camPosLoc, camera.pos[0], camera.pos[1], camera.pos[2]);
    glUniform3f(camForwardLoc, camera.forward[0], camera.forward[1], camera.forward[2]);
    glUniform3f(camRightLoc, camera.right[0], camera.right[1], camera.right[2]);
    glUniform3f(camUpLoc, camera.up[0], camera.up[1], camera.up[2]);
    glUniform1f(camFovLoc, camera.fovScale);
    glUniform1i(mapTexLoc, 0);
    glUniform2i(mapSizeLoc, dungeonWidth_, dungeonHeight_);
    glUniform2f(mapOriginLoc, dungeonOriginX_, dungeonOriginZ_);
    glUniform1f(mapCellSizeLoc, dungeonCellSize_);
    glUniform1i(floorTexLoc, 1);
    glUniform1i(ceilTexLoc, 2);
    glUniform1i(wallTexLoc, 3);
    glUniform1i(floorLayerMapLoc, 4);
    glUniform1i(ceilLayerMapLoc, 5);
    glUniform1i(lightMapLoc, 6);
    glUniform2i(floorTexSizeLoc, floorTexW_, floorTexH_);
    glUniform2i(ceilTexSizeLoc, ceilingTexW_, ceilingTexH_);
    glUniform2i(wallTexSizeLoc, wallTexW_, wallTexH_);
    glUniform1i(floorTexLayersLoc, floorTexLayers_);
    glUniform1i(ceilTexLayersLoc, ceilingTexLayers_);
    glUniform1i(wallTexLayersLoc, wallTexLayers_);
    glUniform1i(wallSegCountLoc, static_cast<int>(activeWallSegments_.size() / 4u));

    glBindTextureUnit(0, dungeonTexture_);
    glBindTextureUnit(1, floorTexture_);
    glBindTextureUnit(2, ceilingTexture_);
    glBindTextureUnit(3, wallTexture_);
    glBindTextureUnit(4, floorLayerMapTexture_);
    glBindTextureUnit(5, ceilingLayerMapTexture_);
    glBindTextureUnit(6, lightMapTexture_);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, wallSegmentBuffer_);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, wallSegmentAttribBuffer_);

    glBindImageTexture(0, outputTexture_, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA8);

    constexpr GLuint kLocalX = 8;
    constexpr GLuint kLocalY = 8;
    const GLuint gx = static_cast<GLuint>(std::ceil(width_ / static_cast<float>(kLocalX)));
    const GLuint gy = static_cast<GLuint>(std::ceil(height_ / static_cast<float>(kLocalY)));

    glDispatchCompute(gx, gy, 1);
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
}

bool ComputeRenderer::isWallAt(float worldX, float worldZ) const {
    if (dungeonCells_.empty()) {
        return true;
    }

    const float invCell = 1.0f / dungeonCellSize_;
    const int cellX = static_cast<int>(std::floor((worldX - dungeonOriginX_) * invCell));
    const int cellY = static_cast<int>(std::floor((worldZ - dungeonOriginZ_) * invCell));
    if (cellX < 0 || cellY < 0 || cellX >= dungeonWidth_ || cellY >= dungeonHeight_) {
        return true;
    }

    return dungeonCells_[idx2d(cellX, cellY, dungeonWidth_)] != 0u;
}
