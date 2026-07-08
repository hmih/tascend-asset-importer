from typing import OrderedDict as OrderedDictType, Generator

from ctypes import Structure
from enum import Enum
from ..shared.data import PsxBone, Quaternion, Vector3


class PsaSectionName(bytes, Enum):
    ANIMHEAD = ...
    BONENAMES = ...
    ANIMINFO = ...
    ANIMKEYS = ...
    SCALEKEYS = ...


class Psa:
    """
    Note that keys are not stored within the Psa object.
    Use the `PsaReader.get_sequence_keys` to get the keys for a sequence.
    """

    class Sequence(Structure):
        name: bytes
        group: bytes
        bone_count: int
        root_include: int
        compression_style: int
        key_quotum: int
        key_reduction: float
        track_time: float
        fps: float
        start_bone: int
        frame_start_index: int
        frame_count: int

    class Key(Structure):
        location: Vector3
        rotation: Quaternion
        time: float

        @property
        def data(self) -> Generator[float]: ...
    
    class ScaleKey(Structure):
        scale: Vector3
        time: float

        @property
        def data(self) -> Generator[float]: ...
        
    bones: list[PsxBone]
    sequences: OrderedDictType[str, Psa.Sequence]
    keys: list[Psa.Key]
    scale_keys: list[Psa.ScaleKey]

    def get_sequence_keys(self, sequence_name: str) -> list[Psa.Key]: ...
    def get_sequence_scale_keys(self, sequence_name: str) -> list[Psa.ScaleKey]: ...


__all__ = [
    'Psa',
    'PsaSectionName'
]


def __dir__():
    return __all__
