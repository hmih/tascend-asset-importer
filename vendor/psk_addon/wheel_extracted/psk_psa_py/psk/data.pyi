from ctypes import Structure
from enum import Enum
from ..shared.data import Color, Vector2, Vector3, Quaternion, PsxBone


class PskSectionName(bytes, Enum):
    ACTRHEAD = ...
    PNTS0000 = ...
    VTXW0000 = ...
    FACE0000 = ...
    MATT0000 = ...
    REFSKELT = ...
    RAWWEIGHTS = ...
    FACE3200 = ...
    VERTEXCOLOR = ...
    VTXNORMS = ...
    MRPHINFO = ...
    MRPHDATA = ...


class Psk:
    class Wedge:
        point_index: int
        u: float
        v: float
        material_index: int
        
        def __init__(self, point_index: int, u: float, v: float, material_index: int = 0) -> None: ...
        def __hash__(self) -> int: ...
        def __eq__(self, other: object) -> bool: ...

    class _Wedge16(Structure):
        point_index: int
        u: float
        v: float
        material_index: int
        
        def to_wedge(self) -> Psk.Wedge: ...

    class _Wedge32(Structure):
        point_index: int
        u: float
        v: float
        material_index: int
        
        def to_wedge(self) -> Psk.Wedge: ...

    class Face:
        wedge_indices: tuple[int, int, int]
        material_index: int
        aux_material_index: int
        smoothing_groups: int
        
        def __init__(self, wedge_indices: tuple[int, int, int], material_index: int = 0,
                     aux_material_index: int = 0, smoothing_groups: int = 0) -> None: ...

    class _Face16(Structure):
        wedge_indices: tuple[int, int, int]
        material_index: int
        aux_material_index: int
        smoothing_groups: int
        
        def to_face(self) -> Psk.Face: ...

    class _Face32(Structure):
        wedge_indices: tuple[int, int, int]
        material_index: int
        aux_material_index: int
        smoothing_groups: int
        
        def to_face(self) -> Psk.Face: ...

    class Material(Structure):
        name: bytes
        texture_index: int
        poly_flags: int
        aux_material: int
        aux_flags: int
        lod_bias: int
        lod_style: int

    class Bone(Structure):
        name: bytes
        flags: int
        children_count: int
        parent_index: int
        rotation: Quaternion
        location: Vector3
        length: float
        size: Vector3

    class Weight(Structure):
        weight: float
        point_index: int
        bone_index: int

    class MorphInfo(Structure):
        name: bytes
        vertex_count: int

    class MorphData(Structure):
        position_delta: Vector3
        tangent_z_delta: Vector3
        point_index: int

    @property
    def has_extra_uvs(self) -> bool: ...

    @property
    def has_vertex_colors(self) -> bool: ...

    @property
    def has_vertex_normals(self) -> bool: ...

    @property
    def has_material_references(self) -> bool: ...

    @property
    def has_morph_data(self) -> bool: ...
    
    def sort_and_normalize_weights(self): ...

    points: list[Vector3]
    wedges: list[Psk.Wedge]
    faces: list[Psk.Face]
    materials: list[Psk.Material]
    weights: list[Psk.Weight]
    bones: list[PsxBone]
    extra_uvs: list[list[Vector2]]
    vertex_colors: list[Color]
    vertex_normals: list[Vector3]
    morph_infos: list[Psk.MorphInfo]
    morph_data: list[Psk.MorphData]
    material_references: list[str]
