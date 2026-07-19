#version 430 core

in vec2 vUV;
out vec4 fragColor;

uniform sampler2D uSource;
uniform sampler2D uHudTex;
uniform sampler2DArray uWeaponTex;
uniform sampler2DArray uFlashTex;
uniform sampler2DArray uStatGlyphTex;
uniform sampler2DArray uFaceTex;
uniform float uFps;
uniform float uFrameMs;
uniform vec2 uWindowSize;
uniform ivec2 uHudSize;
uniform int uWeaponLayer;
uniform ivec2 uWeaponSize;
uniform float uWeaponRecoil;
uniform float uMuzzleFlash;
uniform ivec2 uFlashSize;
uniform int uProjectileCount;
uniform vec4 uProjectiles[24];
uniform vec4 uProjectileColors[24];
uniform ivec2 uStatGlyphSize[11];
uniform ivec2 uStatGlyphCell;
uniform int uHealth;
uniform int uAmmo;
uniform int uHudAnimTick;
uniform int uFaceLayerCount;
uniform ivec2 uFaceSize;

void blendGlyph(inout vec4 outCol, ivec2 srcPx, ivec2 origin, int glyphIndex) {
    if (glyphIndex < 0 || glyphIndex > 10) {
        return;
    }

    ivec2 local = srcPx - origin;
    if (local.x < 0 || local.y < 0 || local.x >= uStatGlyphCell.x || local.y >= uStatGlyphCell.y) {
        return;
    }

    ivec2 g = uStatGlyphSize[glyphIndex];
    if (local.x >= g.x || local.y >= g.y) {
        return;
    }

    ivec3 texel = ivec3(local.x, g.y - 1 - local.y, glyphIndex);
    vec4 v = texelFetch(uStatGlyphTex, texel, 0);
    outCol.rgb = mix(outCol.rgb, v.rgb, v.a);
}

void drawNumber3(inout vec4 outCol, ivec2 srcPx, int value, int rightX, int bottomY) {
    int v = clamp(value, 0, 999);
    int d0 = v % 10;
    int d1 = (v / 10) % 10;
    int d2 = (v / 100) % 10;

    int adv = uStatGlyphCell.x + 1;
    blendGlyph(outCol, srcPx, ivec2(rightX - adv, bottomY), d0);
    if (v >= 10) {
        blendGlyph(outCol, srcPx, ivec2(rightX - adv * 2, bottomY), d1);
    }
    if (v >= 100) {
        blendGlyph(outCol, srcPx, ivec2(rightX - adv * 3, bottomY), d2);
    }
}

void drawNumberCentered(inout vec4 outCol, ivec2 srcPx, int value, int centerX, int bottomY, bool withPercent) {
    int v = clamp(value, 0, 999);
    int digits[3];
    int count = 1;
    if (v >= 100) {
        count = 3;
        digits[0] = (v / 100) % 10;
        digits[1] = (v / 10) % 10;
        digits[2] = v % 10;
    } else if (v >= 10) {
        count = 2;
        digits[0] = (v / 10) % 10;
        digits[1] = v % 10;
    } else {
        digits[0] = v;
    }

    int totalW = 0;
    for (int i = 0; i < count; ++i) {
        totalW += uStatGlyphSize[digits[i]].x;
        if (i + 1 < count) {
            totalW += 1;
        }
    }
    if (withPercent) {
        totalW += 1 + uStatGlyphSize[10].x;
    }

    int x = centerX - totalW / 2;
    for (int i = 0; i < count; ++i) {
        blendGlyph(outCol, srcPx, ivec2(x, bottomY), digits[i]);
        x += uStatGlyphSize[digits[i]].x + 1;
    }
    if (withPercent) {
        blendGlyph(outCol, srcPx, ivec2(x, bottomY + 1), 10);
    }
}

void main() {
    ivec2 srcSize = textureSize(uSource, 0);
    vec2 srcUV = clamp(vUV, vec2(0.0), vec2(0.999999));
    ivec2 srcPx = ivec2(floor(srcUV * vec2(srcSize)));
    vec4 base = texelFetch(uSource, srcPx, 0);
    vec4 outCol = base;

    int recoilOffset = int(round(10.0 * clamp(uWeaponRecoil, 0.0, 1.0)));
    ivec2 weaponOrigin = ivec2((srcSize.x - uWeaponSize.x) / 2, recoilOffset);
    ivec2 weaponLocal = srcPx - weaponOrigin;
    if (weaponLocal.x >= 0 && weaponLocal.y >= 0 && weaponLocal.x < uWeaponSize.x && weaponLocal.y < uWeaponSize.y) {
        ivec3 weaponTexel = ivec3(weaponLocal.x, uWeaponSize.y - 1 - weaponLocal.y, uWeaponLayer);
        vec4 wpn = texelFetch(uWeaponTex, weaponTexel, 0);
        float a = wpn.a;
        outCol.rgb = mix(outCol.rgb, wpn.rgb, a);
    }

    if (uMuzzleFlash > 0.001) {
        ivec2 flashOrigin = ivec2((srcSize.x - uFlashSize.x) / 2, uWeaponSize.y - uFlashSize.y / 3 + recoilOffset);
        ivec2 flashLocal = srcPx - flashOrigin;
        if (flashLocal.x >= 0 && flashLocal.y >= 0 && flashLocal.x < uFlashSize.x && flashLocal.y < uFlashSize.y) {
            ivec3 flashTexel = ivec3(flashLocal.x, uFlashSize.y - 1 - flashLocal.y, uWeaponLayer);
            vec4 fl = texelFetch(uFlashTex, flashTexel, 0);
            float fa = fl.a * clamp(uMuzzleFlash, 0.0, 1.0);
            vec3 flashColor = fl.rgb * (1.0 + 0.8 * clamp(uMuzzleFlash, 0.0, 1.0));
            outCol.rgb = mix(outCol.rgb, flashColor, fa);
        }
    }

    vec2 p = vec2(srcPx) + vec2(0.5);
    for (int i = 0; i < uProjectileCount; ++i) {
        vec4 pr = uProjectiles[i];
        vec2 center = vec2(pr.x * float(srcSize.x), pr.y * float(srcSize.y));
        float radius = max(1.0, pr.z);
        float d = length(p - center);
        float glow = smoothstep(radius, 0.0, d) * pr.w;
        outCol.rgb += uProjectileColors[i].rgb * glow;
    }

    ivec2 hudOrigin = ivec2((srcSize.x - uHudSize.x) / 2, 0);
    ivec2 hudLocal = srcPx - hudOrigin;
    if (hudLocal.x >= 0 && hudLocal.y >= 0 && hudLocal.x < uHudSize.x && hudLocal.y < uHudSize.y) {
        ivec2 hudTexel = ivec2(hudLocal.x, uHudSize.y - 1 - hudLocal.y);
        vec4 hud = texelFetch(uHudTex, hudTexel, 0);
        outCol.rgb = mix(outCol.rgb, hud.rgb, hud.a);
    }

    int hudLeft = (srcSize.x - uHudSize.x) / 2;
    int hudBottom = 0;
    int statY = hudBottom + 9;

    drawNumberCentered(outCol, srcPx, uAmmo, hudLeft - 5, statY, false);
    drawNumberCentered(outCol, srcPx, uHealth, hudLeft + 42, statY, true);

    int faceBand = 5;
    if (uHealth > 80) {
        faceBand = 0;
    } else if (uHealth > 60) {
        faceBand = 1;
    } else if (uHealth > 40) {
        faceBand = 2;
    } else if (uHealth > 20) {
        faceBand = 3;
    } else if (uHealth > 0) {
        faceBand = 4;
    }

    int faceLayer = min(faceBand, max(0, uFaceLayerCount - 1));
    if (faceBand < 5 && (uHudAnimTick % 24) > 19 && uFaceLayerCount > 1) {
        faceLayer = min(faceBand + 1, max(0, uFaceLayerCount - 1));
    }

    ivec2 faceOrigin = ivec2(hudLeft + (uHudSize.x - uFaceSize.x) / 2, hudBottom + 2);
    ivec2 faceLocal = srcPx - faceOrigin;
    if (faceLocal.x >= 0 && faceLocal.y >= 0 && faceLocal.x < uFaceSize.x && faceLocal.y < uFaceSize.y) {
        ivec3 faceTexel = ivec3(faceLocal.x, uFaceSize.y - 1 - faceLocal.y, faceLayer);
        vec4 face = texelFetch(uFaceTex, faceTexel, 0);
        outCol.rgb = mix(outCol.rgb, face.rgb, face.a);
    }

    fragColor = vec4(outCol.rgb, 1.0);
}
