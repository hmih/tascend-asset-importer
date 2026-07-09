#!/usr/bin/env python3
"""
Convert UELib actor JSONs → Bevy .scene.actors.json format.

UELib format:
  {"objects": [{"name": ..., "class": ..., "properties": [{"name": ..., "value": ...}]}]}

Bevy format:
  {"map": "MapName", "actor_count": N, "actors": [{"name": ..., "class": ..., "location": [x,y,z], "rotation": [x,y,z,w], "properties": {key: value}}]}

Location/Rotation stored in UE3 space (z-up, left-handed). Bevy converts at runtime.
"""

from __future__ import annotations

import argparse
import json
import math
import os
import re
import sys
from typing import Any, Dict, List, Optional, Tuple

TWO_PI = 2.0 * math.pi
UNREAL_ROT_UNITS = 65536.0

_VEC3_RE = re.compile(r'X=([\-\d.]+)\s*,?\s*Y=([\-\d.]+)\s*,?\s*Z=([\-\d.]+)')
_ROTATOR_RE = re.compile(r'Pitch=([\-\d]+)\s*,?\s*Yaw=([\-\d]+)\s*,?\s*Roll=([\-\d]+)')

# Objects to skip (containers, components, static mesh actors handled by glTF)
_SKIP_CLASSES = {'World', 'Level'}
_SKIP_CLASS_PATTERNS = ['Component', 'StaticMeshActor', 'InterpActor', 'Emitter', 'UTActor',
                        'TrActor', 'DynamicTriggerVolume', 'DynamicBlockingVolume',
                        'DynamicPhysicsVolume', 'Manta', 'GravCycle', 'Beowulf', 'Shrike',
                        'SaberLauncher', 'HERC', 'Scorpion']

_EVIL_BYTES = b'\x07\x00\x00\x00\x0b\x00\x00\x00\xff\xff\xff\xff'


def _clean_bytes(s: str) -> str:
    sd = _EVIL_BYTES.decode('latin-1')
    return s.replace(sd, '')


def parse_vec3(value: str) -> Optional[Tuple[float, float, float]]:
    m = _VEC3_RE.search(value)
    return (float(m[1]), float(m[2]), float(m[3])) if m else None


def parse_rotator(value: str) -> Optional[Tuple[int, int, int]]:
    m = _ROTATOR_RE.search(value)
    return (int(m[1]), int(m[2]), int(m[3])) if m else None


def rotator_to_quat(pitch_units: int, yaw_units: int, roll_units: int) -> Tuple[float, float, float, float]:
    def _half_angle(units: int) -> Tuple[float, float]:
        rad = (units / UNREAL_ROT_UNITS) * TWO_PI
        half = rad / 2.0
        return math.cos(half), math.sin(half)

    cy, sy = _half_angle(yaw_units)
    cp, sp = _half_angle(pitch_units)
    cr, sr = _half_angle(roll_units)

    # q = Roll * Pitch * Yaw
    qx = cr * cp * cy + sr * sp * sy
    qy = sr * cp * cy - cr * sp * sy
    qz = cr * sp * cy + sr * cp * sy
    qw = cr * cp * cy - sr * sp * sy
    return (qx, qy, qz, qw)


def should_skip(class_: str) -> bool:
    if class_ in _SKIP_CLASSES:
        return True
    for pattern in _SKIP_CLASS_PATTERNS:
        if pattern in class_:
            return True
    return False


def convert_objects(objects: List[Dict], clean_bytes: bool = True) -> List[Dict[str, Any]]:
    actors: List[Dict[str, Any]] = []

    for obj in objects:
        class_ = obj.get('class', '')
        name = obj.get('name', '')

        if should_skip(class_):
            continue

        props = obj.get('properties', [])
        props_dict: Dict[str, str] = {}
        location: Optional[Tuple[float, float, float]] = None
        rotation: Optional[Tuple[float, float, float, float]] = None

        for p in props:
            pname = p.get('name', '')
            pval = p.get('value', '')
            if clean_bytes:
                pval = _clean_bytes(pval)

            if pname == 'Location':
                v = parse_vec3(pval)
                if v:
                    location = v
            elif pname == 'Rotation':
                r = parse_rotator(pval)
                if r:
                    rotation = rotator_to_quat(*r)

            props_dict[pname] = pval

        if location is None:
            continue

        actors.append({
            'name': name,
            'class': class_,
            'location': list(location),
            'rotation': list(rotation or (0.0, 0.0, 0.0, 1.0)),
            'properties': props_dict,
        })

    return actors


def convert_file(input_path: str, map_name: str, output_path: str) -> int:
    with open(input_path, 'r') as f:
        data = json.load(f)

    objects = data.get('objects', [])
    actors = convert_objects(objects)

    output = {
        'map': map_name,
        'actor_count': len(actors),
        'actors': actors,
    }

    os.makedirs(os.path.dirname(output_path), exist_ok=True)
    with open(output_path, 'w') as f:
        json.dump(output, f, indent=2)

    print(f"  {os.path.basename(input_path)}: {len(actors)} actors → {output_path}")
    return 0


_MAP_VARIANT_PREFIXES = re.compile(r'^(TrCTF-|TrCTFBlitz-|TrArena-|TrRabbit-|TrTeamRabbit-|TrCaH-|TrTraining-)')


def discover_maps(actors_dir: str) -> Dict[str, List[str]]:
    maps: Dict[str, List[str]] = {}
    if not os.path.isdir(actors_dir):
        return maps
    for fname in sorted(os.listdir(actors_dir)):
        if not fname.endswith('.actors.json'):
            continue
        name = _MAP_VARIANT_PREFIXES.sub('', fname.replace('.actors.json', ''))
        for suffix in ['_Sound', '_Ter', '_TER', '_Visuals', '_Cameras']:
            if name.endswith(suffix):
                name = name[:-len(suffix)]
                break
        maps.setdefault(name, []).append(os.path.join(actors_dir, fname))
    return maps


def main() -> None:
    parser = argparse.ArgumentParser(description="Convert UELib actor JSONs to Bevy .scene.actors.json")
    parser.add_argument("--actors-dir", default="output/gltf/actors", help="Directory containing UELib .actors.json files")
    parser.add_argument("--output-dir", default="output/gltf/maps", help="Output directory for .scene.actors.json files")
    parser.add_argument("--all", action="store_true", help="Convert all maps")
    parser.add_argument("--map", help="Convert a single logical map name (requires --all or --actors-dir)")
    args = parser.parse_args()

    if not args.all and not args.map:
        parser.error("Either --all or --map is required")

    if args.all:
        maps = discover_maps(args.actors_dir)
        if not maps:
            print(f"No .actors.json files found in {args.actors_dir}")
            sys.exit(1)
        print(f"Found {len(maps)} maps")
        failed = 0
        for map_name, json_paths in sorted(maps.items()):
            print(f"Map: {map_name} ({len(json_paths)} files)")
            all_actors: List[Dict[str, Any]] = []
            for jp in json_paths:
                with open(jp, 'r') as f:
                    data = json.load(f)
                objects = data.get('objects', [])
                all_actors.extend(convert_objects(objects))

            output_path = os.path.join(args.output_dir, map_name, map_name + ".scene.actors.json")
            os.makedirs(os.path.dirname(output_path), exist_ok=True)
            output = {'map': map_name, 'actor_count': len(all_actors), 'actors': all_actors}
            with open(output_path, 'w') as f:
                json.dump(output, f, indent=2)
            print(f"  → {output_path} ({len(all_actors)} actors)")

        print(f"\nDone. {len(maps)} maps processed, {failed} failed.")
        sys.exit(0 if failed == 0 else 1)

    if args.map:
        maps = discover_maps(args.actors_dir)
        if args.map not in maps:
            print(f"Map '{args.map}' not found in {args.actors_dir}")
            sys.exit(1)
        json_paths = maps[args.map]
        all_actors = []
        for jp in json_paths:
            with open(jp, 'r') as f:
                data = json.load(f)
            objects = data.get('objects', [])
            all_actors.extend(convert_objects(objects))

        output_path = os.path.join(args.output_dir, args.map, args.map + ".scene.actors.json")
        os.makedirs(os.path.dirname(output_path), exist_ok=True)
        output = {'map': args.map, 'actor_count': len(all_actors), 'actors': all_actors}
        with open(output_path, 'w') as f:
            json.dump(output, f, indent=2)
        print(f"Converted {args.map}: {len(all_actors)} actors → {output_path}")
        sys.exit(0)


if __name__ == "__main__":
    main()
