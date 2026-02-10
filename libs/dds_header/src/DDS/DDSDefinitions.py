import dataclasses
from enum import IntEnum, IntFlag, auto
import struct
from typing import ClassVar, List

from .glstuff import GL_IMAGE_FORMAT, CompressedGLTextureFormat, UncompressedGLTextureFormat

DDS_MAGIC_NUMBER = b"DDS "  # b"\x20\x53\x44\x44"


class IntEnumFromZero(IntEnum):
    def _generate_next_value_(name, start, count, last_values):
        return count


class D3D10_RESOURCE_DIMENSION(IntEnumFromZero):
    D3D10_RESOURCE_DIMENSION_UNKNOWN = auto()
    D3D10_RESOURCE_DIMENSION_BUFFER = auto()
    D3D10_RESOURCE_DIMENSION_TEXTURE1D = auto()
    D3D10_RESOURCE_DIMENSION_TEXTURE2D = auto()
    D3D10_RESOURCE_DIMENSION_TEXTURE3D = auto()


class DXGI_FORMAT(IntEnumFromZero):
    DXGI_FORMAT_UNKNOWN = auto()
    DXGI_FORMAT_R32G32B32A32_TYPELESS = auto()
    DXGI_FORMAT_R32G32B32A32_FLOAT = auto()
    DXGI_FORMAT_R32G32B32A32_UINT = auto()
    DXGI_FORMAT_R32G32B32A32_SINT = auto()
    DXGI_FORMAT_R32G32B32_TYPELESS = auto()
    DXGI_FORMAT_R32G32B32_FLOAT = auto()
    DXGI_FORMAT_R32G32B32_UINT = auto()
    DXGI_FORMAT_R32G32B32_SINT = auto()
    DXGI_FORMAT_R16G16B16A16_TYPELESS = auto()
    DXGI_FORMAT_R16G16B16A16_FLOAT = auto()
    DXGI_FORMAT_R16G16B16A16_UNORM = auto()
    DXGI_FORMAT_R16G16B16A16_UINT = auto()
    DXGI_FORMAT_R16G16B16A16_SNORM = auto()
    DXGI_FORMAT_R16G16B16A16_SINT = auto()
    DXGI_FORMAT_R32G32_TYPELESS = auto()
    DXGI_FORMAT_R32G32_FLOAT = auto()
    DXGI_FORMAT_R32G32_UINT = auto()
    DXGI_FORMAT_R32G32_SINT = auto()
    DXGI_FORMAT_R32G8X24_TYPELESS = auto()
    DXGI_FORMAT_D32_FLOAT_S8X24_UINT = auto()
    DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS = auto()
    DXGI_FORMAT_X32_TYPELESS_G8X24_UINT = auto()
    DXGI_FORMAT_R10G10B10A2_TYPELESS = auto()
    DXGI_FORMAT_R10G10B10A2_UNORM = auto()
    DXGI_FORMAT_R10G10B10A2_UINT = auto()
    DXGI_FORMAT_R11G11B10_FLOAT = auto()
    DXGI_FORMAT_R8G8B8A8_TYPELESS = auto()
    DXGI_FORMAT_R8G8B8A8_UNORM = auto()
    DXGI_FORMAT_R8G8B8A8_UNORM_SRGB = auto()
    DXGI_FORMAT_R8G8B8A8_UINT = auto()
    DXGI_FORMAT_R8G8B8A8_SNORM = auto()
    DXGI_FORMAT_R8G8B8A8_SINT = auto()
    DXGI_FORMAT_R16G16_TYPELESS = auto()
    DXGI_FORMAT_R16G16_FLOAT = auto()
    DXGI_FORMAT_R16G16_UNORM = auto()
    DXGI_FORMAT_R16G16_UINT = auto()
    DXGI_FORMAT_R16G16_SNORM = auto()
    DXGI_FORMAT_R16G16_SINT = auto()
    DXGI_FORMAT_R32_TYPELESS = auto()
    DXGI_FORMAT_D32_FLOAT = auto()
    DXGI_FORMAT_R32_FLOAT = auto()
    DXGI_FORMAT_R32_UINT = auto()
    DXGI_FORMAT_R32_SINT = auto()
    DXGI_FORMAT_R24G8_TYPELESS = auto()
    DXGI_FORMAT_D24_UNORM_S8_UINT = auto()
    DXGI_FORMAT_R24_UNORM_X8_TYPELESS = auto()
    DXGI_FORMAT_X24_TYPELESS_G8_UINT = auto()
    DXGI_FORMAT_R8G8_TYPELESS = auto()
    DXGI_FORMAT_R8G8_UNORM = auto()
    DXGI_FORMAT_R8G8_UINT = auto()
    DXGI_FORMAT_R8G8_SNORM = auto()
    DXGI_FORMAT_R8G8_SINT = auto()
    DXGI_FORMAT_R16_TYPELESS = auto()
    DXGI_FORMAT_R16_FLOAT = auto()
    DXGI_FORMAT_D16_UNORM = auto()
    DXGI_FORMAT_R16_UNORM = auto()
    DXGI_FORMAT_R16_UINT = auto()
    DXGI_FORMAT_R16_SNORM = auto()
    DXGI_FORMAT_R16_SINT = auto()
    DXGI_FORMAT_R8_TYPELESS = auto()
    DXGI_FORMAT_R8_UNORM = auto()
    DXGI_FORMAT_R8_UINT = auto()
    DXGI_FORMAT_R8_SNORM = auto()
    DXGI_FORMAT_R8_SINT = auto()
    DXGI_FORMAT_A8_UNORM = auto()
    DXGI_FORMAT_R1_UNORM = auto()
    DXGI_FORMAT_R9G9B9E5_SHAREDEXP = auto()
    DXGI_FORMAT_R8G8_B8G8_UNORM = auto()
    DXGI_FORMAT_G8R8_G8B8_UNORM = auto()
    DXGI_FORMAT_BC1_TYPELESS = auto()
    DXGI_FORMAT_BC1_UNORM = auto()
    DXGI_FORMAT_BC1_UNORM_SRGB = auto()
    DXGI_FORMAT_BC2_TYPELESS = auto()
    DXGI_FORMAT_BC2_UNORM = auto()
    DXGI_FORMAT_BC2_UNORM_SRGB = auto()
    DXGI_FORMAT_BC3_TYPELESS = auto()
    DXGI_FORMAT_BC3_UNORM = auto()
    DXGI_FORMAT_BC3_UNORM_SRGB = auto()
    DXGI_FORMAT_BC4_TYPELESS = auto()
    DXGI_FORMAT_BC4_UNORM = auto()
    DXGI_FORMAT_BC4_SNORM = auto()
    DXGI_FORMAT_BC5_TYPELESS = auto()
    DXGI_FORMAT_BC5_UNORM = auto()
    DXGI_FORMAT_BC5_SNORM = auto()
    DXGI_FORMAT_B5G6R5_UNORM = auto()
    DXGI_FORMAT_B5G5R5A1_UNORM = auto()
    DXGI_FORMAT_B8G8R8A8_UNORM = auto()
    DXGI_FORMAT_B8G8R8X8_UNORM = auto()
    DXGI_FORMAT_R10G10B10_XR_BIAS_A2_UNORM = auto()
    DXGI_FORMAT_B8G8R8A8_TYPELESS = auto()
    DXGI_FORMAT_B8G8R8A8_UNORM_SRGB = auto()
    DXGI_FORMAT_B8G8R8X8_TYPELESS = auto()
    DXGI_FORMAT_B8G8R8X8_UNORM_SRGB = auto()
    DXGI_FORMAT_BC6H_TYPELESS = auto()
    DXGI_FORMAT_BC6H_UF16 = auto()
    DXGI_FORMAT_BC6H_SF16 = auto()
    DXGI_FORMAT_BC7_TYPELESS = auto()
    DXGI_FORMAT_BC7_UNORM = auto()
    DXGI_FORMAT_BC7_UNORM_SRGB = auto()
    DXGI_FORMAT_AYUV = auto()
    DXGI_FORMAT_Y410 = auto()
    DXGI_FORMAT_Y416 = auto()
    DXGI_FORMAT_NV12 = auto()
    DXGI_FORMAT_P010 = auto()
    DXGI_FORMAT_P016 = auto()
    DXGI_FORMAT_420_OPAQUE = auto()
    DXGI_FORMAT_YUY2 = auto()
    DXGI_FORMAT_Y210 = auto()
    DXGI_FORMAT_Y216 = auto()
    DXGI_FORMAT_NV11 = auto()
    DXGI_FORMAT_AI44 = auto()
    DXGI_FORMAT_IA44 = auto()
    DXGI_FORMAT_P8 = auto()
    DXGI_FORMAT_A8P8 = auto()
    DXGI_FORMAT_B4G4R4A4_UNORM = auto()
    DXGI_FORMAT_P208 = auto()
    DXGI_FORMAT_V208 = auto()
    DXGI_FORMAT_V408 = auto()
    DXGI_FORMAT_FORCE_UINT = auto()


def DataclassFromBytes(dataclass):
    class LoadableDataclass(dataclass):
        def __init__(self, bytes=None):
            super(LoadableDataclass, self).__init__()
            if bytes:
                self.fromBytes(bytes)

        def fromStream(self, byteStream):
            self.fromBytes(byteStream.read(struct.calcsize(self.structFormat)))

        def fromBytes(self, bytes):
            loaded = struct.unpack(self.structFormat, bytes)
            fields = dataclasses.fields(self)
            memberIndex = 0
            for field in fields:
                if field.metadata and "count" in field.metadata:
                    # We have a list
                    listed = field.type.__args__[0]
                    myList = []
                    for i in range(field.metadata["count"]):
                        myList.append(listed(loaded[memberIndex]))
                        memberIndex += 1
                    self.__dict__[field.name] = myList
                else:
                    self.__dict__[field.name] = field.type(loaded[memberIndex])
                    memberIndex += 1

    return LoadableDataclass


@DataclassFromBytes
@dataclasses.dataclass
class DDS_PIXELFORMAT:
    structFormat: ClassVar[str] = "<II4sI4s4s4s4s"

    class Flags(IntFlag):
        DDPF_ALPHAPIXELS = 0x1
        DDPF_ALPHA = 0x2
        DDPF_FOURCC = 0x4
        DDPF_RGB = 0x40
        DDPF_YUV = 0x200
        DDPF_LUMINANCE = 0x20000

    dwSize: int = 0
    dwFlags: Flags = 0
    dwFourCC: bytes = dataclasses.field(default_factory=lambda: b"\x00\x00\x00\x00")
    dwRGBBitCount: int = 0
    dwRBitMask: bytes = dataclasses.field(default_factory=lambda: b"\x00\x00\x00\x00")
    dwGBitMask: bytes = dataclasses.field(default_factory=lambda: b"\x00\x00\x00\x00")
    dwBBitMask: bytes = dataclasses.field(default_factory=lambda: b"\x00\x00\x00\x00")
    dwABitMask: bytes = dataclasses.field(default_factory=lambda: b"\x00\x00\x00\x00")


@DataclassFromBytes
@dataclasses.dataclass
class DDS_HEADER:
    structFormat: ClassVar[str] = "<IIIIIII IIIIIIIIIII 32s IIIII"

    class Flags(IntFlag):
        DDSD_CAPS = 0x1
        DDSD_HEIGHT = 0x2
        DDSD_WIDTH = 0x4
        DDSD_PITCH = 0x8
        DDSD_PIXELFORMAT = 0x1000
        DDSD_MIPMAPCOUNT = 0x20000
        DDSD_LINEARSIZE = 0x80000
        DDSD_DEPTH = 0x800000

    class Caps(IntFlag):
        DDSCAPS_COMPLEX = 0x8
        DDSCAPS_MIPMAP = 0x400000
        DDSCAPS_TEXTURE = 0x1000

    class Caps2(IntFlag):
        DDSCAPS2_CUBEMAP = 0x200
        DDSCAPS2_CUBEMAP_POSITIVEX = 0x400
        DDSCAPS2_CUBEMAP_NEGATIVEX = 0x800
        DDSCAPS2_CUBEMAP_POSITIVEY = 0x1000
        DDSCAPS2_CUBEMAP_NEGATIVEY = 0x2000
        DDSCAPS2_CUBEMAP_POSITIVEZ = 0x4000
        DDSCAPS2_CUBEMAP_NEGATIVEZ = 0x8000
        DDSCAPS2_VOLUME = 0x200000

    dwSize: int = 0
    dwFlags: Flags = 0
    dwHeight: int = 0
    dwWidth: int = 0
    dwPitchOrLinearSize: int = 0
    dwDepth: int = 0
    dwMipMapCount: int = 0
    dwReserved1: List[int] = dataclasses.field(default_factory=lambda: [0] * 11, metadata={"count": 11})
    ddspf: DDS_PIXELFORMAT = dataclasses.field(default_factory=DDS_PIXELFORMAT)
    dwCaps: Caps = 0
    dwCaps2: Caps2 = 0
    dwCaps3: int = 0
    dwCaps4: int = 0
    dwReserved2: int = 0


@DataclassFromBytes
@dataclasses.dataclass
class DDS_HEADER_DXT10:
    structFormat: ClassVar[str] = "<IIIII"

    class MiscFlag(IntFlag):
        DDS_RESOURCE_MISC_TEXTURECUBE = 0x4

    class MiscFlags2(IntFlag):
        DDS_ALPHA_MODE_UNKNOWN = 0x0
        DDS_ALPHA_MODE_STRAIGHT = 0x1
        DDS_ALPHA_MODE_PREMULTIPLIED = 0x2
        DDS_ALPHA_MODE_OPAQUE = 0x3
        DDS_ALPHA_MODE_CUSTOM = 0x4

    dxgiFormat: DXGI_FORMAT = 0
    resourceDimension: D3D10_RESOURCE_DIMENSION = 0
    miscFlag: MiscFlag = 0
    arraySize: int = 0
    miscFlags2: MiscFlags2 = 0


class UnsupportedDDSFormatException(Exception):
    """Thrown when an unsupported DDS format or broken DDS file is given"""
    pass


def fourCCToDXGI(fourCC):
    converter = {
        b"DXT1": DXGI_FORMAT.DXGI_FORMAT_BC1_UNORM,
        b"DXT3": DXGI_FORMAT.DXGI_FORMAT_BC2_UNORM,
        b"DXT5": DXGI_FORMAT.DXGI_FORMAT_BC3_UNORM,
        b"BC4U": DXGI_FORMAT.DXGI_FORMAT_BC4_UNORM,
        b"BC4S": DXGI_FORMAT.DXGI_FORMAT_BC4_SNORM,
        b"ATI2": DXGI_FORMAT.DXGI_FORMAT_BC5_UNORM,
        b"BC5U": DXGI_FORMAT.DXGI_FORMAT_BC5_UNORM,
        b"BC5S": DXGI_FORMAT.DXGI_FORMAT_BC5_SNORM,
        b"RGBG": DXGI_FORMAT.DXGI_FORMAT_R8G8_B8G8_UNORM,
        b"GRGB": DXGI_FORMAT.DXGI_FORMAT_G8R8_G8B8_UNORM,
        (36).to_bytes(4, byteorder='little'): DXGI_FORMAT.DXGI_FORMAT_R16G16B16A16_UNORM,
        (110).to_bytes(4, byteorder='little'): DXGI_FORMAT.DXGI_FORMAT_R16G16B16A16_SNORM,
        (111).to_bytes(4, byteorder='little'): DXGI_FORMAT.DXGI_FORMAT_R16_FLOAT,
        (112).to_bytes(4, byteorder='little'): DXGI_FORMAT.DXGI_FORMAT_R16G16_FLOAT,
        (113).to_bytes(4, byteorder='little'): DXGI_FORMAT.DXGI_FORMAT_R16G16B16A16_FLOAT,
        (114).to_bytes(4, byteorder='little'): DXGI_FORMAT.DXGI_FORMAT_R32_FLOAT,
        (115).to_bytes(4, byteorder='little'): DXGI_FORMAT.DXGI_FORMAT_R32G32_FLOAT,
        (116).to_bytes(4, byteorder='little'): DXGI_FORMAT.DXGI_FORMAT_R32G32B32A32_FLOAT
        # Note: Some formats with a four character code don't have a DXGI format, e.g. pre-multiplied BC2/3 images
    }
    if fourCC in converter:
        return converter[fourCC]


dxgiToGL = {
    DXGI_FORMAT.DXGI_FORMAT_UNKNOWN: None,
    DXGI_FORMAT.DXGI_FORMAT_R32G32B32A32_TYPELESS: None,
    DXGI_FORMAT.DXGI_FORMAT_R32G32B32A32_FLOAT: UncompressedGLTextureFormat(None, GL_IMAGE_FORMAT.GL_RGBA32F,
                                                                            GL_IMAGE_FORMAT.GL_RGBA,
                                                                            GL_IMAGE_FORMAT.GL_FLOAT),
    DXGI_FORMAT.DXGI_FORMAT_R32G32B32A32_UINT: UncompressedGLTextureFormat(None, GL_IMAGE_FORMAT.GL_RGBA32UI,
                                                                           GL_IMAGE_FORMAT.GL_RGBA_INTEGER,
                                                                           GL_IMAGE_FORMAT.GL_UNSIGNED_INT),
    DXGI_FORMAT.DXGI_FORMAT_R32G32B32A32_SINT: UncompressedGLTextureFormat(None, GL_IMAGE_FORMAT.GL_RGBA32I,
                                                                           GL_IMAGE_FORMAT.GL_RGBA_INTEGER,
                                                                           GL_IMAGE_FORMAT.GL_INT),
    DXGI_FORMAT.DXGI_FORMAT_R32G32B32_TYPELESS: None,
    DXGI_FORMAT.DXGI_FORMAT_R32G32B32_FLOAT: UncompressedGLTextureFormat(None, GL_IMAGE_FORMAT.GL_RGB32F,
                                                                         GL_IMAGE_FORMAT.GL_RGB,
                                                                         GL_IMAGE_FORMAT.GL_FLOAT),
    DXGI_FORMAT.DXGI_FORMAT_R32G32B32_UINT: UncompressedGLTextureFormat(None, GL_IMAGE_FORMAT.GL_RGB32UI,
                                                                        GL_IMAGE_FORMAT.GL_RGB_INTEGER,
                                                                        GL_IMAGE_FORMAT.GL_UNSIGNED_INT),
    DXGI_FORMAT.DXGI_FORMAT_R32G32B32_SINT: UncompressedGLTextureFormat(None, GL_IMAGE_FORMAT.GL_RGB32I,
                                                                        GL_IMAGE_FORMAT.GL_RGB_INTEGER,
                                                                        GL_IMAGE_FORMAT.GL_INT),
    DXGI_FORMAT.DXGI_FORMAT_R16G16B16A16_TYPELESS: None,
    DXGI_FORMAT.DXGI_FORMAT_R16G16B16A16_FLOAT: UncompressedGLTextureFormat(((3, 0), [b"GL_ARB_half_float_pixel"]),
                                                                            GL_IMAGE_FORMAT.GL_RGBA16F,
                                                                            GL_IMAGE_FORMAT.GL_RGBA,
                                                                            GL_IMAGE_FORMAT.GL_HALF_FLOAT),
    DXGI_FORMAT.DXGI_FORMAT_R16G16B16A16_UNORM: UncompressedGLTextureFormat(None, GL_IMAGE_FORMAT.GL_RGBA16,
                                                                            GL_IMAGE_FORMAT.GL_RGBA,
                                                                            GL_IMAGE_FORMAT.GL_UNSIGNED_SHORT),
    DXGI_FORMAT.DXGI_FORMAT_R16G16B16A16_UINT: UncompressedGLTextureFormat(None, GL_IMAGE_FORMAT.GL_RGBA16UI,
                                                                           GL_IMAGE_FORMAT.GL_RGBA_INTEGER,
                                                                           GL_IMAGE_FORMAT.GL_UNSIGNED_SHORT),
    DXGI_FORMAT.DXGI_FORMAT_R16G16B16A16_SNORM: UncompressedGLTextureFormat(None, GL_IMAGE_FORMAT.GL_RGBA16_SNORM,
                                                                            GL_IMAGE_FORMAT.GL_RGBA,
                                                                            GL_IMAGE_FORMAT.GL_SHORT),
    DXGI_FORMAT.DXGI_FORMAT_R16G16B16A16_SINT: UncompressedGLTextureFormat(None, GL_IMAGE_FORMAT.GL_RGBA16I,
                                                                           GL_IMAGE_FORMAT.GL_RGBA_INTEGER,
                                                                           GL_IMAGE_FORMAT.GL_SHORT),
    DXGI_FORMAT.DXGI_FORMAT_R32G32_TYPELESS: None,
    DXGI_FORMAT.DXGI_FORMAT_R32G32_FLOAT: UncompressedGLTextureFormat(None, GL_IMAGE_FORMAT.GL_RG32F,
                                                                      GL_IMAGE_FORMAT.GL_RG, GL_IMAGE_FORMAT.GL_FLOAT),
    DXGI_FORMAT.DXGI_FORMAT_R32G32_UINT: UncompressedGLTextureFormat(None, GL_IMAGE_FORMAT.GL_RG32UI,
                                                                     GL_IMAGE_FORMAT.GL_RG_INTEGER,
                                                                     GL_IMAGE_FORMAT.GL_UNSIGNED_INT),
    DXGI_FORMAT.DXGI_FORMAT_R32G32_SINT: UncompressedGLTextureFormat(None, GL_IMAGE_FORMAT.GL_RG32I,
                                                                     GL_IMAGE_FORMAT.GL_RG, GL_IMAGE_FORMAT.GL_INT),
    DXGI_FORMAT.DXGI_FORMAT_R32G8X24_TYPELESS: None,
    DXGI_FORMAT.DXGI_FORMAT_D32_FLOAT_S8X24_UINT: None,
    DXGI_FORMAT.DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS: UncompressedGLTextureFormat(None, GL_IMAGE_FORMAT.GL_R32F,
                                                                                  GL_IMAGE_FORMAT.GL_RG,
                                                                                  GL_IMAGE_FORMAT.GL_FLOAT),
    DXGI_FORMAT.DXGI_FORMAT_X32_TYPELESS_G8X24_UINT: None,
    DXGI_FORMAT.DXGI_FORMAT_R10G10B10A2_TYPELESS: None,
    DXGI_FORMAT.DXGI_FORMAT_R10G10B10A2_UNORM: UncompressedGLTextureFormat(None, GL_IMAGE_FORMAT.GL_RGB10_A2,
                                                                           GL_IMAGE_FORMAT.GL_RGBA,
                                                                           GL_IMAGE_FORMAT.GL_UNSIGNED_INT_2_10_10_10_REV),
    DXGI_FORMAT.DXGI_FORMAT_R10G10B10A2_UINT: UncompressedGLTextureFormat(None, GL_IMAGE_FORMAT.GL_RGB10_A2UI,
                                                                          GL_IMAGE_FORMAT.GL_RGBA_INTEGER,
                                                                          GL_IMAGE_FORMAT.GL_UNSIGNED_INT_2_10_10_10_REV),
    DXGI_FORMAT.DXGI_FORMAT_R11G11B10_FLOAT: UncompressedGLTextureFormat(((-1, 0), [b"EXT_packed_float"]),
                                                                         GL_IMAGE_FORMAT.GL_R11F_G11F_B10F,
                                                                         GL_IMAGE_FORMAT.GL_RGB,
                                                                         GL_IMAGE_FORMAT.GL_UNSIGNED_INT_10F_11F_11F_REV_EXT),
    DXGI_FORMAT.DXGI_FORMAT_R8G8B8A8_TYPELESS: None,
    DXGI_FORMAT.DXGI_FORMAT_R8G8B8A8_UNORM: UncompressedGLTextureFormat(None, GL_IMAGE_FORMAT.GL_RGBA8,
                                                                        GL_IMAGE_FORMAT.GL_RGBA,
                                                                        GL_IMAGE_FORMAT.GL_UNSIGNED_BYTE),
    DXGI_FORMAT.DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: UncompressedGLTextureFormat(((2, 1), [b"GL_EXT_texture_sRGB"]),
                                                                             GL_IMAGE_FORMAT.GL_SRGB8_ALPHA8,
                                                                             GL_IMAGE_FORMAT.GL_RGBA,
                                                                             GL_IMAGE_FORMAT.GL_UNSIGNED_BYTE),
    DXGI_FORMAT.DXGI_FORMAT_R8G8B8A8_UINT: UncompressedGLTextureFormat(None, GL_IMAGE_FORMAT.GL_RGBA8UI,
                                                                       GL_IMAGE_FORMAT.GL_RGBA_INTEGER,
                                                                       GL_IMAGE_FORMAT.GL_UNSIGNED_BYTE),
    DXGI_FORMAT.DXGI_FORMAT_R8G8B8A8_SNORM: UncompressedGLTextureFormat(None, GL_IMAGE_FORMAT.GL_RGBA8_SNORM,
                                                                        GL_IMAGE_FORMAT.GL_RGBA,
                                                                        GL_IMAGE_FORMAT.GL_BYTE),
    DXGI_FORMAT.DXGI_FORMAT_R8G8B8A8_SINT: UncompressedGLTextureFormat(None, GL_IMAGE_FORMAT.GL_RGBA8I,
                                                                       GL_IMAGE_FORMAT.GL_RGBA_INTEGER,
                                                                       GL_IMAGE_FORMAT.GL_BYTE),
    DXGI_FORMAT.DXGI_FORMAT_R16G16_TYPELESS: None,
    DXGI_FORMAT.DXGI_FORMAT_R16G16_FLOAT: UncompressedGLTextureFormat(((3, 0), [b"GL_ARB_half_float_pixel"]),
                                                                      GL_IMAGE_FORMAT.GL_RG16F, GL_IMAGE_FORMAT.GL_RG,
                                                                      GL_IMAGE_FORMAT.GL_HALF_FLOAT),
    DXGI_FORMAT.DXGI_FORMAT_R16G16_UNORM: UncompressedGLTextureFormat(None, GL_IMAGE_FORMAT.GL_RG16,
                                                                      GL_IMAGE_FORMAT.GL_RG,
                                                                      GL_IMAGE_FORMAT.GL_UNSIGNED_SHORT),
    DXGI_FORMAT.DXGI_FORMAT_R16G16_UINT: UncompressedGLTextureFormat(None, GL_IMAGE_FORMAT.GL_RG16UI,
                                                                     GL_IMAGE_FORMAT.GL_RG_INTEGER,
                                                                     GL_IMAGE_FORMAT.GL_UNSIGNED_SHORT),
    DXGI_FORMAT.DXGI_FORMAT_R16G16_SNORM: UncompressedGLTextureFormat(None, GL_IMAGE_FORMAT.GL_RG16_SNORM,
                                                                      GL_IMAGE_FORMAT.GL_RG, GL_IMAGE_FORMAT.GL_SHORT),
    DXGI_FORMAT.DXGI_FORMAT_R16G16_SINT: UncompressedGLTextureFormat(None, GL_IMAGE_FORMAT.GL_RG16I,
                                                                     GL_IMAGE_FORMAT.GL_RG_INTEGER,
                                                                     GL_IMAGE_FORMAT.GL_SHORT),
    DXGI_FORMAT.DXGI_FORMAT_R32_TYPELESS: None,
    DXGI_FORMAT.DXGI_FORMAT_D32_FLOAT: None,
    DXGI_FORMAT.DXGI_FORMAT_R32_FLOAT: UncompressedGLTextureFormat(None, GL_IMAGE_FORMAT.GL_R32F,
                                                                   GL_IMAGE_FORMAT.GL_RED, GL_IMAGE_FORMAT.GL_FLOAT),
    DXGI_FORMAT.DXGI_FORMAT_R32_UINT: UncompressedGLTextureFormat(None, GL_IMAGE_FORMAT.GL_R32UI,
                                                                  GL_IMAGE_FORMAT.GL_RED_INTEGER,
                                                                  GL_IMAGE_FORMAT.GL_UNSIGNED_INT),
    DXGI_FORMAT.DXGI_FORMAT_R32_SINT: UncompressedGLTextureFormat(None, GL_IMAGE_FORMAT.GL_R32I,
                                                                  GL_IMAGE_FORMAT.GL_RED_INTEGER,
                                                                  GL_IMAGE_FORMAT.GL_INT),
    DXGI_FORMAT.DXGI_FORMAT_R24G8_TYPELESS: None,
    DXGI_FORMAT.DXGI_FORMAT_D24_UNORM_S8_UINT: None,
    DXGI_FORMAT.DXGI_FORMAT_R24_UNORM_X8_TYPELESS: None,
    DXGI_FORMAT.DXGI_FORMAT_X24_TYPELESS_G8_UINT: None,
    DXGI_FORMAT.DXGI_FORMAT_R8G8_TYPELESS: None,
    DXGI_FORMAT.DXGI_FORMAT_R8G8_UNORM: UncompressedGLTextureFormat(None, GL_IMAGE_FORMAT.GL_RG8, GL_IMAGE_FORMAT.GL_RG,
                                                                    GL_IMAGE_FORMAT.GL_UNSIGNED_BYTE),
    DXGI_FORMAT.DXGI_FORMAT_R8G8_UINT: UncompressedGLTextureFormat(None, GL_IMAGE_FORMAT.GL_RG8UI,
                                                                   GL_IMAGE_FORMAT.GL_RG_INTEGER,
                                                                   GL_IMAGE_FORMAT.GL_UNSIGNED_BYTE),
    DXGI_FORMAT.DXGI_FORMAT_R8G8_SNORM: UncompressedGLTextureFormat(None, GL_IMAGE_FORMAT.GL_RG8_SNORM,
                                                                    GL_IMAGE_FORMAT.GL_RG, GL_IMAGE_FORMAT.GL_BYTE),
    DXGI_FORMAT.DXGI_FORMAT_R8G8_SINT: UncompressedGLTextureFormat(None, GL_IMAGE_FORMAT.GL_RG8I,
                                                                   GL_IMAGE_FORMAT.GL_RG_INTEGER,
                                                                   GL_IMAGE_FORMAT.GL_BYTE),
    DXGI_FORMAT.DXGI_FORMAT_R16_TYPELESS: None,
    DXGI_FORMAT.DXGI_FORMAT_R16_FLOAT: UncompressedGLTextureFormat(((3, 0), [b"GL_ARB_half_float_pixel"]),
                                                                   GL_IMAGE_FORMAT.GL_R16F, GL_IMAGE_FORMAT.GL_RED,
                                                                   GL_IMAGE_FORMAT.GL_HALF_FLOAT),
    DXGI_FORMAT.DXGI_FORMAT_D16_UNORM: None,
    DXGI_FORMAT.DXGI_FORMAT_R16_UNORM: UncompressedGLTextureFormat(None, GL_IMAGE_FORMAT.GL_R16, GL_IMAGE_FORMAT.GL_RED,
                                                                   GL_IMAGE_FORMAT.GL_UNSIGNED_SHORT),
    DXGI_FORMAT.DXGI_FORMAT_R16_UINT: UncompressedGLTextureFormat(None, GL_IMAGE_FORMAT.GL_R16UI,
                                                                  GL_IMAGE_FORMAT.GL_RED_INTEGER,
                                                                  GL_IMAGE_FORMAT.GL_UNSIGNED_SHORT),
    DXGI_FORMAT.DXGI_FORMAT_R16_SNORM: UncompressedGLTextureFormat(None, GL_IMAGE_FORMAT.GL_R16_SNORM,
                                                                   GL_IMAGE_FORMAT.GL_RED, GL_IMAGE_FORMAT.GL_SHORT),
    DXGI_FORMAT.DXGI_FORMAT_R16_SINT: UncompressedGLTextureFormat(None, GL_IMAGE_FORMAT.GL_R16I,
                                                                  GL_IMAGE_FORMAT.GL_RED_INTEGER,
                                                                  GL_IMAGE_FORMAT.GL_SHORT),
    DXGI_FORMAT.DXGI_FORMAT_R8_TYPELESS: None,
    DXGI_FORMAT.DXGI_FORMAT_R8_UNORM: UncompressedGLTextureFormat(None, GL_IMAGE_FORMAT.GL_R8, GL_IMAGE_FORMAT.GL_RED,
                                                                  GL_IMAGE_FORMAT.GL_UNSIGNED_BYTE),
    DXGI_FORMAT.DXGI_FORMAT_R8_UINT: UncompressedGLTextureFormat(None, GL_IMAGE_FORMAT.GL_R8UI,
                                                                 GL_IMAGE_FORMAT.GL_RED_INTEGER,
                                                                 GL_IMAGE_FORMAT.GL_UNSIGNED_BYTE),
    DXGI_FORMAT.DXGI_FORMAT_R8_SNORM: UncompressedGLTextureFormat(None, GL_IMAGE_FORMAT.GL_R8_SNORM,
                                                                  GL_IMAGE_FORMAT.GL_RED, GL_IMAGE_FORMAT.GL_BYTE),
    DXGI_FORMAT.DXGI_FORMAT_R8_SINT: UncompressedGLTextureFormat(None, GL_IMAGE_FORMAT.GL_R8I,
                                                                 GL_IMAGE_FORMAT.GL_RED_INTEGER,
                                                                 GL_IMAGE_FORMAT.GL_BYTE),
    DXGI_FORMAT.DXGI_FORMAT_A8_UNORM: UncompressedGLTextureFormat(None, GL_IMAGE_FORMAT.GL_ALPHA8,
                                                                  GL_IMAGE_FORMAT.GL_ALPHA,
                                                                  GL_IMAGE_FORMAT.GL_UNSIGNED_BYTE),
    DXGI_FORMAT.DXGI_FORMAT_R1_UNORM: None,
    DXGI_FORMAT.DXGI_FORMAT_R9G9B9E5_SHAREDEXP: UncompressedGLTextureFormat(
        ((3, 0), [b"GL_EXT_texture_shared_exponent"]), GL_IMAGE_FORMAT.GL_RGB9_E5, GL_IMAGE_FORMAT.GL_RGB,
        GL_IMAGE_FORMAT.GL_UNSIGNED_INT_5_9_9_9_REV_EXT),
    DXGI_FORMAT.DXGI_FORMAT_R8G8_B8G8_UNORM: None,
    DXGI_FORMAT.DXGI_FORMAT_G8R8_G8B8_UNORM: None,
    DXGI_FORMAT.DXGI_FORMAT_BC1_TYPELESS: None,
    DXGI_FORMAT.DXGI_FORMAT_BC1_UNORM: CompressedGLTextureFormat(((-1, 0), [b"GL_EXT_texture_compression_s3tc"]),
                                                                 GL_IMAGE_FORMAT.GL_COMPRESSED_RGBA_S3TC_DXT1_EXT),
    DXGI_FORMAT.DXGI_FORMAT_BC1_UNORM_SRGB: CompressedGLTextureFormat(((-1, 0), [b"GL_EXT_texture_compression_s3tc"]),
                                                                      GL_IMAGE_FORMAT.GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT1_EXT),
    DXGI_FORMAT.DXGI_FORMAT_BC2_TYPELESS: None,
    DXGI_FORMAT.DXGI_FORMAT_BC2_UNORM: CompressedGLTextureFormat(((-1, 0), [b"GL_EXT_texture_compression_s3tc"]),
                                                                 GL_IMAGE_FORMAT.GL_COMPRESSED_RGBA_S3TC_DXT3_EXT),
    DXGI_FORMAT.DXGI_FORMAT_BC2_UNORM_SRGB: CompressedGLTextureFormat(((-1, 0), [b"GL_EXT_texture_compression_s3tc"]),
                                                                      GL_IMAGE_FORMAT.GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT3_EXT),
    DXGI_FORMAT.DXGI_FORMAT_BC3_TYPELESS: None,
    DXGI_FORMAT.DXGI_FORMAT_BC3_UNORM: CompressedGLTextureFormat(((-1, 0), [b"GL_EXT_texture_compression_s3tc"]),
                                                                 GL_IMAGE_FORMAT.GL_COMPRESSED_RGBA_S3TC_DXT5_EXT),
    DXGI_FORMAT.DXGI_FORMAT_BC3_UNORM_SRGB: CompressedGLTextureFormat(((-1, 0), [b"GL_EXT_texture_compression_s3tc"]),
                                                                      GL_IMAGE_FORMAT.GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT),
    DXGI_FORMAT.DXGI_FORMAT_BC4_TYPELESS: None,
    DXGI_FORMAT.DXGI_FORMAT_BC4_UNORM: CompressedGLTextureFormat(((3, 0), [b"GL_ARB_texture_compression_rgtc"]),
                                                                 GL_IMAGE_FORMAT.GL_COMPRESSED_RED_RGTC1),
    DXGI_FORMAT.DXGI_FORMAT_BC4_SNORM: CompressedGLTextureFormat(((3, 0), [b"GL_ARB_texture_compression_rgtc"]),
                                                                 GL_IMAGE_FORMAT.GL_COMPRESSED_SIGNED_RED_RGTC1),
    DXGI_FORMAT.DXGI_FORMAT_BC5_TYPELESS: None,
    DXGI_FORMAT.DXGI_FORMAT_BC5_UNORM: CompressedGLTextureFormat(((3, 0), [b"GL_ARB_texture_compression_rgtc"]),
                                                                 GL_IMAGE_FORMAT.GL_COMPRESSED_RG_RGTC2),
    DXGI_FORMAT.DXGI_FORMAT_BC5_SNORM: CompressedGLTextureFormat(((3, 0), [b"GL_ARB_texture_compression_rgtc"]),
                                                                 GL_IMAGE_FORMAT.GL_COMPRESSED_SIGNED_RG_RGTC2),
    DXGI_FORMAT.DXGI_FORMAT_B5G6R5_UNORM: UncompressedGLTextureFormat(None, GL_IMAGE_FORMAT.GL_RGB,
                                                                      GL_IMAGE_FORMAT.GL_RGB,
                                                                      GL_IMAGE_FORMAT.GL_UNSIGNED_SHORT_5_6_5_REV),
    DXGI_FORMAT.DXGI_FORMAT_B5G5R5A1_UNORM: UncompressedGLTextureFormat(None, GL_IMAGE_FORMAT.GL_RGB5_A1,
                                                                        GL_IMAGE_FORMAT.GL_BGRA,
                                                                        GL_IMAGE_FORMAT.GL_UNSIGNED_SHORT_1_5_5_5_REV),
    DXGI_FORMAT.DXGI_FORMAT_B8G8R8A8_UNORM: UncompressedGLTextureFormat(None, GL_IMAGE_FORMAT.GL_RGBA8,
                                                                        GL_IMAGE_FORMAT.GL_BGRA,
                                                                        GL_IMAGE_FORMAT.GL_UNSIGNED_BYTE),
    DXGI_FORMAT.DXGI_FORMAT_B8G8R8X8_UNORM: UncompressedGLTextureFormat(None, GL_IMAGE_FORMAT.GL_RGB8,
                                                                        GL_IMAGE_FORMAT.GL_BGRA,
                                                                        GL_IMAGE_FORMAT.GL_UNSIGNED_BYTE),
    DXGI_FORMAT.DXGI_FORMAT_R10G10B10_XR_BIAS_A2_UNORM: None,
    DXGI_FORMAT.DXGI_FORMAT_B8G8R8A8_TYPELESS: None,
    DXGI_FORMAT.DXGI_FORMAT_B8G8R8A8_UNORM_SRGB: UncompressedGLTextureFormat(((2, 1), [b"GL_EXT_texture_sRGB"]),
                                                                             GL_IMAGE_FORMAT.GL_SRGB8_ALPHA8,
                                                                             GL_IMAGE_FORMAT.GL_BGRA,
                                                                             GL_IMAGE_FORMAT.GL_UNSIGNED_BYTE),
    DXGI_FORMAT.DXGI_FORMAT_B8G8R8X8_TYPELESS: None,
    DXGI_FORMAT.DXGI_FORMAT_B8G8R8X8_UNORM_SRGB: UncompressedGLTextureFormat(((2, 1), [b"GL_EXT_texture_sRGB"]),
                                                                             GL_IMAGE_FORMAT.GL_SRGB8,
                                                                             GL_IMAGE_FORMAT.GL_BGRA,
                                                                             GL_IMAGE_FORMAT.GL_UNSIGNED_BYTE),
    DXGI_FORMAT.DXGI_FORMAT_BC6H_TYPELESS: None,
    DXGI_FORMAT.DXGI_FORMAT_BC6H_UF16: CompressedGLTextureFormat(((4, 2), [b"GL_ARB_texture_compression_bptc"]),
                                                                 GL_IMAGE_FORMAT.GL_COMPRESSED_RGB_BPTC_UNSIGNED_FLOAT),
    DXGI_FORMAT.DXGI_FORMAT_BC6H_SF16: CompressedGLTextureFormat(((4, 2), [b"GL_ARB_texture_compression_bptc"]),
                                                                 GL_IMAGE_FORMAT.GL_COMPRESSED_RGB_BPTC_SIGNED_FLOAT),
    DXGI_FORMAT.DXGI_FORMAT_BC7_TYPELESS: None,
    DXGI_FORMAT.DXGI_FORMAT_BC7_UNORM: CompressedGLTextureFormat(((4, 2), [b"GL_ARB_texture_compression_bptc"]),
                                                                 GL_IMAGE_FORMAT.GL_COMPRESSED_RGBA_BPTC_UNORM),
    DXGI_FORMAT.DXGI_FORMAT_BC7_UNORM_SRGB: CompressedGLTextureFormat(((4, 2), [b"GL_ARB_texture_compression_bptc"]),
                                                                      GL_IMAGE_FORMAT.GL_COMPRESSED_SRGB_ALPHA_BPTC_UNORM),
    DXGI_FORMAT.DXGI_FORMAT_AYUV: None,
    DXGI_FORMAT.DXGI_FORMAT_Y410: None,
    DXGI_FORMAT.DXGI_FORMAT_Y416: None,
    DXGI_FORMAT.DXGI_FORMAT_NV12: None,
    DXGI_FORMAT.DXGI_FORMAT_P010: None,
    DXGI_FORMAT.DXGI_FORMAT_P016: None,
    DXGI_FORMAT.DXGI_FORMAT_420_OPAQUE: None,
    DXGI_FORMAT.DXGI_FORMAT_YUY2: None,
    DXGI_FORMAT.DXGI_FORMAT_Y210: None,
    DXGI_FORMAT.DXGI_FORMAT_Y216: None,
    DXGI_FORMAT.DXGI_FORMAT_NV11: None,
    DXGI_FORMAT.DXGI_FORMAT_AI44: None,
    DXGI_FORMAT.DXGI_FORMAT_IA44: None,
    DXGI_FORMAT.DXGI_FORMAT_P8: None,
    DXGI_FORMAT.DXGI_FORMAT_A8P8: None,
    DXGI_FORMAT.DXGI_FORMAT_B4G4R4A4_UNORM: UncompressedGLTextureFormat(None, GL_IMAGE_FORMAT.GL_RGBA4,
                                                                        GL_IMAGE_FORMAT.GL_BGRA,
                                                                        GL_IMAGE_FORMAT.GL_UNSIGNED_SHORT_4_4_4_4_REV),
    DXGI_FORMAT.DXGI_FORMAT_P208: None,
    DXGI_FORMAT.DXGI_FORMAT_V208: None,
    DXGI_FORMAT.DXGI_FORMAT_V408: None,
    DXGI_FORMAT.DXGI_FORMAT_FORCE_UINT: None
}


def buildConverter(byteCount, usedBitCounts={8}, bitmasks=None, intmasks=None):
    # A converter converts the data to boring BGRA with enough bits per channel to fit the original
    glFormat = GL_IMAGE_FORMAT.GL_BGRA
    biggestComponent = max(usedBitCounts)
    biggestComponent = (biggestComponent + 7) // 8
    if biggestComponent == 1:
        packFormat = "=4B"
        glType = GL_IMAGE_FORMAT.GL_UNSIGNED_BYTE
        glInternalFormat = GL_IMAGE_FORMAT.GL_RGBA8
    elif biggestComponent == 2:
        packFormat = "=4H"
        glType = GL_IMAGE_FORMAT.GL_UNSIGNED_SHORT
        glInternalFormat = GL_IMAGE_FORMAT.GL_RGBA16
    elif biggestComponent <= 4:
        biggestComponent == 4
        packFormat = "=4I"
        glType = GL_IMAGE_FORMAT.GL_UNSIGNED_INT
        glInternalFormat = GL_IMAGE_FORMAT.GL_RGBA32F
    multiplier = 255 ** biggestComponent

    if byteCount == 1:
        unpackFormat = "B"
        unpackCombiner = lambda x: x
    elif byteCount == 2:
        unpackFormat = "H"
        unpackCombiner = lambda x: x
    elif byteCount == 3:
        unpackFormat = "3s"

        def unpackCombiner(x):
            x = struct.unpack("<HB", x)
            return x[0] + (x[1] << 16)
    elif byteCount == 4:
        unpackFormat = "I"
        unpackCombiner = lambda x: x

    if intmasks:
        rIntMask, gIntMask, bIntMask, aIntMask = intmasks
    else:
        rIntMask = 0
        gIntMask = 0
        bIntMask = 0
        aIntMask = 0

    if bitmasks:
        if "r" in bitmasks:
            rIntMask = unpackCombiner(*struct.unpack("<" + unpackFormat, bitmasks["r"][:byteCount]))
        if "g" in bitmasks:
            gIntMask = unpackCombiner(*struct.unpack("<" + unpackFormat, bitmasks["g"][:byteCount]))
        if "b" in bitmasks:
            bIntMask = unpackCombiner(*struct.unpack("<" + unpackFormat, bitmasks["b"][:byteCount]))
        if "a" in bitmasks:
            aIntMask = unpackCombiner(*struct.unpack("<" + unpackFormat, bitmasks["a"][:byteCount]))
        if "luminance" in bitmasks:
            rIntMask = unpackCombiner(*struct.unpack("<" + unpackFormat, bitmasks["luminance"][:byteCount]))
            gIntMask = unpackCombiner(*struct.unpack("<" + unpackFormat, bitmasks["luminance"][:byteCount]))
            bIntMask = unpackCombiner(*struct.unpack("<" + unpackFormat, bitmasks["luminance"][:byteCount]))

    rDivisor = 2 ** bin(rIntMask).count("1") - 1
    gDivisor = 2 ** bin(gIntMask).count("1") - 1
    bDivisor = 2 ** bin(bIntMask).count("1") - 1
    aDivisor = 2 ** bin(aIntMask).count("1") - 1

    rShift = (rIntMask & -rIntMask).bit_length() - 1
    gShift = (gIntMask & -gIntMask).bit_length() - 1
    bShift = (bIntMask & -bIntMask).bit_length() - 1
    aShift = (aIntMask & -aIntMask).bit_length() - 1

    def convert(imageData):
        length = len(imageData) // byteCount
        unpackString = "<" + ((str(length) + unpackFormat) if unpackFormat.isalpha() else unpackFormat * length)
        unpacked = struct.unpack(unpackString, imageData)
        repacked = bytearray(length * biggestComponent * 4)
        repackIndex = 0
        for pixel in unpacked:
            pixel = unpackCombiner(pixel)
            if rIntMask:
                red = (((rIntMask & pixel) >> rShift) * multiplier) // rDivisor
            else:
                red = 0
            if gIntMask:
                green = (((gIntMask & pixel) >> gShift) * multiplier) // gDivisor
            else:
                green = 0
            if bIntMask:
                blue = (((bIntMask & pixel) >> bShift) * multiplier) // bDivisor
            else:
                blue = 0
            if aIntMask:
                alpha = (((aIntMask & pixel) >> aShift) * multiplier) // aDivisor
            else:
                alpha = multiplier
            struct.pack_into(packFormat, repacked, repackIndex, blue, green, red, alpha)
            repackIndex += biggestComponent * 4
        return bytes(repacked)

    return (convert, glInternalFormat, glFormat, glType)


def getGLFormat(pixelFormat, dxt10Header=None):
    # Half or more of this function may be unreachable or otherwise redundant.
    glInternalFormat = None
    dxgiFormat = None
    if dxt10Header:
        dxgiFormat = dxt10Header.dxgiFormat
    flags = pixelFormat.dwFlags
    if flags & pixelFormat.Flags.DDPF_FOURCC:
        fourCC = pixelFormat.dwFourCC
        if fourCC == b"DX10" and not dxt10Header:
            raise UnsupportedDDSFormatException()
        dxgiFormat = dxt10Header.dxgiFormat if dxt10Header else fourCCToDXGI(fourCC)

    if dxgiFormat:
        return dxgiToGL[dxgiFormat]
    elif flags & (
        pixelFormat.Flags.DDPF_ALPHA | pixelFormat.Flags.DDPF_RGB | pixelFormat.Flags.DDPF_YUV | pixelFormat.Flags.DDPF_LUMINANCE):
        compressed = False

        rBitmask = None
        gBitmask = None
        bBitmask = None
        aBitmask = None
        lumBitmask = None
        if flags & (pixelFormat.Flags.DDPF_ALPHA | pixelFormat.Flags.DDPF_ALPHAPIXELS):
            aBitmask = pixelFormat.dwABitMask
        if flags & (pixelFormat.Flags.DDPF_RGB | pixelFormat.Flags.DDPF_YUV):
            rBitmask = pixelFormat.dwRBitMask
            gBitmask = pixelFormat.dwGBitMask
            bBitmask = pixelFormat.dwBBitMask
        if flags & pixelFormat.Flags.DDPF_LUMINANCE:
            lumBitmask = pixelFormat.dwRBitMask

        def bitCount(theBytes):
            count = 0
            for byte in theBytes:
                count += bin(byte).count("1")
            return count

        def firstBit(theBytes):
            index = 0
            for byte in theBytes:
                if byte != 0:
                    return index + format(byte, 'b').find("1")
                index += 8

        bitCounts = dict()
        starts = dict()
        namedBitmasks = dict()
        if rBitmask:
            bitCounts["r"] = bitCount(rBitmask)
            starts["r"] = firstBit(rBitmask)
            namedBitmasks["r"] = rBitmask
        if gBitmask:
            bitCounts["g"] = bitCount(gBitmask)
            starts["g"] = firstBit(gBitmask)
            namedBitmasks["g"] = gBitmask
        if bBitmask:
            bitCounts["b"] = bitCount(bBitmask)
            starts["b"] = firstBit(bBitmask)
            namedBitmasks["b"] = bBitmask
        if aBitmask:
            bitCounts["a"] = bitCount(aBitmask)
            starts["a"] = firstBit(aBitmask)
            namedBitmasks["a"] = aBitmask
        if lumBitmask:
            bitCounts["luminance"] = bitCount(lumBitmask)
            starts["luminance"] = firstBit(lumBitmask)
            namedBitmasks["luminance"] = lumBitmask

        toSort = []
        for key in starts:
            toSort.append((starts[key], key))
        toSort.sort()

        glInternalFormatName = ["GL"]
        glRequirements = None

        desc = "GL_"
        lastBitCount = -1
        usedBitCounts = set()
        for pos, channel in toSort:
            if lastBitCount != bitCounts[channel]:
                if lastBitCount != -1:
                    desc += str(lastBitCount)
                lastBitCount = bitCounts[channel]
                usedBitCounts.add(lastBitCount)
            desc += channel
        if len(usedBitCounts) != 1:
            desc += str(lastBitCount)

        desc = desc.upper()
        glFormat = GL_IMAGE_FORMAT[desc] if desc in GL_IMAGE_FORMAT.__members__ else None

        numComponents = len(toSort)
        byteCount = (pixelFormat.dwRGBBitCount + 7) // 8
        needsConversion = False
        if len(usedBitCounts) == 1:
            if lastBitCount == 8:
                glType = GL_IMAGE_FORMAT.GL_UNSIGNED_BYTE
            elif lastBitCount == 16:
                glType = GL_IMAGE_FORMAT.GL_UNSIGNED_SHORT
            elif lastBitCount == 32:
                glType = GL_IMAGE_FORMAT.GL_UNSIGNED_INT
            elif lastBitCount == 4:
                if numComponents == 4:
                    glType = GL_IMAGE_FORMAT.GL_UNSIGNED_SHORT_4_4_4_4_REV
                else:
                    needsConversion = True
        if not glFormat or numComponents * lastBitCount != pixelFormat.dwRGBBitCount:
            needsConversion = True

        if needsConversion:
            convert, glInternalFormat, glFormat, glType = buildConverter(byteCount, bitmasks=namedBitmasks)
        else:
            convert = None

    if not glInternalFormat:
        glInternalFormatName = "_".join(glInternalFormatName)
        glInternalFormat = GL_IMAGE_FORMAT[
            glInternalFormatName] if glInternalFormatName in GL_IMAGE_FORMAT.__members__ else GL_IMAGE_FORMAT.GL_RGBA

    if compressed:
        return CompressedGLTextureFormat(glRequirements, glInternalFormat)
    else:
        return UncompressedGLTextureFormat(glRequirements, glInternalFormat, glFormat, glType, convert)


def sizeFromFormat(dxgiFormat, width, height):
    blockCompressed = False
    if dxgiFormat in {DXGI_FORMAT.DXGI_FORMAT_BC1_TYPELESS,
                      DXGI_FORMAT.DXGI_FORMAT_BC1_UNORM,
                      DXGI_FORMAT.DXGI_FORMAT_BC1_UNORM_SRGB,
                      DXGI_FORMAT.DXGI_FORMAT_BC4_TYPELESS,
                      DXGI_FORMAT.DXGI_FORMAT_BC4_UNORM,
                      DXGI_FORMAT.DXGI_FORMAT_BC4_SNORM}:
        blockCompressed = True
        blockSize = 8
    elif dxgiFormat in {DXGI_FORMAT.DXGI_FORMAT_BC2_TYPELESS,
                        DXGI_FORMAT.DXGI_FORMAT_BC2_UNORM,
                        DXGI_FORMAT.DXGI_FORMAT_BC2_UNORM_SRGB,
                        DXGI_FORMAT.DXGI_FORMAT_BC3_TYPELESS,
                        DXGI_FORMAT.DXGI_FORMAT_BC3_UNORM,
                        DXGI_FORMAT.DXGI_FORMAT_BC3_UNORM_SRGB,
                        DXGI_FORMAT.DXGI_FORMAT_BC5_TYPELESS,
                        DXGI_FORMAT.DXGI_FORMAT_BC5_UNORM,
                        DXGI_FORMAT.DXGI_FORMAT_BC5_SNORM,
                        DXGI_FORMAT.DXGI_FORMAT_BC6H_TYPELESS,
                        DXGI_FORMAT.DXGI_FORMAT_BC6H_UF16,
                        DXGI_FORMAT.DXGI_FORMAT_BC6H_SF16,
                        DXGI_FORMAT.DXGI_FORMAT_BC7_TYPELESS,
                        DXGI_FORMAT.DXGI_FORMAT_BC7_UNORM,
                        DXGI_FORMAT.DXGI_FORMAT_BC7_UNORM_SRGB}:
        blockCompressed = True
        blockSize = 16

    if blockCompressed:
        return max(1, ((width + 3) // 4)) * max(1, ((height + 3) // 4)) * blockSize

    if dxgiFormat <= DXGI_FORMAT.DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
        name = dxgiFormat.name + "_"
        count = 0
        currentNum = ""
        for char in name:
            if char.isdecimal():
                currentNum += char
            elif currentNum != "":
                count += int(currentNum)
                currentNum = ""
        pixelSize = (count + 7) // 8
        return width * height * pixelSize
    pass
