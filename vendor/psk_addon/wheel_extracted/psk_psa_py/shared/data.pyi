from ctypes import Structure
from typing import Tuple


class StructureEq(Structure):
    def __eq__(self, other: object) -> bool: ...
    def __ne__(self, other: object) -> bool: ...


class Color(StructureEq):
    r: int
    g: int
    b: int
    a: int

    def normalized(self) -> Tuple: ...
    def __iter__(self): ...
    def __eq__(self, other: object) -> bool: ...
    def __repr__(self) -> str: ...


class Vector2(StructureEq):
    x: float
    y: float

    def __iter__(self): ...
    def __repr__(self) -> str: ...


class Vector3(StructureEq):
    x: float
    y: float
    z: float

    @classmethod
    def zero(cls) -> Vector3: ...
    def __iter__(self): ...
    def __repr__(self) -> str: ...


class Quaternion(StructureEq):
    x: float
    y: float
    z: float
    w: float

    @classmethod
    def identity(cls) -> Quaternion: ...
    def __iter__(self): ...
    def __repr__(self) -> str: ...


class PsxBone(StructureEq):
    name: bytes
    flags: int
    children_count: int
    parent_index: int
    rotation: Quaternion
    location: Vector3
    length: float
    size: Vector3


class Section(StructureEq):
    name: bytes
    type_flags: int
    data_size: int
    data_count: int
