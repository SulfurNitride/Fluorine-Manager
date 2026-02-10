# -*- encoding: utf-8 -*-
from __future__ import annotations

import io
import struct
from typing import Optional, Tuple

from .XRMath import IVec3


class XRReader:
    def __init__(self, buffer: bytes):
        self._buffer = buffer
        self._pos = 0

    def __len__(self) -> int:
        return len(self._buffer)

    def _read(self, size: int) -> Tuple[bytes, int]:
        pos = min(len(self._buffer), self._pos + size)
        buffer = self._buffer[self._pos : pos]
        return (buffer, pos)

    def read(self, size: int = -1) -> bytes:
        if size < 0:
            size = len(self._buffer)
        if len(self._buffer) <= self._pos:
            return b""
        (buffer, pos) = self._read(size)
        self._pos = pos
        return buffer

    def peek(self, size: int = -1) -> bytes:
        if size < 0:
            size = len(self._buffer)
        if len(self._buffer) <= self._pos:
            return b""
        (buffer, _pos) = self._read(size)
        return buffer

    def seek(self, pos: int, whence: int = io.SEEK_SET) -> int:
        if whence == 0:
            if pos < 0:
                raise ValueError(f"negative seek position {pos}")
            self._pos = pos
        elif whence == 1:
            self._pos = max(0, self._pos + pos)
        elif whence == 2:
            self._pos = max(0, len(self._buffer) + pos)
        else:
            raise ValueError("unsupported whence value")
        return self._pos

    def elapsed(self) -> int:
        return len(self._buffer) - self._pos

    def eof(self) -> bool:
        return self.elapsed() <= 0

    def u8(self) -> int:
        return int(struct.unpack("<B", self.read(1))[0])

    def s8(self) -> int:
        return int(struct.unpack("<b", self.read(1))[0])

    def u16(self) -> int:
        return int(struct.unpack("<H", self.read(2))[0])

    def s16(self) -> int:
        return int(struct.unpack("<h", self.read(2))[0])

    def u32(self) -> int:
        return int(struct.unpack("<I", self.read(4))[0])

    def s32(self) -> int:
        return int(struct.unpack("<i", self.read(4))[0])

    def u64(self) -> int:
        return int(struct.unpack("<Q", self.read(8))[0])

    def s64(self) -> int:
        return int(struct.unpack("<q", self.read(8))[0])

    def bool(self) -> bool:
        return bool(struct.unpack("<?", self.read(1))[0])

    def float(self) -> float:
        return float(struct.unpack("<f", self.read(4))[0])

    def str(self) -> str:
        chars = bytearray()
        while not self.eof():
            c = self.read(1)
            if c == b"\x00":
                return str(chars, "utf-8")
            else:
                chars += c
        return ""

    def fvec3(self) -> IVec3:
        (f1, f2, f3) = struct.unpack("<fff", self.read(12))
        return IVec3(f1, f2, f3)


class XRStream(XRReader):
    def __init__(self, buffer: bytes):
        super().__init__(buffer)
        self.last_pos: int = 0

    def find_chunk(self, id: int) -> Optional[int]:
        dw_type = 0
        dw_size = 0
        success = False
        if self.last_pos != 0:
            self.seek(self.last_pos)
            dw_type = self.u32()
            dw_size = self.u32()
            if (dw_type & (~(1 << 31))) == id:
                success = True

        if not success:
            self.seek(0)
            while not self.eof():
                dw_type = self.u32()
                dw_size = self.u32()
                if (dw_type & (~(1 << 31))) == id:
                    success = True
                    break
                else:
                    self.seek(dw_size, io.SEEK_CUR)

            if not success:
                self.last_pos = 0
                return None

        if (self._pos + dw_size) < len(self._buffer):
            self.last_pos = self._pos + dw_size
        else:
            self.last_pos = 0

        return dw_size

    def open_chunk(self, id: int) -> Optional[XRStream]:
        size = self.find_chunk(id)
        if size and size != 0:
            data = self.read(size)
            return XRStream(data)
        return None
