"""
Standalone script to match PSK files with PSA files by bone name overlap.
Pure Python - no Blender, no addon needed. Parses PSK/PSA binary format directly.

Usage:
  python3 src/match_psa.py <assets_dir> <output_json>

Output: JSON file mapping PSK relative paths to lists of matching PSA info.
"""
import sys
import os
import json
import struct
import time


def read_section(fp):
    """Read a PSK/PSA section header (32 bytes: name + type_flags + data_size + data_count)."""
    data = fp.read(32)
    if len(data) < 32:
        return None
    name = data[:20].rstrip(b'\x00')
    type_flags, data_size, data_count = struct.unpack('<III', data[20:32])
    return name, type_flags, data_size, data_count


def read_psk_bone_names(psk_path):
    """Read bone names from a PSK file."""
    bones = set()
    try:
        with open(psk_path, 'rb') as fp:
            while True:
                section = read_section(fp)
                if section is None:
                    break
                name, type_flags, data_size, data_count = section
                if name == b'REFSKELT':
                    # Each bone starts with a 64-byte name field
                    for i in range(data_count):
                        bone_data = fp.read(data_size)
                        if len(bone_data) >= 64:
                            bone_name = bone_data[:64].rstrip(b'\x00').decode('windows-1252', errors='replace')
                            bones.add(bone_name)
                else:
                    # Skip this section
                    fp.seek(data_size * data_count, os.SEEK_CUR)
    except Exception as e:
        print(f"  ERROR reading PSK {psk_path}: {e}", file=sys.stderr)
    return bones


def read_psa_bone_names_and_sequences(psa_path):
    """Read bone names and sequence names from a PSA file."""
    bones = set()
    sequences = []
    try:
        with open(psa_path, 'rb') as fp:
            while True:
                section = read_section(fp)
                if section is None:
                    break
                name, type_flags, data_size, data_count = section
                if name == b'BONENAMES':
                    for i in range(data_count):
                        bone_data = fp.read(data_size)
                        if len(bone_data) >= 64:
                            bone_name = bone_data[:64].rstrip(b'\x00').decode('windows-1252', errors='replace')
                            bones.add(bone_name)
                elif name == b'ANIMINFO':
                    for i in range(data_count):
                        seq_data = fp.read(data_size)
                        if len(seq_data) >= 64:
                            seq_name = seq_data[:64].rstrip(b'\x00').decode('windows-1252', errors='replace')
                            sequences.append(seq_name)
                else:
                    fp.seek(data_size * data_count, os.SEEK_CUR)
    except Exception as e:
        print(f"  ERROR reading PSA {psa_path}: {e}", file=sys.stderr)
    return bones, sequences


def main():
    args = sys.argv[1:]
    if len(args) < 2:
        print("Usage: match_psa.py <assets_dir> <output_json>")
        sys.exit(1)

    assets_dir = args[0]
    output_json = args[1]

    # Find all PSK and PSA files
    psk_files = []
    psa_files = []
    for root, dirs, files in os.walk(assets_dir):
        for f in files:
            if f.endswith('.psk'):
                psk_files.append(os.path.join(root, f))
            elif f.endswith('.psa'):
                psa_files.append(os.path.join(root, f))
    psk_files.sort()
    psa_files.sort()

    print(f"Found {len(psk_files)} PSK files, {len(psa_files)} PSA files")

    # Read bone names from all PSA files
    print("Reading bone names and sequences from PSA files...")
    start = time.time()
    psa_info = {}  # path -> (bone_names, sequence_names)
    for i, psa_path in enumerate(psa_files):
        bones, seqs = read_psa_bone_names_and_sequences(psa_path)
        psa_info[psa_path] = (bones, seqs)
        if (i + 1) % 50 == 0 or i == len(psa_files) - 1:
            print(f"  [{i+1}/{len(psa_files)}] {time.time()-start:.1f}s")
    print(f"PSA info cached in {time.time()-start:.1f}s")

    # Read bone names from all PSK files and match
    print("Reading PSK bone names and matching...")
    matching = {}
    stats = {'total_psks': len(psk_files), 'psks_with_matches': 0, 'total_matches': 0,
             'total_sequences': 0}

    for i, psk_path in enumerate(psk_files):
        psk_bones = read_psk_bone_names(psk_path)
        if not psk_bones:
            continue

        rel_psk = os.path.relpath(psk_path, assets_dir)
        matches = []

        for psa_path, (psa_bones, psa_seqs) in psa_info.items():
            if not psa_bones:
                continue
            intersection = psk_bones & psa_bones
            if not intersection:
                continue
            match_ratio = len(intersection) / len(psa_bones)
            if match_ratio >= 1.0:
                rel_psa = os.path.relpath(psa_path, assets_dir)
                matches.append({
                    'psa_path': rel_psa,
                    'match_ratio': round(match_ratio, 3),
                    'bone_count': len(intersection),
                    'psa_bone_count': len(psa_bones),
                    'sequence_count': len(psa_seqs),
                    'sequences': psa_seqs,
                })

        matches.sort(key=lambda x: x['match_ratio'], reverse=True)
        if matches:
            matching[rel_psk] = matches
            stats['psks_with_matches'] += 1
            stats['total_matches'] += len(matches)
            total_seqs = sum(m['sequence_count'] for m in matches)
            stats['total_sequences'] += total_seqs
            print(f"  [{i+1}/{len(psk_files)}] {rel_psk}: {len(matches)} PSA(s), {total_seqs} sequences")

    # Save to JSON
    output = {
        'stats': stats,
        'matching': matching,
    }
    with open(output_json, 'w') as f:
        json.dump(output, f, indent=2)

    print(f"\n{'='*60}")
    print(f"PSK files:          {stats['total_psks']}")
    print(f"PSKs with matches:   {stats['psks_with_matches']}")
    print(f"Total PSA matches:   {stats['total_matches']}")
    print(f"Total sequences:     {stats['total_sequences']}")
    print(f"Output: {output_json}")


if __name__ == '__main__':
    main()
