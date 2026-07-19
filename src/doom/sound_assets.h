#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace doom {

struct PcmSound {
    int sampleRate = 22050;
    int channels = 1;
    std::vector<std::int16_t> samples;
};

bool loadWeaponFireSoundsFromWad(
    const std::string& wadPath,
    std::vector<PcmSound>& outSounds,
    std::string& outError);

bool loadWeaponFireSoundFromWad(const std::string& wadPath, PcmSound& outSound, std::string& outError);

void makeFallbackFireSound(PcmSound& outSound);

}  // namespace doom
