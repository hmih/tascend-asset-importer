from ctypes import Structure
from typing import Type, TypeVar, BinaryIO
from .data import Section

T = TypeVar('T', bound=Structure)


def read_types(fp: BinaryIO, data_class: Type[T], section: Section, data: list[T]) -> None:
    """Read binary section data into a list of ctypes Structure instances.
    
    Args:
        fp: File pointer to read from
        data_class: The ctypes.Structure subclass to instantiate
        section: Section header containing data_size and data_count
        data: List to append instances to
    """
    buffer_length = section.data_size * section.data_count
    buffer = fp.read(buffer_length)
    offset = 0
    for _ in range(section.data_count):
        data.append(data_class.from_buffer_copy(buffer, offset))
        offset += section.data_size


__all__ = ['read_types']


def __dir__():
    return __all__
