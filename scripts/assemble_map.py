#!/usr/bin/env python3
"""
Map assembler: reads UELib actor JSON, resolves mesh references,
merges individual mesh glTFs into a combined map glTF with instanced nodes.

Usage:
  python3 scripts/assemble_map.py <actors.json> [--output <dir>] [--static-meshes <dir>] [--skeletal-meshes <dir>]
"""

from __future__ import annotations

import argparse
import json
import math
import os
import re
import sys
from dataclasses import dataclass, field
from enum import Enum
from pathlib import Path
from typing import Any, Dict, Iterable, List, Optional, Tuple


# ═══════════════════════════════════════════════════════════════════════════
# Geometry types
# ═══════════════════════════════════════════════════════════════════════════

@dataclass(frozen=True)
class Vec3:
    x: float
    y: float
    z: float

    def __iter__(self):
        return iter((self.x, self.y, self.z))

    @staticmethod
    def zero() -> Vec3:
        return Vec3(0.0, 0.0, 0.0)

    @staticmethod
    def one() -> Vec3:
        return Vec3(1.0, 1.0, 1.0)


@dataclass(frozen=True)
class Quat:
    x: float
    y: float
    z: float
    w: float

    def __iter__(self):
        return iter((self.x, self.y, self.z, self.w))

    @staticmethod
    def identity() -> Quat:
        return Quat(0.0, 0.0, 0.0, 1.0)

    def conjugate(self) -> Quat:
        return Quat(-self.x, -self.y, -self.z, self.w)

    def __mul__(self, other: Quat) -> Quat:
        """Quaternion multiplication (Hamilton product)."""
        ax, ay, az, aw = self.x, self.y, self.z, self.w
        bx, by, bz, bw = other.x, other.y, other.z, other.w
        return Quat(
            aw * bx + ax * bw + ay * bz - az * by,
            aw * by - ax * bz + ay * bw + az * bx,
            aw * bz + ax * by - ay * bx + az * bw,
            aw * bw - ax * bx - ay * by - az * bz,
        )

    @staticmethod
    def from_axis_angle(x: float, y: float, z: float, angle: float) -> Quat:
        half = angle / 2.0
        s = math.sin(half)
        return Quat(x * s, y * s, z * s, math.cos(half))


# ═══════════════════════════════════════════════════════════════════════════
# Domain types
# ═══════════════════════════════════════════════════════════════════════════

class MeshType(Enum):
    STATIC = "static"
    SKELETAL = "skeletal"


@dataclass(frozen=True)
class MeshInstance:
    """A single placed mesh instance from an actor JSON."""
    mesh_type: MeshType
    mesh_ref: str          # e.g. "DEP_ForceField_3p.Models.ASE_ForceField_Collision"
    location: Vec3
    rotation: Optional[Quat]  # UE3 rotator converted to quaternion
    scale: Vec3
    actor_name: str
    actor_class: str


@dataclass(frozen=True)
class GltfFile:
    """Parsed individual mesh glTF file."""
    path: str
    json: Dict[str, Any]
    mesh_count: int = field(init=False)

    def __post_init__(self):
        object.__setattr__(self, 'mesh_count', len(self.json.get('meshes', [])))

    @property
    def meshes(self) -> List[Dict[str, Any]]:
        return self.json.get('meshes', [])

    @property
    def accessors(self) -> List[Dict[str, Any]]:
        return self.json.get('accessors', [])

    @property
    def buffer_views(self) -> List[Dict[str, Any]]:
        return self.json.get('bufferViews', [])

    @property
    def buffers(self) -> List[Dict[str, Any]]:
        return self.json.get('buffers', [])

    @property
    def materials(self) -> List[Dict[str, Any]]:
        return self.json.get('materials', [])

    @property
    def textures(self) -> List[Dict[str, Any]]:
        return self.json.get('textures', [])

    @property
    def images(self) -> List[Dict[str, Any]]:
        return self.json.get('images', [])

    @property
    def directory(self) -> str:
        return os.path.dirname(self.path)


# ═══════════════════════════════════════════════════════════════════════════
# UE3 ↔ glTF coordinate conversion
# ═══════════════════════════════════════════════════════════════════════════

C_QUAT: Quat = Quat.from_axis_angle(1.0, 0.0, 0.0, math.pi / 2.0)  # +90° X rotation
TWO_PI: float = 2.0 * math.pi
UNREAL_ROT_UNITS: float = 65536.0  # per full circle


def ue3_pos_to_gltf(pos: Vec3) -> Vec3:
    """Convert UE3 (X=fwd, Y=right, Z=up) → glTF (X=right, Y=up, Z=-fwd)."""
    return Vec3(pos.x, pos.z, -pos.y)


def ue3_scale_to_gltf(scale: Vec3) -> Vec3:
    """Remap UE3 scale axes to glTF axes."""
    return Vec3(scale.x, scale.z, scale.y)


def ue3_rotator_to_quat(pitch_units: int, yaw_units: int, roll_units: int) -> Quat:
    """Convert UE3 rotator (Pitch/Yaw/Roll in Unreal rotation units) → UE3 quaternion.

    UE3 rotation order: Yaw(Z) → Pitch(Y) → Roll(X), i.e. q = Roll * Pitch * Yaw.
    The root node matrix handles UE3→glTF conversion, so quaternions stay in UE3 space.
    """
    def _half_angle(units: int) -> Tuple[float, float]:
        rad = (units / UNREAL_ROT_UNITS) * TWO_PI
        half = rad / 2.0
        return math.cos(half), math.sin(half)

    cy, sy = _half_angle(yaw_units)    # Z axis
    cp, sp = _half_angle(pitch_units)  # Y axis
    cr, sr = _half_angle(roll_units)   # X axis

    q_yaw   = Quat(0.0, 0.0, sy, cy)
    q_pitch = Quat(0.0, sp, 0.0, cp)
    q_roll  = Quat(sr, 0.0, 0.0, cr)

    return q_roll * q_pitch * q_yaw


# ═══════════════════════════════════════════════════════════════════════════
# T3D property parsing (pure functions)
# ═══════════════════════════════════════════════════════════════════════════

_VEC3_RE = re.compile(r'X=([\-\d.]+)\s*,?\s*Y=([\-\d.]+)\s*,?\s*Z=([\-\d.]+)')
_ROTATOR_RE = re.compile(r'Pitch=([\-\d]+)\s*,?\s*Yaw=([\-\d]+)\s*,?\s*Roll=([\-\d]+)')
_FLOAT_RE = re.compile(r'=([\-\d.]+)')
_MESH_REF_RE = re.compile(r"(StaticMesh|SkeletalMesh)=\1'([^']+)'")
_COMPONENT_REF_RE = re.compile(r'(\w+)=(\w+_\d+)\s*$')
_MAP_VARIANT_PREFIXES = re.compile(
    r'^(TrCTF-|TrCTFBlitz-|TrArena-|TrRabbit-|TrTeamRabbit-|TrCaH-|TrTraining-)'
)
_LAYER_SUFFIXES = ['_Sound', '_Ter', '_TER', '_Visuals', '_Cameras', '_Visuals']


def parse_vec3(value: str) -> Optional[Vec3]:
    """Parse a UE3 vector string: (X=1.0,Y=2.0,Z=3.0)."""
    m = _VEC3_RE.search(value)
    return Vec3(float(m[1]), float(m[2]), float(m[3])) if m else None


def parse_rotator(value: str) -> Optional[Tuple[int, int, int]]:
    """Parse a UE3 rotator: (Pitch=...,Yaw=...,Roll=...). Returns (pitch, yaw, roll) in UR units."""
    m = _ROTATOR_RE.search(value)
    return (int(m[1]), int(m[2]), int(m[3])) if m else None


def parse_float(value: str) -> Optional[float]:
    """Parse a UE3 float: DrawScale=3.0."""
    m = _FLOAT_RE.search(value)
    return float(m[1]) if m else None


def parse_mesh_refs(value: str) -> List[Tuple[MeshType, str]]:
    """Extract StaticMesh/SkeletalMesh references from a property value string.

    Returns list of (MeshType, 'Package.Group.Name') tuples.
    """
    refs: List[Tuple[MeshType, str]] = []
    for kind_str, path in _MESH_REF_RE.findall(value):
        mt = MeshType.SKELETAL if kind_str == 'SkeletalMesh' else MeshType.STATIC
        refs.append((mt, path))
    return refs


def parse_component_ref(value: str) -> Optional[Tuple[str, str]]:
    """Extract a component object reference from the end of a property value.

    e.g. "StaticMeshComponent=StaticMeshComponent_768" → ("StaticMeshComponent", "StaticMeshComponent_768")
    """
    m = _COMPONENT_REF_RE.search(value)
    return (m[1], m[2]) if m else None


# ═══════════════════════════════════════════════════════════════════════════
# Actor extraction (pure functions over immutable data)
# ═══════════════════════════════════════════════════════════════════════════

@dataclass
class ActorJsonObject:
    name: str
    class_: str
    outer: str
    properties: List[Dict[str, str]]


def _load_actor_objects(path: str) -> List[ActorJsonObject]:
    """Load all objects from a UELib .actors.json file."""
    with open(path, 'r') as f:
        data = json.load(f)
    return [
        ActorJsonObject(
            name=obj.get('name', ''),
            class_=obj.get('class', ''),
            outer=obj.get('outer', '') or '',
            properties=obj.get('properties', []),
        )
        for obj in data.get('objects', [])
    ]


def _build_component_mesh_index(objects: List[ActorJsonObject]) -> Dict[str, List[Tuple[MeshType, str]]]:
    """Pre-index child component objects by their mesh references.

    Scans each object's properties for StaticMesh/SkeletalMesh references.
    Returns: {component_name: [(MeshType, 'Package.Group.Name'), ...]}

    Component names are scoped by their outer (parent) name to avoid
    collisions between actors (e.g. two actors can each have a child
    named 'StaticMeshComponent_1' with different meshes).
    """
    index: Dict[str, List[Tuple[MeshType, str]]] = {}
    for obj in objects:
        refs: List[Tuple[MeshType, str]] = []
        for prop in obj.properties:
            pname = prop.get('name', '')
            pval = prop.get('value', '')
            if pname in ('StaticMesh', 'SkeletalMesh') and pval:
                refs.extend(parse_mesh_refs(pval))
            elif 'StaticMesh=' in pval or 'SkeletalMesh=' in pval:
                refs.extend(parse_mesh_refs(pval))
        if refs:
            key = f"{obj.outer}.{obj.name}" if obj.outer else obj.name
            index[key] = refs
    return index


def _extract_transform(props: List[Dict[str, str]]) -> Tuple[Optional[Vec3], Optional[Tuple[int, int, int]], float, Vec3]:
    """Extract Location, Rotation, DrawScale, DrawScale3D from properties.

    Returns (location, rotator, draw_scale, draw_scale3d).
    """
    location: Optional[Vec3] = None
    rotator: Optional[Tuple[int, int, int]] = None
    draw_scale: float = 1.0
    draw_scale3d: Vec3 = Vec3.one()

    for prop in props:
        pname = prop.get('name', '')
        pval = prop.get('value', '')
        ptype = prop.get('type', '')

        if pname == 'Location' and ptype == 'StructProperty':
            v = parse_vec3(pval)
            if v:
                location = v
        elif pname == 'Rotation' and ptype == 'StructProperty':
            r = parse_rotator(pval)
            if r:
                rotator = r
        elif pname == 'DrawScale' and ptype == 'FloatProperty':
            s = parse_float(pval)
            if s is not None:
                draw_scale = s
        elif pname == 'DrawScale3D' and ptype == 'StructProperty':
            v = parse_vec3(pval)
            if v:
                draw_scale3d = v

    return location, rotator, draw_scale, draw_scale3d


def _find_mesh_refs(
    obj: ActorJsonObject,
    component_mesh_index: Dict[str, List[Tuple[MeshType, str]]],
) -> List[Tuple[MeshType, str]]:
    """Find all mesh references for an actor by scanning its properties and
    following component references to child objects."""
    seen: set = set()
    refs: List[Tuple[MeshType, str]] = []

    for prop in obj.properties:
        pval = prop.get('value', '')

        # Direct inline references
        for mt, path in parse_mesh_refs(pval):
            key = (mt, path)
            if key not in seen:
                seen.add(key)
                refs.append((mt, path))

        # Component references (e.g. StaticMeshComponent=StaticMeshComponent_768)
        comp_ref = parse_component_ref(pval)
        if comp_ref:
            type_name, comp_name = comp_ref
            # Try scoped lookup first (ActorName.ComponentName), then bare name
            scoped = f"{obj.name}.{comp_name}" if obj.name else comp_name
            for key in (scoped, comp_name):
                if key in component_mesh_index:
                    for mt, path in component_mesh_index[key]:
                        k = (mt, path)
                        if k not in seen:
                            seen.add(k)
                            refs.append((mt, path))
                    break

    return refs


def extract_mesh_instances(actors_json_path: str) -> List[MeshInstance]:
    """Parse a UELib .actors.json and extract all placed mesh instances."""
    objects = _load_actor_objects(actors_json_path)
    component_mesh_index = _build_component_mesh_index(objects)

    instances: List[MeshInstance] = []

    for obj in objects:
        location, rotator, draw_scale, draw_scale3d = _extract_transform(obj.properties)
        if location is None:
            continue

        mesh_refs = _find_mesh_refs(obj, component_mesh_index)
        if not mesh_refs:
            continue

        rotation = ue3_rotator_to_quat(*rotator) if rotator else None
        scale = Vec3(
            draw_scale3d.x * draw_scale,
            draw_scale3d.y * draw_scale,
            draw_scale3d.z * draw_scale,
        )

        for mesh_type, mesh_ref in mesh_refs:
            instances.append(MeshInstance(
                mesh_type=mesh_type,
                mesh_ref=mesh_ref,
                location=location,
                rotation=rotation,
                scale=scale,
                actor_name=obj.name,
                actor_class=obj.class_,
            ))

    return instances


# ═══════════════════════════════════════════════════════════════════════════
# Map discovery: group actor JSONs by base map name
# ═══════════════════════════════════════════════════════════════════════════

def discover_maps(actors_dir: str) -> Dict[str, List[str]]:
    """Scan an actors directory and group .actors.json files by map variant.

    Each game mode (CTF, Rabbit, Blitz, etc.) is assembled as a separate map.
    Base layers (files without a game-mode prefix, e.g. ``ArxNovena_TER``) are
    shared across all variants of the same base map.

    Naming convention:
      - ``ArxNovena_TER.actors.json``         → base layer of ArxNovena
      - ``ArxNovena_Sound.actors.json``       → base layer of ArxNovena
      - ``TrCTF-ArxNovena.actors.json``        → CTF actors (default variant)
      - ``TrRabbit-ArxNovena.actors.json``     → Rabbit variant
      - ``TrTeamRabbit-ArxNovena_Ter.actors.json`` → TeamRabbit terrain layer

    Output map names:
      - CTF (default): ``ArxNovena``
      - Other modes:   ``TrRabbit-ArxNovena``, ``TrCTFBlitz-ArxNovena``, etc.

    Each variant map includes all base layers + its own mode-specific layers.
    """
    if not os.path.isdir(actors_dir):
        return {}

    # Parse each file into (base_name, mode_prefix, full_path)
    base_files: Dict[str, List[str]] = {}   # base_name → [paths]
    variant_files: Dict[str, Dict[str, List[str]]] = {}  # base_name → {mode → [paths]}

    for fname in sorted(os.listdir(actors_dir)):
        if not fname.endswith('.actors.json'):
            continue
        name = fname.replace('.actors.json', '')
        path = os.path.join(actors_dir, fname)

        mode_match = _MAP_VARIANT_PREFIXES.match(name)
        if mode_match:
            mode = mode_match.group(0)  # e.g. "TrCTF-", "TrRabbit-"
            rest = name[len(mode):]
        else:
            mode = ""
            rest = name

        # Strip layer suffix to get the base map name
        base = rest
        for suffix in _LAYER_SUFFIXES:
            if rest.endswith(suffix):
                base = rest[:-len(suffix)]
                break

        if mode == "":
            base_files.setdefault(base, []).append(path)
        else:
            variant_files.setdefault(base, {}).setdefault(mode, []).append(path)

    # Build final map list: each variant gets base layers + its own layers
    maps: Dict[str, List[str]] = {}

    for base, modes in variant_files.items():
        shared = base_files.get(base, [])
        for mode in sorted(modes):
            # CTF is the default game mode — use the bare base name
            if mode == "TrCTF-":
                map_name = base
            else:
                map_name = f"{mode}{base}"
            maps[map_name] = shared + modes[mode]

    # Handle maps that have base files but no game-mode variants
    for base, paths in base_files.items():
        if base not in variant_files:
            maps[base] = paths

    return maps


# ═══════════════════════════════════════════════════════════════════════════
# Mesh index: resolve UE3 mesh_ref → glTF filesystem path
# ═══════════════════════════════════════════════════════════════════════════

MeshIndex = Dict[str, List[str]]  # mesh_ref → [gltf_path, ...]


def _gltf_mesh_refs(gltf_path: str) -> List[str]:
    """Derive potential UE3 mesh_refs from a glTF file's node/mesh name.
    Returns multiple candidates since the depth of the hierarchy varies."""
    try:
        with open(gltf_path, 'r') as f:
            gltf = json.load(f)
    except Exception:
        return []

    sources = [
        gltf.get('nodes', [{}])[0].get('name'),
        gltf.get('meshes', [{}])[0].get('name'),
    ]
    results: List[str] = []
    for name in (s for s in sources if s):
        parts = name.split('/')
        if len(parts) >= 2:
            results.append('.'.join(parts[-2:]))
        if len(parts) >= 3:
            results.append('.'.join(parts[-3:]))
        if len(parts) >= 4:
            results.append('.'.join(parts[-4:]))
    return results


def build_mesh_index(mesh_dirs: Iterable[str]) -> MeshIndex:
    """Scan directory trees for .gltf/.glb files and build mesh_ref → paths index."""
    index: MeshIndex = {}

    for mesh_dir in mesh_dirs:
        mesh_dir = os.path.abspath(mesh_dir)
        if not os.path.isdir(mesh_dir):
            continue
        for root, _, files in os.walk(mesh_dir):
            for fname in files:
                if not fname.endswith(('.gltf', '.glb')):
                    continue
                full_path = os.path.join(root, fname)

                mesh_refs = _gltf_mesh_refs(full_path)
                if not mesh_refs:
                    rel = os.path.relpath(full_path, mesh_dir)
                    parts = Path(rel).with_suffix('').parts
                    if len(parts) >= 4:
                        mesh_refs = ['.'.join(parts[-4:])]
                    elif len(parts) >= 3:
                        mesh_refs = ['.'.join(parts[-3:])]
                    elif len(parts) == 2:
                        mesh_refs = ['.'.join(parts[-2:])]
                    else:
                        continue

                for mesh_ref in mesh_refs:
                    index.setdefault(mesh_ref, []).append(full_path)

    return index


def resolve_mesh_ref(mesh_ref: str, mesh_type: MeshType, index: MeshIndex) -> Optional[str]:
    """Find a glTF file for a given mesh reference, preferring appropriate extension."""
    paths = index.get(mesh_ref)
    if not paths:
        return None
    if mesh_type == MeshType.SKELETAL:
        for p in paths:
            if p.endswith('.glb'):
                return p
    for p in paths:
        if p.endswith('.gltf'):
            return p
    return paths[0]


# ═══════════════════════════════════════════════════════════════════════════
# glTF merging
# ═══════════════════════════════════════════════════════════════════════════

_DEFAULT_SAMPLER = {
    "magFilter": 9729,
    "minFilter": 9987,
    "wrapS": 10497,
    "wrapT": 10497,
}


def _load_gltf(path: str) -> Optional[GltfFile]:
    try:
        with open(path, 'r') as f:
            return GltfFile(path=path, json=json.load(f))
    except Exception as e:
        print(f"  WARNING: Failed to load {path}: {e}", file=sys.stderr)
        return None


def _resolve_textures_base(static_meshes_dir: str) -> str:
    """Find the absolute textures directory.

    The C++ glTF writer references images as '../textures/<path>' relative to the
    static-meshes/ root. The textures directory has a flat per-package structure
    that doesn't match the deep raw/ paths, so we use raw/ directly.
    """
    gltf_dir = os.path.dirname(static_meshes_dir)
    output_dir = os.path.dirname(gltf_dir)
    raw_dir = os.path.join(output_dir, "raw")
    if os.path.isdir(raw_dir):
        return raw_dir
    return os.path.join(gltf_dir, "textures")


def _resolve_image_uri(uri: str, mesh_dir: str, textures_base: str) -> str:
    """Resolve an image URI from a mesh glTF to an absolute path.

    The glTF writer uses "../textures/<path>" which is relative to the
    static-meshes/ root (a bug), not to the nested mesh subdirectory.
    We detect this prefix and resolve to the actual textures directory.
    """
    if uri.startswith("../textures/"):
        rel = uri[len("../textures/"):]
        return os.path.normpath(os.path.join(textures_base, rel))
    return os.path.normpath(os.path.join(mesh_dir, uri))


def _read_mesh_buffer(gltf: GltfFile) -> bytes:
    """Read the binary buffer data referenced by a mesh glTF."""
    data = bytearray()
    for buf in gltf.buffers:
        if 'uri' in buf:
            buf_path = os.path.join(gltf.directory, buf['uri'])
            try:
                with open(buf_path, "rb") as f:
                    data.extend(f.read())
            except Exception as e:
                print(f"  WARNING: Failed to read buffer {buf_path}: {e}", file=sys.stderr)
    return bytes(data)


def _remap_material(mat: Dict[str, Any], tex_remap: Dict[int, int]) -> Dict[str, Any]:
    """Clone a material dict, remapping its texture indices."""
    result = dict(mat)
    for tex_key in ('baseColorTexture', 'metallicRoughnessTexture'):
        if 'pbrMetallicRoughness' in result and tex_key in result['pbrMetallicRoughness']:
            old = result['pbrMetallicRoughness'][tex_key]['index']
            result['pbrMetallicRoughness'][tex_key] = {'index': tex_remap.get(old, old)}
    for tex_key in ('normalTexture', 'emissiveTexture', 'occlusionTexture'):
        if tex_key in result:
            old = result[tex_key]['index']
            result[tex_key] = {'index': tex_remap.get(old, old)}
    return result


def merge_gltf_assets(
    gltf_files: List[GltfFile],
    output_path: str,
    textures_base: str,
) -> Tuple[Dict[str, Any], bytes, str, str]:
    """Merge multiple mesh glTFs into one combined glTF data structure.

    Returns (combined_json, combined_buffer, gltf_out_path, bin_out_path).
    Does NOT create instance nodes — caller is responsible.
    """
    if not gltf_files:
        raise ValueError("No glTF files to merge")

    output_dir = os.path.dirname(output_path) or "."
    os.makedirs(output_dir, exist_ok=True)

    base_name = os.path.splitext(os.path.basename(output_path))[0]
    gltf_out_path = os.path.join(output_dir, base_name + ".gltf")
    bin_out_path = os.path.join(output_dir, base_name + ".bin")

    combined: Dict[str, Any] = {
        "asset": {"version": "2.0", "generator": "tascend_importer map_assembler"},
        "scene": 0,
        "scenes": [{"nodes": []}],
        "nodes": [],
        "meshes": [],
        "accessors": [],
        "bufferViews": [],
        "buffers": [],
        "images": [],
        "textures": [],
        "materials": [],
        "samplers": [_DEFAULT_SAMPLER],
    }

    combined_buffer = bytearray()
    current_offset: int = 0

    # Deduplication indexes (immutable keys)
    image_index: Dict[str, int] = {}     # abs_path → idx
    texture_index: Dict[int, int] = {}   # source_idx → idx
    material_index: Dict[str, int] = {}  # fp_json → idx

    for gltf in gltf_files:
        bv_start = len(combined["bufferViews"])
        acc_start = len(combined["accessors"])

        buf_data = _read_mesh_buffer(gltf)

        # Buffer views
        for bv in gltf.buffer_views:
            bv = dict(bv)
            bv["byteOffset"] = bv.get("byteOffset", 0) + current_offset
            bv["buffer"] = 0
            combined["bufferViews"].append(bv)

        # Accessors
        for acc in gltf.accessors:
            acc = dict(acc)
            if "bufferView" in acc:
                acc["bufferView"] += bv_start
            combined["accessors"].append(acc)

        # Images (deduplicated by absolute resolved path)
        img_remap: Dict[int, int] = {}
        for i, img in enumerate(gltf.images):
            if 'uri' in img:
                abs_uri = _resolve_image_uri(img['uri'], gltf.directory, textures_base)
                if abs_uri in image_index:
                    img_remap[i] = image_index[abs_uri]
                else:
                    try:
                        rel_uri = os.path.relpath(abs_uri, output_dir)
                    except ValueError:
                        rel_uri = abs_uri
                    idx = len(combined["images"])
                    combined["images"].append({"uri": rel_uri})
                    image_index[abs_uri] = idx
                    img_remap[i] = idx
            elif i not in img_remap:
                idx = len(combined["images"])
                combined["images"].append(dict(img))
                img_remap[i] = idx

        # Textures (deduplicated by source image index)
        tex_remap: Dict[int, int] = {}
        for i, tex in enumerate(gltf.textures):
            new_src = img_remap.get(tex.get('source', 0), 0)
            if new_src not in texture_index:
                idx = len(combined["textures"])
                combined["textures"].append({"source": new_src, "sampler": 0})
                texture_index[new_src] = idx
            tex_remap[i] = texture_index[new_src]

        # Materials (deduplicated by JSON fingerprint)
        mat_remap: Dict[int, int] = {}
        for i, mat in enumerate(gltf.materials):
            fp = json.dumps(mat, sort_keys=True)
            if fp not in material_index:
                combined["materials"].append(_remap_material(mat, tex_remap))
                material_index[fp] = len(combined["materials"]) - 1
            mat_remap[i] = material_index[fp]

        # Meshes
        for mesh in gltf.meshes:
            mesh = dict(mesh)
            prims = []
            for prim in mesh.get("primitives", []):
                prim = dict(prim)
                prim["attributes"] = {k: v + acc_start for k, v in prim.get("attributes", {}).items()}
                if "indices" in prim:
                    prim["indices"] += acc_start
                if "material" in prim:
                    prim["material"] = mat_remap.get(prim["material"], prim["material"])
                prims.append(prim)
            mesh["primitives"] = prims
            combined["meshes"].append(mesh)

        # Advance buffer offset
        while len(combined_buffer) % 4 != 0:
            combined_buffer.extend(b'\x00')
        combined_buffer.extend(buf_data)
        current_offset = len(combined_buffer)

    combined["buffers"].append({
        "uri": base_name + ".bin",
        "byteLength": len(combined_buffer),
    })

    return combined, bytes(combined_buffer), gltf_out_path, bin_out_path


# ═══════════════════════════════════════════════════════════════════════════
# Node instantiation
# ═══════════════════════════════════════════════════════════════════════════

def _build_mesh_ref_index(
    gltf_files: List[GltfFile],
    mesh_ref_to_path: Dict[str, str],
) -> Dict[str, int]:
    """Map each mesh_ref to the combined mesh index where its glTF data starts."""
    # Build path → first mesh index lookup
    path_to_mesh_start: Dict[str, int] = {}
    mesh_idx = 0
    for gltf in gltf_files:
        path_to_mesh_start[gltf.path] = mesh_idx
        mesh_idx += gltf.mesh_count

    result: Dict[str, int] = {}
    for mr, mp in mesh_ref_to_path.items():
        if mp in path_to_mesh_start:
            result[mr] = path_to_mesh_start[mp]
    return result


def _make_node(
    mesh_idx: int,
    instance: MeshInstance,
    node_index: int,
) -> Dict[str, Any]:
    """Create a glTF node dict from a MeshInstance. Kept in UE3 space.
    
    Scale components use absolute values — negative UE3 scales cause
    winding failures when combined with the reflection root matrix (det=-1)."""
    pos = instance.location
    rot = instance.rotation if instance.rotation else Quat.identity()
    scl = Vec3(abs(instance.scale.x), abs(instance.scale.y), abs(instance.scale.z))

    return {
        "mesh": mesh_idx,
        "name": instance.actor_name or f"instance_{node_index}",
        "translation": list(pos),
        "rotation": [rot.x, rot.y, -rot.z, rot.w],
        "scale": list(scl),
        "extras": {
            "mesh_ref": instance.mesh_ref,
            "actor_class": instance.actor_class,
            "mesh_type": instance.mesh_type.value,
        },
    }


# Matrix converting UE3 (X=fwd,Y=right,Z=up) → glTF (X=right,Y=up,Z=fwd with +Z=screen-up in top-down).
# Maps (x,y,z)_ue → (x,z,y)_gltf. Reflection (det=-1); glTF inverts winding for mirrored nodes.
# Column-major 4x4 for glTF node matrix.
_UE3_TO_GLTF_MATRIX = [1, 0, 0, 0,  0, 0, 1, 0,  0, 1, 0, 0,  0, 0, 0, 1]


def _add_root_node(combined: Dict[str, Any], child_indices: List[int], world_scale: float = 1.0) -> None:
    """Add a root node with the UE3→glTF conversion matrix, parenting all instance nodes."""
    s = world_scale
    matrix = [s, 0, 0, 0,  0, 0, s, 0,  0, s, 0, 0,  0, 0, 0, 1]
    root_node: Dict[str, Any] = {
        "name": "ue3_to_gltf_root",
        "matrix": matrix,
        "children": child_indices,
    }
    combined["nodes"].append(root_node)
    combined["scenes"][0]["nodes"] = [len(combined["nodes"]) - 1]


# ═══════════════════════════════════════════════════════════════════════════
# Main orchestration
# ═══════════════════════════════════════════════════════════════════════════

def assemble_map(
    actors_json_paths: List[str],
    static_meshes_dir: str,
    skeletal_meshes_dir: Optional[str],
    output_path: str,
    world_scale: float = 1.0,
) -> int:
    print(f"Assembling map from {len(actors_json_paths)} actor file(s):")
    for p in actors_json_paths:
        print(f"  {p}")

    # 1. Build mesh index
    print("Building mesh index...")
    mesh_dirs = [static_meshes_dir]
    if skeletal_meshes_dir and os.path.isdir(skeletal_meshes_dir):
        mesh_dirs.append(skeletal_meshes_dir)
    mesh_index = build_mesh_index(mesh_dirs)
    total_files = sum(len(v) for v in mesh_index.values())
    print(f"  Indexed {len(mesh_index)} unique mesh references from {total_files} files")

    # 2. Extract mesh instances from all actor JSONs
    print("Extracting mesh instances...")
    instances: List[MeshInstance] = []
    for path in actors_json_paths:
        instances.extend(extract_mesh_instances(path))
    print(f"  Found {len(instances)} mesh instances")

    # 3. Resolve mesh references
    print("Resolving mesh references...")
    mesh_ref_to_path: Dict[str, str] = {}
    unresolved: set = set()
    for inst in instances:
        mr = inst.mesh_ref
        if mr not in mesh_ref_to_path:
            path = resolve_mesh_ref(mr, inst.mesh_type, mesh_index)
            if path:
                mesh_ref_to_path[mr] = path
            else:
                unresolved.add(mr)

    if unresolved:
        print(f"  WARNING: {len(unresolved)} unresolved mesh refs:")
        for mr in sorted(unresolved)[:20]:
            print(f"    {mr}")
        if len(unresolved) > 20:
            print(f"    ... and {len(unresolved) - 20} more")

    print(f"  Resolved {len(mesh_ref_to_path)} unique meshes from {len(instances)} instances")

    if not mesh_ref_to_path:
        if not instances:
            print("SKIP: No mesh instances in actor data (non-geometry map).")
        else:
            print(f"WARNING: {len(instances)} instances but 0 resolved meshes.")
        return 0

    # 4. Load all unique mesh glTFs
    print(f"Loading {len(mesh_ref_to_path)} unique mesh glTFs...")
    # Preserve insertion order for predictable mesh indexing
    unique_paths = list(dict.fromkeys(mesh_ref_to_path.values()))
    gltf_files = [gf for p in unique_paths if (gf := _load_gltf(p)) is not None]

    if not gltf_files:
        print("ERROR: Failed to load any mesh glTFs.")
        return 1

    # 5. Merge glTFs
    print(f"Merging {len(gltf_files)} mesh glTFs...")
    textures_base = _resolve_textures_base(static_meshes_dir)
    combined, combined_buffer, gltf_out_path, bin_out_path = merge_gltf_assets(
        gltf_files, output_path, textures_base,
    )

    # 6. Build mesh_ref → combined mesh index
    mesh_ref_to_idx = _build_mesh_ref_index(gltf_files, mesh_ref_to_path)

    # 7. Create instance nodes (UE3 space, root node converts to glTF)
    node_idx = 0
    instance_indices: List[int] = []
    for inst in instances:
        mr = inst.mesh_ref
        if mr not in mesh_ref_to_idx:
            continue
        combined["nodes"].append(_make_node(mesh_ref_to_idx[mr], inst, node_idx))
        instance_indices.append(node_idx)
        node_idx += 1

    # 8. Add UE3→glTF conversion root node (parens all instances)
    _add_root_node(combined, instance_indices, world_scale)

    # 9. Write output
    with open(gltf_out_path, "w") as f:
        json.dump(combined, f, indent=2)
    with open(bin_out_path, "wb") as f:
        f.write(combined_buffer)

    print(f"Wrote {gltf_out_path} ({node_idx} nodes)")
    print(f"Wrote {bin_out_path} ({len(combined_buffer)} bytes)")
    print(f"  Meshes: {len(combined['meshes'])}")
    print(f"  Materials: {len(combined['materials'])}")
    print(f"  Textures: {len(combined['textures'])}")
    print(f"  Images: {len(combined['images'])}")

    return 0


def main() -> None:
    parser = argparse.ArgumentParser(description="Assemble combined map glTFs from actor JSONs")
    parser.add_argument("actors_json", nargs='?', help="Path to a single .actors.json file")
    parser.add_argument("--all", action="store_true", help="Assemble all maps in the actors directory")
    parser.add_argument("--actors-dir", default="output/gltf/actors", help="Directory containing .actors.json files")
    parser.add_argument("--output", "-o", default="output/gltf/maps", help="Output directory for combined map glTFs")
    parser.add_argument("--static-meshes", default="output/gltf/static-meshes", help="Static meshes directory")
    parser.add_argument("--skeletal-meshes", default="output/gltf/skeletal-meshes", help="Skeletal meshes directory")
    parser.add_argument("--world-scale", type=float, default=1.0, help="Scale factor applied to entire world (0.01 for cm->m)")
    args = parser.parse_args()

    if args.all:
        maps = discover_maps(args.actors_dir)
        if not maps:
            print(f"No .actors.json files found in {args.actors_dir}")
            sys.exit(1)
        print(f"Found {len(maps)} maps to assemble")
        failed = 0
        for map_name, json_paths in sorted(maps.items()):
            print(f"\n{'='*60}")
            print(f"Map: {map_name} ({len(json_paths)} files)")
            output_path = os.path.join(args.output, map_name, map_name + ".gltf")
            rc = assemble_map(json_paths, args.static_meshes, args.skeletal_meshes,
                              output_path, args.world_scale)
            if rc != 0:
                failed += 1
        print(f"\nDone. {len(maps)} maps processed, {failed} failed.")
        sys.exit(0 if failed == 0 else 1)

    if not args.actors_json:
        parser.error("Either specify an actors_json file or use --all")

    map_name = os.path.splitext(os.path.basename(args.actors_json))[0]
    if map_name.endswith(".actors"):
        map_name = map_name[:-7]

    output_path = os.path.join(args.output, map_name, map_name + ".gltf")
    sys.exit(assemble_map([args.actors_json], args.static_meshes, args.skeletal_meshes,
                           output_path, args.world_scale))


if __name__ == "__main__":
    main()
