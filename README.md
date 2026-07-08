# tascend_importer

Native C++ asset import pipeline for Tribes: Ascend (Unreal Engine 3).

Replaces the old Lima VM / qemu-i386 / Python / C# / Rust chain with a single C++
binary that drives UModel's library API and shells out to UELib for actor extraction.

## Quick start

```bash
# 1. Clone with submodules
git clone --recurse-submodules git@github.com:hmih/tascend-asset-importer.git
cd tascend-asset-importer

# 2. Build inside nix shell
nix-shell --pure
mkdir build && cd build
cmake .. -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
ninja

# 3. Run the pipeline
./tascend_importer all \
    --game-path=/path/to/TribesGame/CookedPC \
    --output=output
```

All build dependencies (cmake, ninja, simde, SDL2, libpng, zlib, lzo, .NET SDK)
are provided by `shell.nix`. No system packages needed beyond Nix.

## Commands

```
tascend_importer [options] <command>

Commands:
  scan             List all packages found in the game directory
  extract          Run UModel raw extraction stage
  convert          Run glTF conversion stage (PSKx → glTF)
  dedup            Deduplicate identical raw files via hardlinks
  extract-actors   Extract actor placements from .fmap files (UELib)
  all              Core pipeline: extract → convert → skeletal
  map <name>       Process a single map end-to-end

Options:
  --game-path=<dir>     Path to CookedPC/ (default: original/TribesGame/CookedPC)
  --output=<dir>        Output root (default: output)
  --blender=<path>      Blender binary (default: /Applications/Blender.app/...)
  --skip-extract        Skip raw extraction stage
  --skip-convert        Skip glTF conversion stage
  --skip-skeletal       Skip skeletal mesh (Blender) stage
  --verbose             Verbose output
```

## Pipeline stages

1. **Raw extraction** — Drives UModel's `ExportPackages()` API to export
   textures (PNG from .tfc), materials (.mat), static meshes (PSKx),
   skeletal meshes (PSK), animations (PSA), and sounds (OGG).
   Output: `output/raw/`

2. **Dedup** — Scans raw output for byte-identical files and replaces
   duplicates with hardlinks, saving significant disk space.

3. **Static mesh conversion** — Parses PSKx files, resolves material→texture
   references, and emits glTF 2.0 with materials. Output: `output/gltf/static-meshes/`

4. **Skeletal mesh conversion** — Shells out to Blender with the
   `io_scene_psk_psa` addon for PSK+PSA → GLB. Output: `output/gltf/skeletal-meshes/`

5. **Actor extraction** — Shells out to the UELib .NET MapExtractor to parse
   actor placements and properties from .fmap files. Output: `output/gltf/actors/`

## Architecture

```
├── CMakeLists.txt
├── shell.nix                  # Nix development shell
├── include/tascend/
│   ├── pipeline.hh            # Top-level orchestration
│   ├── package_scanner.hh     # Walk .u/.upk/.fmap files
│   ├── export_driver.hh       # Drives UModel's ExportPackages() API
│   ├── material_resolver.hh   # Resolve .mat refs → texture paths
│   ├── psk_reader.hh          # PSK/PSKx/PSA binary parser
│   ├── gltf_writer.hh         # glTF 2.0 emission with materials
│   ├── map_assembler.hh       # Merge mesh glTFs into combined map glTF
│   ├── blender_bridge.hh      # Native Blender for skeletal meshes
│   ├── actor_extractor.hh     # C++ FPropertyTag parser (experimental)
│   └── terrain_reader.hh      # Terrain heightmap reader (WIP)
├── src/
│   ├── main.cpp               # CLI entry point
│   ├── pipeline.cpp
│   ├── package_scanner.cpp
│   ├── export_driver.cpp      # Links UModel source directly
│   ├── material_resolver.cpp
│   ├── psk_reader.cpp
│   ├── gltf_writer.cpp
│   ├── map_assembler.cpp
│   ├── blender_bridge.cpp
│   ├── actor_extractor.cpp
│   └── terrain_reader.cpp
├── slicer/                    # .NET MapExtractor (UELib wrapper)
│   ├── Decompiler.csproj
│   ├── MapExtractor.cs
│   └── ...
├── scripts/
│   ├── convert_skeletal.py    # Blender headless PSK+PSA → GLB
│   └── match_psa.py           # PSK/PSA pairing heuristics
└── vendor/
    ├── UEViewer/              # UModel fork (submodule)
    ├── UELib/                 # .NET UE3 package reader (submodule)
    └── psk_addon/             # Blender PSK/PSA import addon
```

## Dependencies

| Dependency | Purpose | Source |
|------------|---------|--------|
| **UModel** | UE3 package reader, raw asset export | `vendor/UEViewer/` (submodule, branch `hmih/tascend`) |
| **UELib** | .NET UE3 property deserializer | `vendor/UELib/` (submodule, branch `hmih/tascend`) |
| **.NET SDK 10** | Builds the UELib MapExtractor | `shell.nix` |
| **SIMDE** | Portable SSE intrinsics on arm64 | `shell.nix` |
| **SDL2** | Headers for UModel declarations | `shell.nix` |
| **libpng** | Texture encoding | `shell.nix` |
| **zlib** | Package decompression | `shell.nix` |
| **lzo** | LZO decompression support | `shell.nix` |
| **Blender** | Skeletal mesh PSK+PSA → GLB | macOS app, external |
