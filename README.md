# rtboom

A Doom-inspired GPU renderer prototype built with modern OpenGL compute shaders, SDL3, and C++17.

`rtboom` loads classic WAD data (Freedoom assets included in `assets/`) and renders a playable first-person scene with software-era aesthetics, weapon HUD, audio effects, and collision against map geometry.

## Highlights

- OpenGL 4.3 compute-shader rendering pipeline
- WAD-backed map loading (Freedoom map fallback path)
- Classic FPS camera controls and wall collision
- Weapon switching, recoil/flash feedback, and projectile overlays
- SDL3-based audio streams for music and weapon SFX
- Self-contained CMake build with dependencies fetched automatically

## Tech Stack

- C++17
- CMake (3.24+)
- OpenGL 4.3+
- SDL3
- GLM
- GLAD

## Requirements

- Linux (tested)
- A C++ compiler with C++17 support (`g++` or `clang++`)
- CMake 3.24 or newer
- OpenGL 4.3 capable GPU/driver
- Internet access on first configure (for FetchContent dependencies)

## Build

From the repository root:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
```

Alternative (if you prefer Make directly):

```bash
cd build
make -j
cd ..
```

## Run

```bash
./build/rtboom
```

The build copies `shaders/` and `assets/` next to the executable automatically.

## Controls

- `W A S D`: Move
- `Mouse`: Look around
- `Left Click`: Fire
- `1-7`: Switch weapon slot
- `Tab`: Toggle relative mouse mode
- `F11`: Toggle fullscreen desktop
- `Esc`: Quit

## Project Layout

- `src/main.cpp`: Main loop, input, camera, HUD/audio wiring
- `src/doom/`: WAD loading and Doom asset helpers
- `src/render/`: Compute renderer, shader program management, present pass
- `src/platform/`: SDL/OpenGL window bootstrap
- `shaders/`: Compute + present shaders
- `assets/`: Included Freedoom WAD data and related docs

## Notes

- The project currently targets a fast prototype loop and may evolve quickly.
- If CMake integration in your IDE is flaky, terminal CMake configure/build works reliably.

## Credits and Licensing

- Project code: see repository license policy (add/update your preferred LICENSE file)
- Included game assets/documents: see files in `assets/`:
  - `COPYING.txt`
  - `CREDITS.txt`
  - `CREDITS-MUSIC.txt`

Freedoom data and related attribution/licensing are documented in those asset files.
