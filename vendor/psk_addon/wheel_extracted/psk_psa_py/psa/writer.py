from ctypes import Structure, sizeof
from typing import Type, Collection, BinaryIO

from .data import Psa, PsaSectionName
from ..shared.data import PsxBone, Section


def _write_section(fp: BinaryIO, name: bytes, data_type: Type[Structure] | None = None, data: Collection | None = None):
    section = Section()
    section.name = name
    if data_type is not None and data is not None:
        section.data_size = sizeof(data_type)
        section.data_count = len(data)
    fp.write(section)
    if data is not None:
        for datum in data:
            fp.write(datum)


def write_psa(psa: Psa, fp: BinaryIO, write_scale_keys: bool = False):
    _write_section(fp, PsaSectionName.ANIMHEAD)
    _write_section(fp, PsaSectionName.BONENAMES, PsxBone, psa.bones)
    _write_section(fp, PsaSectionName.ANIMINFO, Psa.Sequence, list(psa.sequences.values()))
    _write_section(fp, PsaSectionName.ANIMKEYS, Psa.Key, psa.keys)
    if write_scale_keys and len(psa.scale_keys) > 0:
        _write_section(fp, PsaSectionName.SCALEKEYS, Psa.ScaleKey, psa.scale_keys)


def write_psa_to_file(psa: Psa, path: str, write_scale_keys: bool = False):
    with open(path, 'wb') as fp:
        write_psa(psa, fp, write_scale_keys)


__all__ = [
    'write_psa',
    'write_psa_to_file'
]


def __dir__():
    return __all__
