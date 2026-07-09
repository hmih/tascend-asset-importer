#!/usr/bin/env python3
"""
Orchestrate terrain heightmap extraction from .fmap files.

Runs two external tools per terrain .fmap:
  1. .NET TerrainExtractor  → .terrain.bin + .terrain.json
  2. convert_terrain.py     → .terrain.gltf

Output is placed at output/gltf/maps/<MapName>/<MapName>_Ter.terrain.gltf
which matches what the Bevy MapViewerPlugin expects.
"""

from __future__ import annotations

import argparse
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Dict, List, Optional, Tuple

_MAP_VARIANT_PREFIXES = re.compile(r'^(TrCTF-|TrCTFBlitz-|TrArena-|TrRabbit-|TrTeamRabbit-|TrCaH-|TrTraining-)')


def _base_map_name(name: str) -> str:
    for suffix in ['_Sound', '_Ter', '_TER', '_Visuals', '_Cameras']:
        if name.endswith(suffix):
            name = name[:-len(suffix)]
            break
    return name


def discover_terrain_fmaps(maps_root: str) -> Dict[str, List[str]]:
    """Scan for *_Ter.fmap and *_TER.fmap, group by logical map name.
    Returns {logical_map_name: [fmap_paths]}.
    """
    fmaps: Dict[str, List[str]] = {}
    for root, _, files in os.walk(maps_root):
        for f in files:
            if not (f.endswith('_Ter.fmap') or f.endswith('_TER.fmap')):
                continue
            full = os.path.join(root, f)
            name = _MAP_VARIANT_PREFIXES.sub('', f.replace('.fmap', ''))
            name = _base_map_name(name)
            fmaps.setdefault(name, []).append(full)
    return fmaps


def _pick_main_fmap(fmap_paths: List[str]) -> str:
    """Prefer the fmap without mode prefix (the main terrain)."""
    unprefixed = [p for p in fmap_paths if not _MAP_VARIANT_PREFIXES.match(os.path.basename(p))]
    if unprefixed:
        return unprefixed[0]
    return fmap_paths[0]


def _repo_root() -> str:
    return os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..', '..'))


def _slicer_project() -> str:
    return os.path.join(_repo_root(), 'src', 'importer', 'slicer')


def _convert_script() -> str:
    return os.path.join(_repo_root(), 'scripts', 'convert_terrain.py')


def extract_one(fmap_path: str, output_gltf: str, temp_dir: str) -> bool:
    """Run the .NET terrain extractor then the Python converter."""
    base_name = os.path.splitext(os.path.basename(fmap_path))[0]
    ter_base = os.path.join(temp_dir, base_name)

    # Step 1: .NET TerrainExtractor
    result = subprocess.run(
        [
            'dotnet', 'run', '--project', _slicer_project(), '--',
            'terrain-extract', fmap_path, temp_dir,
        ],
        capture_output=True, text=True,
    )
    if result.returncode != 0:
        print(f"  ERROR (.NET): {result.stderr.strip()[:200]}")
        return False

    bin_path = ter_base + '.terrain.bin'
    json_path = ter_base + '.terrain.json'
    if not (os.path.exists(bin_path) and os.path.exists(json_path)):
        print(f"  ERROR: .NET extractor did not produce output files")
        return False

    # Step 2: Python convert_terrain.py
    os.makedirs(os.path.dirname(output_gltf), exist_ok=True)
    result = subprocess.run(
        ['python3', _convert_script(), ter_base, output_gltf],
        capture_output=True, text=True,
    )
    if result.returncode != 0:
        print(f"  ERROR (Python): {result.stderr.strip()[:200]}")
        return False

    print(f"  {result.stdout.strip()}")
    return True


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Extract terrain heightmaps from .fmap files",
    )
    parser.add_argument('--all', action='store_true', help='Process all maps with terrain .fmap files')
    parser.add_argument('--map', help='Process a single logical map name')
    parser.add_argument('--maps-root', default='original/TribesGame/CookedPC/Maps',
                        help='Root directory containing map .fmap files')
    parser.add_argument('--output-dir', default='output/gltf/maps',
                        help='Output directory for .terrain.gltf files')
    args = parser.parse_args()

    repo_root = _repo_root()
    maps_root = os.path.join(repo_root, args.maps_root)
    output_dir = os.path.join(repo_root, args.output_dir)

    if not os.path.isdir(maps_root):
        print(f"ERROR: Maps root not found: {maps_root}")
        sys.exit(1)

    fmaps = discover_terrain_fmaps(maps_root)

    if args.all:
        targets = sorted(fmaps.keys())
    elif args.map:
        if args.map not in fmaps:
            print(f"Map '{args.map}' not found. Available: {', '.join(sorted(fmaps.keys()))}")
            sys.exit(1)
        targets = [args.map]
    else:
        parser.error("Either --all or --map is required")

    print(f"Found {len(fmaps)} maps with terrain .fmap files")
    print(f"Processing {len(targets)} map(s)\n")

    with tempfile.TemporaryDirectory(prefix='terrain_') as temp_dir:
        succeeded = 0
        skipped = 0
        failed = 0

        for map_name in targets:
            fmap_paths = fmaps[map_name]
            fmap_path = _pick_main_fmap(fmap_paths)
            fmap_base = os.path.basename(fmap_path)

            output_gltf = os.path.join(output_dir, map_name, f"{map_name}_Ter.terrain.gltf")

            if os.path.exists(output_gltf):
                print(f"  {map_name}: already exists, skipping")
                skipped += 1
                continue

            print(f"  {map_name}: {fmap_base}")
            if len(fmap_paths) > 1:
                others = [os.path.basename(p) for p in fmap_paths if p != fmap_path]
                print(f"    (skipping variants: {', '.join(others)})")

            if extract_one(fmap_path, output_gltf, temp_dir):
                succeeded += 1
            else:
                failed += 1

        print(f"\nDone. {succeeded} succeeded, {skipped} skipped, {failed} failed.")


if __name__ == '__main__':
    main()
