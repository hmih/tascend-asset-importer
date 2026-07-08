# tascend_importer

Native C++ asset import pipeline for Tribes: Ascend (Unreal Engine 3).

## Overview

Replaces the previous Lima VM / qemu-i386 / Python / C# / Rust multi-tool chain
with a single C++ pipeline that drives UModel's library API directly.

## Building

```bash
# Prerequisites (Homebrew):
#   brew install cmake ninja simde sdl2 libpng

cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(sysctl -n hw.ncpu)
```

## Usage

```bash
# List all game packages
./build/tascend_importer scan \
    --game-path=original/TribesGame/CookedPC

# Full pipeline: extract raw assets + convert to glTF
./build/tascend_importer all \
    --game-path=original/TribesGame/CookedPC \
    --output=src/importer/output

# Process a single map end-to-end
./build/tascend_importer map Perdition \
    --game-path=original/TribesGame/CookedPC

# Skip stages as needed
./build/tascend_importer all --skip-skeletal
```

## Pipeline stages

1. **Raw extraction** — Drives UModel's `ExportPackages()` API to export
   textures (PNG from .tfc), materials (.mat slot refs), meshes (PSK/PSKX),
   sounds (OGG), and animations. Output: `output/raw/`

2. **glTF conversion** — Parses PSK files, resolves material → texture
   references, and emits glTF 2.0 with real materials and texture references.
   Output: `output/gltf/`

3. **Skeletal meshes** — Shells out to native macOS Blender with the
   `io_scene_psk_psa` addon for PSK+PSA → GLB conversion.

## Architecture

```
src/importer/
├── CMakeLists.txt
├── include/tascend/
│   ├── pipeline.hh          # Top-level orchestration
│   ├── package_scanner.hh   # Walk .u/.upk/.fmap files
│   ├── export_driver.hh     # Drives UModel's ExportPackages() API
│   ├── material_resolver.hh # Resolve .mat refs → texture paths
│   ├── psk_reader.hh        # PSK/PSA binary parser
│   ├── gltf_writer.hh       # glTF 2.0 emission with materials
│   ├── map_assembler.hh     # Merge mesh glTFs into combined map glTF
│   └── blender_bridge.hh    # Native Blender for skeletal meshes
├── src/
│   ├── main.cpp             # CLI entry point
│   ├── pipeline.cpp
│   ├── package_scanner.cpp
│   ├── export_driver.cpp    # Links UModel source directly
│   ├── material_resolver.cpp
│   ├── psk_reader.cpp
│   ├── gltf_writer.cpp
│   ├── map_assembler.cpp
│   └── blender_bridge.cpp
└── scripts/
    └── convert_skeletal.py  # Blender headless script (from scripts/)
```

## Dependencies

- **UModel** — vendored at `vendor/UEViewer/` (fork: `hmih/UEViewer`, branch `hmih/tascend`)
- **UELib** — vendored at `vendor/UELib/` (fork: `hmih/uelib`, branch `hmih/tascend`)
- **SIMDE** — portable SSE intrinsics for arm64
- **SDL2** — provides headers for UModel's RENDERING=1 declarations
- **libpng** — texture encoding
- **zlib** — package decompression
- **Blender** — only for skeletal mesh + animation → GLB conversion
