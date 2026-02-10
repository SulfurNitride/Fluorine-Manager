from .XRIO import XRReader
from .XRMath import IVec3, IVec4


class XRNETState:
    def __init__(self):
        self.position = IVec3(0.0, 0.0, 0.0)
        self.quaternion = IVec4(0.0, 0.0, 0.0, 0.0)
        self.enabled = False

    def read(self, reader: XRReader, fmin: IVec3, fmax: IVec3):
        self.position = self.fvec_q8(reader, fmin, fmax)
        self.quaternion = self.fqt_q8(reader)
        self.enabled = bool(reader.u8())

    def clamp(self, val: float, low: float, high: float) -> float:
        if val < low:
            return low
        elif val > high:
            return high
        else:
            return val

    def fvec_q8(self, reader: XRReader, fmin: IVec3, fmax: IVec3):
        vec = IVec3(0.0, 0.0, 0.0)
        vec.x = self.f_q8(reader, fmin.x, fmax.x)
        vec.y = self.f_q8(reader, fmin.y, fmax.y)
        vec.z = self.f_q8(reader, fmin.z, fmax.z)
        vec.x = self.clamp(vec.x, fmin.x, fmax.x)
        vec.y = self.clamp(vec.x, fmin.y, fmax.y)
        vec.z = self.clamp(vec.z, fmin.z, fmax.z)

    def fqt_q8(self, reader: XRReader):
        vec = IVec4(0.0, 0.0, 0.0, 0.0)
        vec.x = self.f_q8(reader, -1.0, 1.0)
        vec.y = self.f_q8(reader, -1.0, 1.0)
        vec.z = self.f_q8(reader, -1.0, 1.0)
        vec.w = self.f_q8(reader, -1.0, 1.0)
        vec.x = self.clamp(vec.x, -1.0, 1.0)
        vec.y = self.clamp(vec.y, -1.0, 1.0)
        vec.z = self.clamp(vec.z, -1.0, 1.0)
        vec.w = self.clamp(vec.w, -1.0, 1.0)

    def f_q8(self, reader: XRReader, fmin: float, fmax: float):
        return (float(reader.u8()) / 255.0) * (fmax - fmin) + fmin
