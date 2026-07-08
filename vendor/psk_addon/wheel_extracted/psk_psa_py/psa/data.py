from collections import OrderedDict
from typing import OrderedDict as OrderedDictType
from enum import Enum

from ctypes import Structure, c_char, c_int32, c_float
from ..shared.data import PsxBone, Quaternion, Vector3


class PsaSectionName(bytes, Enum):
    ANIMHEAD = b'ANIMHEAD'
    BONENAMES = b'BONENAMES'
    ANIMINFO = b'ANIMINFO'
    ANIMKEYS = b'ANIMKEYS'
    SCALEKEYS = b'SCALEKEYS'


class Psa:
    """
    Note that keys are not stored within the Psa object.
    Use the `PsaReader.get_sequence_keys` to get the keys for a sequence.
    """

    class Sequence(Structure):
        _fields_ = [
            ('name', c_char * 64),
            ('group', c_char * 64),
            ('bone_count', c_int32),
            ('root_include', c_int32),
            ('compression_style', c_int32),
            ('key_quotum', c_int32),
            ('key_reduction', c_float),
            ('track_time', c_float),
            ('fps', c_float),
            ('start_bone', c_int32),
            ('frame_start_index', c_int32),
            ('frame_count', c_int32)
        ]

    class Key(Structure):
        _fields_ = [
            ('location', Vector3),
            ('rotation', Quaternion),
            ('time', c_float)
        ]

        @property
        def data(self):
            yield self.rotation.w
            yield self.rotation.x
            yield self.rotation.y
            yield self.rotation.z
            yield self.location.x
            yield self.location.y
            yield self.location.z

        def __repr__(self) -> str:
            return repr((self.location, self.rotation, self.time))
    
    class ScaleKey(Structure):
        _fields_ = [
            ('scale', Vector3),
            ('time', c_float),
        ]

        @property
        def data(self):
            yield self.scale.x
            yield self.scale.y
            yield self.scale.z

    def __init__(self):
        self.bones: list[PsxBone] = []
        self.sequences: OrderedDictType[str, Psa.Sequence] = OrderedDict()
        self.keys: list[Psa.Key] = []
        self.scale_keys: list[Psa.ScaleKey] = []

    def get_sequence_key_range(self, sequence_name: str) -> tuple[int, int]:
        sequence = self.sequences[sequence_name]
        frame_index = sequence.frame_start_index * len(self.bones)
        start = frame_index
        end = frame_index + len(self.bones) * sequence.frame_count
        return start, end
    
    def get_sequence_keys(self, sequence_name: str) -> list[Key]:
        start, end = self.get_sequence_key_range(sequence_name)
        return self.keys[start:end]

    def get_sequence_scale_keys(self, sequence_name: str) -> list[ScaleKey]:
        if len(self.scale_keys) == 0:
            return []
        start, end = self.get_sequence_key_range(sequence_name)
        return self.scale_keys[start:end]

__all__ = [
    'Psa',
    'PsaSectionName'
]


def __dir__():
    return __all__
