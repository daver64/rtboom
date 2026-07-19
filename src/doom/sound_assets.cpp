#include "doom/sound_assets.h"

#include <algorithm>
#include <array>
#include <cmath>
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
        const char c = static_cast<char>(bytes[offset + static_cast<std::size_t>(i)]);
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

bool decodeDoomDigitalSound(
    const std::vector<std::uint8_t>& wad,
    const LumpInfo& lump,
    PcmSound& outSound,
    std::string& outError) {
    if (lump.size < 8) {
        outError = "Sound lump too small";
        return false;
    }

    const std::size_t base = static_cast<std::size_t>(lump.offset);
    const std::uint16_t format = readU16(wad, base + 0u);
    if (format != 3u) {
        outError = "Unsupported Doom sound format";
        return false;
    }

    const std::uint16_t sampleRate = readU16(wad, base + 2u);
    const std::uint32_t sampleCount = readU32(wad, base + 4u);

    const std::size_t available = static_cast<std::size_t>(std::max(0, lump.size - 8));
    const std::size_t count = std::min(static_cast<std::size_t>(sampleCount), available);
    if (count == 0u || sampleRate < 2000u || sampleRate > 64000u) {
        outError = "Invalid Doom sound metadata";
        return false;
    }

    outSound.sampleRate = static_cast<int>(sampleRate);
    outSound.channels = 1;
    outSound.samples.resize(count);

    for (std::size_t i = 0; i < count; ++i) {
        const std::uint8_t u8 = wad[base + 8u + i];
        const int centered = static_cast<int>(u8) - 128;
        outSound.samples[i] = static_cast<std::int16_t>(centered << 8);
    }

    return true;
}

void resampleToRate(PcmSound& sound, int targetRate) {
    if (targetRate <= 0) {
        return;
    }
    if (sound.samples.empty() || sound.sampleRate <= 0 || sound.sampleRate == targetRate) {
        sound.sampleRate = targetRate;
        return;
    }

    const std::size_t srcCount = sound.samples.size();
    const std::size_t dstCount =
        static_cast<std::size_t>((static_cast<double>(srcCount) * static_cast<double>(targetRate)) /
                                 static_cast<double>(sound.sampleRate));
    if (dstCount == 0u) {
        sound.samples.clear();
        sound.sampleRate = targetRate;
        return;
    }

    std::vector<std::int16_t> out(dstCount);
    for (std::size_t i = 0; i < dstCount; ++i) {
        const double srcPos = (static_cast<double>(i) * static_cast<double>(sound.sampleRate)) /
                              static_cast<double>(targetRate);
        std::size_t srcIndex = static_cast<std::size_t>(srcPos);
        if (srcIndex >= srcCount) {
            srcIndex = srcCount - 1u;
        }
        out[i] = sound.samples[srcIndex];
    }

    sound.samples = std::move(out);
    sound.sampleRate = targetRate;
}

bool loadSoundByCandidates(
    const std::vector<std::uint8_t>& wad,
    const std::vector<LumpInfo>& lumps,
    const std::unordered_map<std::string, int>& firstLumpByName,
    const std::array<const char*, 4>& candidates,
    PcmSound& outSound,
    std::string& outError) {
    for (const char* name : candidates) {
        if (!name || name[0] == '\0') {
            continue;
        }
        const auto it = firstLumpByName.find(name);
        if (it == firstLumpByName.end()) {
            continue;
        }
        if (decodeDoomDigitalSound(wad, lumps[static_cast<std::size_t>(it->second)], outSound, outError)) {
            return true;
        }
    }
    return false;
}

}  // namespace

void makeFallbackFireSound(PcmSound& outSound) {
    outSound.sampleRate = 22050;
    outSound.channels = 1;

    const int durationMs = 120;
    const int count = (outSound.sampleRate * durationMs) / 1000;
    outSound.samples.resize(static_cast<std::size_t>(count));

    const float twoPi = 6.28318530718f;
    for (int i = 0; i < count; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(outSound.sampleRate);
        const float env = std::max(0.0f, 1.0f - t * 10.0f);
        const float tone = std::sin(twoPi * 1400.0f * t) * 0.55f + std::sin(twoPi * 900.0f * t) * 0.3f;
        const float v = tone * env;
        outSound.samples[static_cast<std::size_t>(i)] = static_cast<std::int16_t>(v * 32767.0f);
    }
}

bool loadWeaponFireSoundsFromWad(
    const std::string& wadPath,
    std::vector<PcmSound>& outSounds,
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

    const std::array<std::array<const char*, 4>, 7> weaponSoundCandidates = {
        std::array<const char*, 4>{"DSSAWFUL", "DSSAWHIT", "DSSAWUP", "DSPISTOL"},
        std::array<const char*, 4>{"DSPISTOL", "DSRIFLE", "", ""},
        std::array<const char*, 4>{"DSSHOTGN", "DSSHT2", "", ""},
        std::array<const char*, 4>{"DSPISTOL", "DSRIFLE", "", ""},
        std::array<const char*, 4>{"DSRLAUNC", "DSRXPLOD", "", ""},
        std::array<const char*, 4>{"DSPLASMA", "DSFIRSHT", "", ""},
        std::array<const char*, 4>{"DSBFG", "DSBFGX", "", ""}};

    outSounds.clear();
    outSounds.resize(7);

    bool anyLoaded = false;
    std::string lastError;
    for (std::size_t i = 0; i < weaponSoundCandidates.size(); ++i) {
        PcmSound s{};
        if (loadSoundByCandidates(wad, lumps, firstLumpByName, weaponSoundCandidates[i], s, lastError)) {
            outSounds[i] = std::move(s);
            anyLoaded = true;
        }
    }

    if (!anyLoaded) {
        outError = "No supported weapon sound lump found";
        return false;
    }

    for (std::size_t i = 0; i < outSounds.size(); ++i) {
        if (outSounds[i].samples.empty()) {
            if (!outSounds[1].samples.empty()) {
                outSounds[i] = outSounds[1];
            } else {
                makeFallbackFireSound(outSounds[i]);
            }
        }
        resampleToRate(outSounds[i], 22050);
    }

    return true;
}

bool loadWeaponFireSoundFromWad(const std::string& wadPath, PcmSound& outSound, std::string& outError) {
    std::vector<PcmSound> sounds;
    if (!loadWeaponFireSoundsFromWad(wadPath, sounds, outError)) {
        return false;
    }
    if (sounds.size() < 2u || sounds[1].samples.empty()) {
        outError = "Weapon sound bank missing pistol slot";
        return false;
    }
    outSound = std::move(sounds[1]);
    return true;
}

}  // namespace doom
