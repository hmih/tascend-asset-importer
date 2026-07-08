"""
Batch convert PSK (skeletal meshes) and PSA (animations) to glTF using Blender headless.

Usage:
  blender --background --python src/convert_skeletal.py -- <assets_dir> <output_dir> [--psa <matching_json>] [--single <file>]

- assets_dir: read-only mounted directory containing .psk and .psa files
- output_dir: writable directory for .glb output (e.g. /var/tmp/skeletal)
- --psa <json>: also import matching PSA animations using pre-computed matching JSON
- --single <file>: convert only one PSK file (for testing)
"""
import bpy
import os
import sys
import json
import time
import traceback

# Add wheel path before importing the addon
_wheel_dir = os.environ.get('PSK_PSA_PY_PATH', os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'vendor', 'psk_addon', 'wheel_extracted'))
if _wheel_dir not in sys.path:
    sys.path.insert(0, _wheel_dir)

import addon_utils

addon_utils.enable("io_scene_psk_psa", default_set=True)

from io_scene_psk_psa.psk.importer import import_psk, PskImportOptions
from io_scene_psk_psa.psa.importer import import_psa, PsaImportOptions
from psk_psa_py.psk.reader import read_psk_from_file
from psk_psa_py.psa.reader import PsaReader


def clean_scene():
    """Remove all objects, meshes, armatures, actions, materials from the scene."""
    bpy.ops.object.select_all(action='SELECT')
    bpy.ops.object.delete(use_global=False)
    for collection in list(bpy.data.collections):
        bpy.data.collections.remove(collection)
    for mesh in list(bpy.data.meshes):
        bpy.data.meshes.remove(mesh)
    for armature in list(bpy.data.armatures):
        bpy.data.armatures.remove(armature)
    for action in list(bpy.data.actions):
        bpy.data.actions.remove(action)
    for material in list(bpy.data.materials):
        bpy.data.materials.remove(material)
    for image in list(bpy.data.images):
        bpy.data.images.remove(image)
    for node in list(bpy.data.node_groups):
        bpy.data.node_groups.remove(node)


def get_armature_object():
    """Find the armature object in the scene."""
    for obj in bpy.context.scene.objects:
        if obj.type == 'ARMATURE':
            return obj
    return None


def import_psk_file(psk_path):
    """Import a PSK file and return the import result."""
    name = os.path.splitext(os.path.basename(psk_path))[0]
    psk = read_psk_from_file(psk_path)

    options = PskImportOptions()
    options.should_import_mesh = True
    options.should_import_extra_uvs = True
    options.should_import_vertex_colors = True
    options.should_import_vertex_normals = True
    options.should_import_armature = True
    options.should_import_shape_keys = True
    options.bone_length = 1.0

    return import_psk(psk, bpy.context, name, options)


def import_psa_file(psa_path, armature_object, sequence_names=None):
    """Import a PSA file onto an armature."""
    reader = PsaReader.from_path(psa_path)

    if sequence_names is None:
        sequence_names = list(reader.sequences.keys())

    options = PsaImportOptions()
    options.sequence_names = sequence_names
    options.should_use_fake_user = True
    options.should_stash = True
    options.should_overwrite = True

    result = import_psa(bpy.context, reader, armature_object, options)
    return result


def export_gltf(output_path, export_animations=True):
    """Export the current scene to glTF (GLB format)."""
    export_props = {
        'export_format': 'GLB',
        'use_selection': False,
        'export_apply': True,
        'export_yup': True,
        'export_materials': 'EXPORT',
        'export_texcoords': True,
        'export_normals': True,
        'export_vertex_color': 'MATERIAL',
        'export_cameras': False,
        'export_lights': False,
        'export_extras': False,
        'export_draco_mesh_compression_enable': False,
    }
    if export_animations:
        export_props['export_animations'] = True
        export_props['export_animation_mode'] = 'ACTIONS'
    else:
        export_props['export_animations'] = False

    bpy.ops.export_scene.gltf(filepath=output_path, **export_props)


def main():
    args = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    if len(args) < 2:
        print("Usage: convert_skeletal.py <assets_dir> <output_dir> [--psa <json>] [--single <file>]")
        sys.exit(1)

    assets_dir = args[0]
    output_dir = args[1]

    # Parse optional args
    matching_data = None
    single_file = None
    list_file = None

    i = 2
    while i < len(args):
        if args[i] == "--psa" and i + 1 < len(args):
            matching_json_path = args[i + 1]
            with open(matching_json_path, 'r') as f:
                matching_data = json.load(f)
            print(f"Loaded PSA matching from: {matching_json_path}")
            print(f"  PSKs with matches: {matching_data['stats']['psks_with_matches']}")
            print(f"  Total PSA matches: {matching_data['stats']['total_matches']}")
            print(f"  Total sequences:   {matching_data['stats']['total_sequences']}")
            i += 2
        elif args[i] == "--single" and i + 1 < len(args):
            single_file = args[i + 1]
            i += 2
        elif args[i] == "--list" and i + 1 < len(args):
            list_file = args[i + 1]
            i += 2
        else:
            i += 1

    has_psa = matching_data is not None
    os.makedirs(output_dir, exist_ok=True)

    # Find all PSK files
    psk_files = []
    if list_file:
        with open(list_file, 'r') as f:
            psk_files = [l.strip() for l in f if l.strip()]
    elif single_file:
        psk_files = [single_file]
    else:
        for root, dirs, files in os.walk(assets_dir):
            for f in files:
                if f.endswith('.psk'):
                    psk_files.append(os.path.join(root, f))
    psk_files.sort()

    print(f"Found {len(psk_files)} PSK files to convert")

    # Stats
    stats = {
        'total': len(psk_files),
        'success': 0,
        'failed': 0,
        'with_animations': 0,
        'total_animations': 0,
        'errors': [],
        'warnings': [],
    }

    start_time = time.time()

    for i, psk_path in enumerate(psk_files):
        psk_name = os.path.splitext(os.path.basename(psk_path))[0]
        rel_path = os.path.relpath(psk_path, assets_dir)
        output_path = os.path.join(output_dir, os.path.splitext(rel_path)[0] + '.glb')
        os.makedirs(os.path.dirname(output_path), exist_ok=True)

        print(f"\n[{i+1}/{len(psk_files)}] Converting: {rel_path}")

        clean_scene()

        try:
            # Import PSK
            result = import_psk_file(psk_path)
            if result.warnings:
                for w in result.warnings:
                    print(f"  WARNING: {w}")
                    stats['warnings'].append(f"{rel_path}: {w}")

            armature = get_armature_object()
            if not armature:
                print(f"  ERROR: No armature found after import")
                stats['failed'] += 1
                stats['errors'].append(f"{rel_path}: No armature found")
                continue

            bone_count = len(armature.data.bones)
            mesh_objs = [o for o in bpy.context.scene.objects if o.type == 'MESH']
            print(f"  Armature: {armature.name} ({bone_count} bones, {len(mesh_objs)} mesh(es))")

            # Import matching PSA files from pre-computed matching
            total_anim_count = 0
            if has_psa and rel_path in matching_data['matching']:
                matches = matching_data['matching'][rel_path]
                print(f"  Importing {len(matches)} PSA files...")
                for match in matches:
                    psa_rel = match['psa_path']
                    psa_path = os.path.join(assets_dir, psa_rel)
                    seq_names = match.get('sequences', [])
                    seq_count = match.get('sequence_count', len(seq_names))

                    try:
                        bpy.ops.object.select_all(action='DESELECT')
                        armature.select_set(True)
                        bpy.context.view_layer.objects.active = armature

                        psa_result = import_psa_file(psa_path, armature, seq_names)
                        total_anim_count += seq_count
                        if psa_result.warnings:
                            for w in psa_result.warnings[:2]:
                                print(f"    WARNING ({psa_rel}): {w}")
                    except Exception as e:
                        print(f"    ERROR ({psa_rel}): {e}")
                        stats['errors'].append(f"{rel_path} <- {psa_rel}: {e}")

                if total_anim_count > 0:
                    stats['with_animations'] += 1
                    stats['total_animations'] += total_anim_count
                    print(f"  Total animations imported: {total_anim_count}")

            # Export to glTF
            export_gltf(output_path, export_animations=has_psa and total_anim_count > 0)
            file_size = os.path.getsize(output_path)
            print(f"  Exported: {file_size:,} bytes")
            stats['success'] += 1

        except Exception as e:
            print(f"  ERROR: {e}")
            traceback.print_exc()
            stats['failed'] += 1
            stats['errors'].append(f"{rel_path}: {e}")

    elapsed = time.time() - start_time

    # Write stats
    stats_path = os.path.join(output_dir, '.conversion_stats.json')
    with open(stats_path, 'w') as f:
        json.dump(stats, f, indent=2)

    print(f"\n{'='*60}")
    print(f"Conversion complete in {elapsed:.1f}s:")
    print(f"  Total:            {stats['total']}")
    print(f"  Success:          {stats['success']}")
    print(f"  Failed:           {stats['failed']}")
    print(f"  With animations:  {stats['with_animations']}")
    print(f"  Total animations: {stats['total_animations']}")
    if stats['errors']:
        print(f"  Errors ({len(stats['errors'])}):")
        for err in stats['errors'][:10]:
            print(f"    - {err}")
        if len(stats['errors']) > 10:
            print(f"    ... and {len(stats['errors']) - 10} more")
    print(f"Stats written to: {stats_path}")


if __name__ == '__main__':
    main()
