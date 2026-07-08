from ctypes import c_uint32, c_float, c_int32, c_uint8, c_int8, c_int16, c_char, c_uint16
from enum import Enum

from ..shared.data import Vector3, Quaternion, Color, Vector2, PsxBone, StructureEq


class PskSectionName(bytes, Enum):
    ACTRHEAD = b'ACTRHEAD'
    PNTS0000 = b'PNTS0000'
    VTXW0000 = b'VTXW0000'
    FACE0000 = b'FACE0000'
    MATT0000 = b'MATT0000'
    REFSKELT = b'REFSKELT'
    RAWWEIGHTS = b'RAWWEIGHTS'
    FACE3200 = b'FACE3200'
    VERTEXCOLOR = b'VERTEXCOLOR'
    VTXNORMS = b'VTXNORMS'
    MRPHINFO = b'MRPHINFO'
    MRPHDATA = b'MRPHDATA'


class Psk(object):
    class Wedge(object):
        def __init__(self, point_index: int, u: float, v: float, material_index: int = 0):
            self.point_index: int = point_index
            self.u: float = u
            self.v: float = v
            self.material_index: int = material_index

        def __hash__(self):
            return hash(f'{self.point_index}-{self.u}-{self.v}-{self.material_index}')

        def __eq__(self, other):
            return (self.point_index == other.point_index and 
                    self.u == other.u and 
                    self.v == other.v and 
                    self.material_index == other.material_index)

    class _Wedge16(StructureEq):
        _fields_ = [
            ('point_index', c_uint32),
            ('u', c_float),
            ('v', c_float),
            ('material_index', c_uint8),
            ('reserved', c_int8),
            ('padding2', c_int16)
        ]

        def to_wedge(self) -> 'Psk.Wedge':
            return Psk.Wedge(self.point_index, self.u, self.v, self.material_index)

    class _Wedge32(StructureEq):
        _fields_ = [
            ('point_index', c_uint32),
            ('u', c_float),
            ('v', c_float),
            ('material_index', c_uint32)
        ]

        def to_wedge(self) -> 'Psk.Wedge':
            return Psk.Wedge(self.point_index, self.u, self.v, self.material_index)

    class Face(object):
        def __init__(self, wedge_indices: tuple[int, int, int], material_index: int = 0, 
                     aux_material_index: int = 0, smoothing_groups: int = 0):
            self.wedge_indices: tuple[int, int, int] = wedge_indices
            self.material_index: int = material_index
            self.aux_material_index: int = aux_material_index
            self.smoothing_groups: int = smoothing_groups

    class _Face16(StructureEq):
        _fields_ = [
            ('wedge_indices', c_uint16 * 3),
            ('material_index', c_uint8),
            ('aux_material_index', c_uint8),
            ('smoothing_groups', c_int32)
        ]

        def to_face(self) -> 'Psk.Face':
            return Psk.Face(
                tuple(self.wedge_indices),
                self.material_index,
                self.aux_material_index,
                self.smoothing_groups
            )

    class _Face32(StructureEq):
        _pack_ = 1
        _layout_ = 'ms'
        _fields_ = [
            ('wedge_indices', c_uint32 * 3),
            ('material_index', c_uint8),
            ('aux_material_index', c_uint8),
            ('smoothing_groups', c_int32)
        ]

        def to_face(self) -> 'Psk.Face':
            return Psk.Face(
                tuple(self.wedge_indices),
                self.material_index,
                self.aux_material_index,
                self.smoothing_groups
            )

    class Material(StructureEq):
        _fields_ = [
            ('name', c_char * 64),
            ('texture_index', c_int32),
            ('poly_flags', c_int32),
            ('aux_material', c_int32),
            ('aux_flags', c_int32),
            ('lod_bias', c_int32),
            ('lod_style', c_int32)
        ]

    class Bone(StructureEq):
        _fields_ = [
            ('name', c_char * 64),
            ('flags', c_int32),
            ('children_count', c_int32),
            ('parent_index', c_int32),
            ('rotation', Quaternion),
            ('location', Vector3),
            ('length', c_float),
            ('size', Vector3)
        ]

    class Weight(StructureEq):
        _fields_ = [
            ('weight', c_float),
            ('point_index', c_int32),
            ('bone_index', c_int32),
        ]

    class MorphInfo(StructureEq):
        _fields_ = [
            ('name', c_char * 64),
            ('vertex_count', c_int32)
        ]

    class MorphData(StructureEq):
        _fields_ = [
            ('position_delta', Vector3),
            ('tangent_z_delta', Vector3),
            ('point_index', c_int32)
        ]

    @property
    def has_extra_uvs(self):
        return len(self.extra_uvs) > 0

    @property
    def has_vertex_colors(self):
        return len(self.vertex_colors) > 0

    @property
    def has_vertex_normals(self):
        return len(self.vertex_normals) > 0

    @property
    def has_material_references(self):
        return len(self.material_references) > 0

    @property
    def has_morph_data(self):
        return len(self.morph_infos) > 0
    
    def sort_and_normalize_weights(self):
        self.weights.sort(key=lambda x: x.point_index)

        weight_index = 0
        weight_total = len(self.weights)

        while weight_index < weight_total:
            point_index = self.weights[weight_index].point_index
            weight_sum = self.weights[weight_index].weight
            point_weight_total = 1

            # Calculate the sum of weights for the current point_index.
            for i in range(weight_index + 1, weight_total):
                if self.weights[i].point_index != point_index:
                    break
                weight_sum += self.weights[i].weight
                point_weight_total += 1

            # Normalize the weights for the current point_index.
            if weight_sum != 0.0:
                for i in range(weight_index, weight_index + point_weight_total):
                    self.weights[i].weight /= weight_sum

            # Move to the next group of weights.
            weight_index += point_weight_total
    
    def __init__(self):
        self.points: list[Vector3] = []
        self.wedges: list[Psk.Wedge] = []
        self.faces: list[Psk.Face] = []
        self.materials: list[Psk.Material] = []
        self.weights: list[Psk.Weight] = []
        self.bones: list[PsxBone] = []
        self.extra_uvs: list[list[Vector2]] = []
        self.vertex_colors: list[Color] = []
        self.vertex_normals: list[Vector3] = []
        self.morph_infos: list[Psk.MorphInfo] = []
        self.morph_data: list[Psk.MorphData] = []
        self.material_references: list[str] = []

__all__ = [
    'Psk',
    'PskSectionName'
]

def __dir__():
    return __all__
