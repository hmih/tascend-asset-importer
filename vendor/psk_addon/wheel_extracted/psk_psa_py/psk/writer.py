import os
from ctypes import Structure, sizeof, c_uint16
from typing import BinaryIO, Type

from .data import Psk, PskSectionName
from ..shared.data import Color, PsxBone, Section, Vector2, Vector3

MAX_WEDGE_COUNT = 65536
MAX_POINT_COUNT = 4294967296
MAX_BONE_COUNT = 2147483647
MAX_MATERIAL_COUNT = 256


def _write_section(fp: BinaryIO, name: bytes, data_type: Type[Structure] | None = None, data: list | None = None):
    section = Section()
    section.name = name
    if data_type is not None and data is not None:
        section.data_size = sizeof(data_type)
        section.data_count = len(data)
    fp.write(section)
    if data is not None:
        for datum in data:
            fp.write(datum)


def write_psk(psk: Psk, fp: BinaryIO, is_extended_format: bool = False):
    if len(psk.wedges) > MAX_WEDGE_COUNT:
        raise RuntimeError(f'Number of wedges ({len(psk.wedges)}) exceeds limit of {MAX_WEDGE_COUNT}')
    if len(psk.points) > MAX_POINT_COUNT:
        raise RuntimeError(f'Numbers of vertices ({len(psk.points)}) exceeds limit of {MAX_POINT_COUNT}')
    if len(psk.materials) > MAX_MATERIAL_COUNT:
        raise RuntimeError(f'Number of materials ({len(psk.materials)}) exceeds limit of {MAX_MATERIAL_COUNT}')
    if len(psk.bones) > MAX_BONE_COUNT:
        raise RuntimeError(f'Number of bones ({len(psk.bones)}) exceeds limit of {MAX_BONE_COUNT}')
    if len(psk.bones) == 0:
        raise RuntimeError(f'At least one bone must be marked for export')

    _write_section(fp, PskSectionName.ACTRHEAD)
    _write_section(fp, PskSectionName.PNTS0000, Vector3, psk.points)

    wedges = [Psk._Wedge16(
        point_index=w.point_index,
        u=w.u,
        v=w.v,
        material_index=w.material_index,
        reserved=0,
        padding2=0
    ) for w in psk.wedges]

    _write_section(fp, PskSectionName.VTXW0000, Psk._Wedge16, wedges)
    
    faces = [Psk._Face16(
        wedge_indices=(c_uint16 * 3)(*f.wedge_indices),
        material_index=f.material_index,
        aux_material_index=f.aux_material_index,
        smoothing_groups=f.smoothing_groups
    ) for f in psk.faces]
    
    _write_section(fp, PskSectionName.FACE0000, Psk._Face16, faces)
    _write_section(fp, PskSectionName.MATT0000, Psk.Material, psk.materials)
    _write_section(fp, PskSectionName.REFSKELT, PsxBone, psk.bones)
    _write_section(fp, PskSectionName.RAWWEIGHTS, Psk.Weight, psk.weights)

    if is_extended_format:
        for i, extra_uvs in enumerate(psk.extra_uvs):
            _write_section(fp, f'EXTRAUV{i}'.encode('windows-1252'), Vector2, extra_uvs)
        _write_section(fp, PskSectionName.VTXNORMS, Vector3, psk.vertex_normals)
        _write_section(fp, PskSectionName.VERTEXCOLOR, Color, psk.vertex_colors)
        _write_section(fp, PskSectionName.MRPHINFO, Psk.MorphInfo, psk.morph_infos)
        _write_section(fp, PskSectionName.MRPHDATA, Psk.MorphData, psk.morph_data)


def write_psk_to_path(psk: Psk, path: str, is_extended_format: bool = False):
    # Make the directory for the file if it doesn't exist.
    os.makedirs(os.path.dirname(path), exist_ok=True)

    try:
        with open(path, 'wb') as fp:
            write_psk(psk, fp, is_extended_format)
    except PermissionError as e:
        raise RuntimeError(f'The current user "{os.getlogin()}" does not have permission to write to "{path}"') from e


__all__ = [
    'write_psk',
    'write_psk_to_path'
]


def __dir__():
    return __all__
