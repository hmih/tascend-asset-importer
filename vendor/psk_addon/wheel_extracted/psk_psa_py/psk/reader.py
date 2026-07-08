import ctypes
import os
import re
import warnings
from pathlib import Path
from typing import BinaryIO
from ..shared.data import Section, Color, PsxBone, Vector2, Vector3
from ..shared.helpers import read_types
from .data import Psk, PskSectionName


def _read_material_references(path: str) -> list[str]:
    property_file_path = Path(path).with_suffix('.props.txt')
    if not property_file_path.is_file():
        # Property file does not exist.
        return []
    # Do a crude regex match to find the Material list entries.
    contents = property_file_path.read_text()
    pattern = r'Material\s*=\s*([^\s^,]+)'
    return re.findall(pattern, contents)


def read_psk_from_file(path: str):
    with open(path, 'rb') as fp:
        psk = read_psk(fp)
    
    """
    UEViewer exports a sidecar file (*.props.txt) with fully-qualified reference paths for each material
    (e.g., Texture'Package.Group.Object').
    """
    psk.material_references = _read_material_references(path)

    return psk


def read_psk(fp: BinaryIO) -> Psk:
    psk = Psk()

    # Read the PSK file sections.
    while fp.read(1):
        fp.seek(-1, 1)
        section = Section.from_buffer_copy(fp.read(ctypes.sizeof(Section)))
        match section.name:
            case PskSectionName.ACTRHEAD:
                pass
            case PskSectionName.PNTS0000:
                read_types(fp, Vector3, section, psk.points)
            case PskSectionName.VTXW0000:
                if section.data_size == ctypes.sizeof(Psk._Wedge16):
                    wedges16: list[Psk._Wedge16] = []
                    read_types(fp, Psk._Wedge16, section, wedges16)
                    psk.wedges.extend(w.to_wedge() for w in wedges16)
                elif section.data_size == ctypes.sizeof(Psk._Wedge32):
                    wedges32: list[Psk._Wedge32] = []
                    read_types(fp, Psk._Wedge32, section, wedges32)
                    psk.wedges.extend(w.to_wedge() for w in wedges32)
                else:
                    raise RuntimeError(f'Unrecognized wedge format with data size {section.data_size}')
            case PskSectionName.FACE0000:
                faces16: list[Psk._Face16] = []
                read_types(fp, Psk._Face16, section, faces16)
                psk.faces.extend(f.to_face() for f in faces16)
            case PskSectionName.MATT0000:
                read_types(fp, Psk.Material, section, psk.materials)
            case PskSectionName.REFSKELT:
                read_types(fp, PsxBone, section, psk.bones)
            case PskSectionName.RAWWEIGHTS:
                read_types(fp, Psk.Weight, section, psk.weights)
            case PskSectionName.FACE3200:
                faces32: list[Psk._Face32] = []
                read_types(fp, Psk._Face32, section, faces32)
                psk.faces.extend(f.to_face() for f in faces32)
            case PskSectionName.VERTEXCOLOR:
                read_types(fp, Color, section, psk.vertex_colors)
            case PskSectionName.VTXNORMS:
                read_types(fp, Vector3, section, psk.vertex_normals)
            case PskSectionName.MRPHINFO:
                read_types(fp, Psk.MorphInfo, section, psk.morph_infos)
            case PskSectionName.MRPHDATA:
                read_types(fp, Psk.MorphData, section, psk.morph_data)
            case _:
                if section.name.startswith(b'EXTRAUV'):
                    extra_uvs: list[Vector2] = []
                    read_types(fp, Vector2, section, extra_uvs)
                    psk.extra_uvs.append(extra_uvs)
                else:
                    # Section is not handled, skip it.
                    fp.seek(section.data_size * section.data_count, os.SEEK_CUR)
                    warnings.warn(f'Unrecognized section {section.name!r} at position {fp.tell():15}')

    """
    Tools like UEViewer and CUE4Parse write the point index as a 32-bit integer, exploiting the fact that due to struct
    alignment, there were 16-bits of padding following the original 16-bit point index in the wedge struct.
    However, this breaks compatibility with PSK files that were created with older tools that treated the
    point index as a 16-bit integer and might have junk data written to the padding bits.
    To work around this, we check if each point is still addressable using a 16-bit index, and if it is, assume the
    point index is a 16-bit integer and truncate the high bits.
    """
    if len(psk.points) <= 65536:
        for wedge in psk.wedges:
            wedge.point_index &= 0xFFFF

    return psk


__all__ = [
    'read_psk'
]


def __dir__():
    return __all__
