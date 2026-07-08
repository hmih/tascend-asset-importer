from ctypes import sizeof
from pathlib import Path
from typing import BinaryIO, Sequence

from .data import Psa, PsaSectionName
from ..shared.data import Section, PsxBone
from ..shared.helpers import read_types


def _try_fix_cue4parse_issue_103(sequences: Sequence[Psa.Sequence]) -> bool:
    # Detect if the file was exported from CUE4Parse prior to the fix for issue #103.
    # https://github.com/FabianFG/CUE4Parse/issues/103
    # The issue was that the frame_start_index was not being set correctly, and was always being set to the same value
    # as the frame_count.
    # This fix will eventually be deprecated as it is only necessary for files exported prior to the fix.
    if len(sequences) > 0 and sequences[0].frame_start_index == sequences[0].frame_count:
        # Manually set the frame_start_index for each sequence. This assumes that the sequences are in order with
        # no shared frames between sequences (all exporters that I know of do this, so it's a safe assumption).
        frame_start_index = 0
        for i, sequence in enumerate(sequences):
            sequence.frame_start_index = frame_start_index
            frame_start_index += sequence.frame_count
        return True
    return False


def read_psa(fp: BinaryIO):
    psa = Psa()
    while fp.read(1):
        fp.seek(-1, 1)
        section = Section.from_buffer_copy(fp.read(sizeof(Section)))
        match section.name:
            case PsaSectionName.ANIMHEAD:
                pass
            case PsaSectionName.BONENAMES:
                read_types(fp, PsxBone, section, psa.bones)
            case PsaSectionName.ANIMINFO:
                sequences: list[Psa.Sequence] = []
                read_types(fp, Psa.Sequence, section, sequences)
                # Try to fix CUE4Parse bug, if necessary.
                _try_fix_cue4parse_issue_103(sequences)
                for sequence in sequences:
                    psa.sequences[sequence.name.decode()] = sequence
            case PsaSectionName.ANIMKEYS:
                read_types(fp, Psa.Key, section, psa.keys)
            case PsaSectionName.SCALEKEYS:
                read_types(fp, Psa.ScaleKey, section, psa.scale_keys)
            case _:
                fp.seek(section.data_size * section.data_count, 1)
                print(f'Unrecognized section in PSA: {section.name!r}')
    return psa


class PsaReader:
    """
    This class reads the sequences and bone information immediately upon instantiation and holds onto a file handle.

    The keyframe data is not read into memory upon instantiation due to its potentially very large size.

    To read the key data for a particular sequence, call `read_sequence_keys`.
    """

    def __init__(self, fp: BinaryIO):
        self._keys_data_offset: int = 0
        self._scale_keys_data_offset: int | None = None
        self._fp = fp
        self._psa: Psa = self._read(self._fp)
    
    @staticmethod
    def from_path(path: str | Path):
        return PsaReader(open(path, 'rb'))

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        self._fp.close()

    @property
    def bones(self):
        return self._psa.bones

    @property
    def sequences(self):
        return self._psa.sequences

    @property
    def has_scale_keys(self) -> bool:
        return self._scale_keys_data_offset is not None

    def read_sequence_keys(self, sequence_name: str) -> list[Psa.Key]:
        """
        Reads and returns the key data for a sequence.

        @param sequence_name: The name of the sequence.
        @return: A list of Psa.Keys.
        """
        # Set the file reader to the beginning of the keys data
        sequence = self._psa.sequences[sequence_name]
        data_size = sizeof(Psa.Key)
        bone_count = len(self._psa.bones)
        buffer_length = data_size * bone_count * sequence.frame_count
        sequence_keys_offset = self._keys_data_offset + (sequence.frame_start_index * bone_count * data_size)
        self._fp.seek(sequence_keys_offset, 0)
        buffer = self._fp.read(buffer_length)
        offset = 0
        keys = []
        for _ in range(sequence.frame_count * bone_count):
            key = Psa.Key.from_buffer_copy(buffer, offset)
            keys.append(key)
            offset += data_size
        return keys

    def read_sequence_scale_keys(self, sequence_name: str) -> list[Psa.ScaleKey]:
        """
        Reads and returns the scale key data for a sequence.

        @param sequence_name: The name of the sequence.
        @return: A list of Psa.ScaleKeys.
        """
        if self._scale_keys_data_offset is None:
            # This PSA has no scale keys, return an empty list.
            return []
        sequence = self._psa.sequences[sequence_name]
        data_size = sizeof(Psa.ScaleKey)
        bone_count = len(self._psa.bones)
        buffer_length = data_size * bone_count * sequence.frame_count
        if buffer_length == 0:
            # In many cases, files are written with this section, but have no data (particularly out of FModel).
            # Therefore, simply return an empty array.
            return []
        sequence_scale_keys_offset = self._scale_keys_data_offset + (sequence.frame_start_index * bone_count * data_size)
        self._fp.seek(sequence_scale_keys_offset)
        buffer = self._fp.read(buffer_length)
        offset = 0
        scale_keys = []
        for _ in range(sequence.frame_count * bone_count):
            scale_key = Psa.ScaleKey.from_buffer_copy(buffer, offset)
            scale_keys.append(scale_key)
            offset += data_size
        return scale_keys

    def _read(self, fp: BinaryIO) -> Psa:
        psa = Psa()
        while fp.read(1):
            fp.seek(-1, 1)
            section = Section.from_buffer_copy(fp.read(sizeof(Section)))
            match section.name:
                case PsaSectionName.ANIMHEAD:
                    pass
                case PsaSectionName.BONENAMES:
                    read_types(fp, PsxBone, section, psa.bones)
                case PsaSectionName.ANIMINFO:
                    sequences: list[Psa.Sequence] = []
                    read_types(fp, Psa.Sequence, section, sequences)
                    # Try to fix CUE4Parse bug, if necessary.
                    _try_fix_cue4parse_issue_103(sequences)
                    for sequence in sequences:
                        psa.sequences[sequence.name.decode()] = sequence
                case PsaSectionName.ANIMKEYS:
                    # Skip keys on this pass. We will keep this file open and read from it as needed.
                    self._keys_data_offset = fp.tell()
                    fp.seek(section.data_size * section.data_count, 1)
                case PsaSectionName.SCALEKEYS:
                    if section.data_count == 0:
                        # An empty SCALEKEYS section is common for exports from FModel, treat it as though it doesn't exist.
                        continue
                    # Skip scale keys on this pass. We will keep this file open and read from it as needed.
                    self._scale_keys_data_offset = fp.tell()
                    fp.seek(section.data_size * section.data_count, 1)
                case _:
                    fp.seek(section.data_size * section.data_count, 1)
                    print(f'Unrecognized section in PSA: {section.name!r}')
        return psa


__all__ = [
    'PsaReader',
    'read_psa'
]


def __dir__():
    return __all__
