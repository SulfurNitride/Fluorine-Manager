cmake_minimum_required(VERSION 3.16)

# Build 7z.so from the official 7zip source code.
# This produces a shared library containing all archive format handlers,
# compression codecs, and crypto modules that the MO2 archive library
# loads via dlopen() at runtime.

if(WIN32)
  # On Windows, 7zip comes from vcpkg; this target is Linux-only.
  return()
endif()

include(FetchContent)
FetchContent_Declare(
  7zip_src
  GIT_REPOSITORY https://github.com/ip7z/7zip.git
  GIT_TAG        24.09
  GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(7zip_src)

set(SZ "${7zip_src_SOURCE_DIR}")

# ---------------------------------------------------------------------------
# C sources (compression, hashing, crypto primitives)
# ---------------------------------------------------------------------------
set(7Z_C_SOURCES
  ${SZ}/C/7zBuf2.c
  ${SZ}/C/7zCrc.c
  ${SZ}/C/7zCrcOpt.c
  ${SZ}/C/7zStream.c
  ${SZ}/C/Aes.c
  ${SZ}/C/AesOpt.c
  ${SZ}/C/Alloc.c
  ${SZ}/C/Bcj2.c
  ${SZ}/C/Bcj2Enc.c
  ${SZ}/C/Blake2s.c
  ${SZ}/C/Bra.c
  ${SZ}/C/Bra86.c
  ${SZ}/C/BraIA64.c
  ${SZ}/C/BwtSort.c
  ${SZ}/C/CpuArch.c
  ${SZ}/C/Delta.c
  ${SZ}/C/HuffEnc.c
  ${SZ}/C/LzFind.c
  ${SZ}/C/LzFindMt.c
  ${SZ}/C/LzFindOpt.c
  ${SZ}/C/Lzma2Dec.c
  ${SZ}/C/Lzma2DecMt.c
  ${SZ}/C/Lzma2Enc.c
  ${SZ}/C/LzmaDec.c
  ${SZ}/C/LzmaEnc.c
  ${SZ}/C/Md5.c
  ${SZ}/C/MtCoder.c
  ${SZ}/C/MtDec.c
  ${SZ}/C/Ppmd7.c
  ${SZ}/C/Ppmd7Dec.c
  ${SZ}/C/Ppmd7aDec.c
  ${SZ}/C/Ppmd7Enc.c
  ${SZ}/C/Ppmd8.c
  ${SZ}/C/Ppmd8Dec.c
  ${SZ}/C/Ppmd8Enc.c
  ${SZ}/C/Sha1.c
  ${SZ}/C/Sha1Opt.c
  ${SZ}/C/Sha256.c
  ${SZ}/C/Sha256Opt.c
  ${SZ}/C/Sha3.c
  ${SZ}/C/Sha512.c
  ${SZ}/C/Sha512Opt.c
  ${SZ}/C/Sort.c
  ${SZ}/C/SwapBytes.c
  ${SZ}/C/Threads.c
  ${SZ}/C/Xxh64.c
  ${SZ}/C/Xz.c
  ${SZ}/C/XzCrc64.c
  ${SZ}/C/XzCrc64Opt.c
  ${SZ}/C/XzDec.c
  ${SZ}/C/XzEnc.c
  ${SZ}/C/XzIn.c
  ${SZ}/C/ZstdDec.c
)

# ---------------------------------------------------------------------------
# C++ Common sources
# ---------------------------------------------------------------------------
set(7Z_CPP_COMMON
  ${SZ}/CPP/Common/CRC.cpp
  ${SZ}/CPP/Common/CrcReg.cpp
  ${SZ}/CPP/Common/DynLimBuf.cpp
  ${SZ}/CPP/Common/IntToString.cpp
  ${SZ}/CPP/Common/LzFindPrepare.cpp
  ${SZ}/CPP/Common/Md5Reg.cpp
  ${SZ}/CPP/Common/MyMap.cpp
  ${SZ}/CPP/Common/MyString.cpp
  ${SZ}/CPP/Common/MyVector.cpp
  ${SZ}/CPP/Common/MyWindows.cpp
  ${SZ}/CPP/Common/MyXml.cpp
  ${SZ}/CPP/Common/NewHandler.cpp
  ${SZ}/CPP/Common/Sha1Prepare.cpp
  ${SZ}/CPP/Common/Sha1Reg.cpp
  ${SZ}/CPP/Common/Sha256Prepare.cpp
  ${SZ}/CPP/Common/Sha256Reg.cpp
  ${SZ}/CPP/Common/Sha3Reg.cpp
  ${SZ}/CPP/Common/Sha512Prepare.cpp
  ${SZ}/CPP/Common/Sha512Reg.cpp
  ${SZ}/CPP/Common/StringConvert.cpp
  ${SZ}/CPP/Common/StringToInt.cpp
  ${SZ}/CPP/Common/UTFConvert.cpp
  ${SZ}/CPP/Common/Wildcard.cpp
  ${SZ}/CPP/Common/Xxh64Reg.cpp
  ${SZ}/CPP/Common/XzCrc64Init.cpp
  ${SZ}/CPP/Common/XzCrc64Reg.cpp
)

# ---------------------------------------------------------------------------
# C++ Windows compatibility layer (POSIX implementations)
# ---------------------------------------------------------------------------
set(7Z_CPP_WINDOWS
  ${SZ}/CPP/Windows/FileDir.cpp
  ${SZ}/CPP/Windows/FileFind.cpp
  ${SZ}/CPP/Windows/FileIO.cpp
  ${SZ}/CPP/Windows/FileName.cpp
  ${SZ}/CPP/Windows/PropVariant.cpp
  ${SZ}/CPP/Windows/PropVariantConv.cpp
  ${SZ}/CPP/Windows/PropVariantUtils.cpp
  ${SZ}/CPP/Windows/Synchronization.cpp
  ${SZ}/CPP/Windows/System.cpp
  ${SZ}/CPP/Windows/TimeUtils.cpp
)

# ---------------------------------------------------------------------------
# 7zip Common (stream, coder infrastructure)
# ---------------------------------------------------------------------------
set(7Z_7ZIP_COMMON
  ${SZ}/CPP/7zip/Common/CreateCoder.cpp
  ${SZ}/CPP/7zip/Common/CWrappers.cpp
  ${SZ}/CPP/7zip/Common/InBuffer.cpp
  ${SZ}/CPP/7zip/Common/InOutTempBuffer.cpp
  ${SZ}/CPP/7zip/Common/FilterCoder.cpp
  ${SZ}/CPP/7zip/Common/LimitedStreams.cpp
  ${SZ}/CPP/7zip/Common/LockedStream.cpp
  ${SZ}/CPP/7zip/Common/MemBlocks.cpp
  ${SZ}/CPP/7zip/Common/MethodId.cpp
  ${SZ}/CPP/7zip/Common/MethodProps.cpp
  ${SZ}/CPP/7zip/Common/OffsetStream.cpp
  ${SZ}/CPP/7zip/Common/OutBuffer.cpp
  ${SZ}/CPP/7zip/Common/OutMemStream.cpp
  ${SZ}/CPP/7zip/Common/ProgressMt.cpp
  ${SZ}/CPP/7zip/Common/ProgressUtils.cpp
  ${SZ}/CPP/7zip/Common/PropId.cpp
  ${SZ}/CPP/7zip/Common/StreamBinder.cpp
  ${SZ}/CPP/7zip/Common/StreamObjects.cpp
  ${SZ}/CPP/7zip/Common/StreamUtils.cpp
  ${SZ}/CPP/7zip/Common/UniqBlocks.cpp
  ${SZ}/CPP/7zip/Common/VirtThread.cpp
)

# ---------------------------------------------------------------------------
# Archive format handlers (standalone)
# ---------------------------------------------------------------------------
set(7Z_ARCHIVE_HANDLERS
  ${SZ}/CPP/7zip/Archive/ApfsHandler.cpp
  ${SZ}/CPP/7zip/Archive/ApmHandler.cpp
  ${SZ}/CPP/7zip/Archive/ArHandler.cpp
  ${SZ}/CPP/7zip/Archive/ArjHandler.cpp
  ${SZ}/CPP/7zip/Archive/Base64Handler.cpp
  ${SZ}/CPP/7zip/Archive/Bz2Handler.cpp
  ${SZ}/CPP/7zip/Archive/ComHandler.cpp
  ${SZ}/CPP/7zip/Archive/CpioHandler.cpp
  ${SZ}/CPP/7zip/Archive/CramfsHandler.cpp
  ${SZ}/CPP/7zip/Archive/DeflateProps.cpp
  ${SZ}/CPP/7zip/Archive/DmgHandler.cpp
  ${SZ}/CPP/7zip/Archive/ElfHandler.cpp
  ${SZ}/CPP/7zip/Archive/ExtHandler.cpp
  ${SZ}/CPP/7zip/Archive/FatHandler.cpp
  ${SZ}/CPP/7zip/Archive/FlvHandler.cpp
  ${SZ}/CPP/7zip/Archive/GptHandler.cpp
  ${SZ}/CPP/7zip/Archive/GzHandler.cpp
  ${SZ}/CPP/7zip/Archive/HandlerCont.cpp
  ${SZ}/CPP/7zip/Archive/HfsHandler.cpp
  ${SZ}/CPP/7zip/Archive/IhexHandler.cpp
  ${SZ}/CPP/7zip/Archive/LpHandler.cpp
  ${SZ}/CPP/7zip/Archive/LzhHandler.cpp
  ${SZ}/CPP/7zip/Archive/LzmaHandler.cpp
  ${SZ}/CPP/7zip/Archive/MachoHandler.cpp
  ${SZ}/CPP/7zip/Archive/MbrHandler.cpp
  ${SZ}/CPP/7zip/Archive/MslzHandler.cpp
  ${SZ}/CPP/7zip/Archive/MubHandler.cpp
  ${SZ}/CPP/7zip/Archive/NtfsHandler.cpp
  ${SZ}/CPP/7zip/Archive/PeHandler.cpp
  ${SZ}/CPP/7zip/Archive/PpmdHandler.cpp
  ${SZ}/CPP/7zip/Archive/QcowHandler.cpp
  ${SZ}/CPP/7zip/Archive/RpmHandler.cpp
  ${SZ}/CPP/7zip/Archive/SparseHandler.cpp
  ${SZ}/CPP/7zip/Archive/SplitHandler.cpp
  ${SZ}/CPP/7zip/Archive/SquashfsHandler.cpp
  ${SZ}/CPP/7zip/Archive/SwfHandler.cpp
  ${SZ}/CPP/7zip/Archive/UefiHandler.cpp
  ${SZ}/CPP/7zip/Archive/VdiHandler.cpp
  ${SZ}/CPP/7zip/Archive/VhdHandler.cpp
  ${SZ}/CPP/7zip/Archive/VhdxHandler.cpp
  ${SZ}/CPP/7zip/Archive/VmdkHandler.cpp
  ${SZ}/CPP/7zip/Archive/XarHandler.cpp
  ${SZ}/CPP/7zip/Archive/XzHandler.cpp
  ${SZ}/CPP/7zip/Archive/ZHandler.cpp
  ${SZ}/CPP/7zip/Archive/ZstdHandler.cpp
)

# ---------------------------------------------------------------------------
# Archive Common
# ---------------------------------------------------------------------------
set(7Z_ARCHIVE_COMMON
  ${SZ}/CPP/7zip/Archive/Common/CoderMixer2.cpp
  ${SZ}/CPP/7zip/Archive/Common/DummyOutStream.cpp
  ${SZ}/CPP/7zip/Archive/Common/FindSignature.cpp
  ${SZ}/CPP/7zip/Archive/Common/InStreamWithCRC.cpp
  ${SZ}/CPP/7zip/Archive/Common/ItemNameUtils.cpp
  ${SZ}/CPP/7zip/Archive/Common/MultiStream.cpp
  ${SZ}/CPP/7zip/Archive/Common/OutStreamWithCRC.cpp
  ${SZ}/CPP/7zip/Archive/Common/OutStreamWithSha1.cpp
  ${SZ}/CPP/7zip/Archive/Common/HandlerOut.cpp
  ${SZ}/CPP/7zip/Archive/Common/ParseProperties.cpp
)

# ---------------------------------------------------------------------------
# 7z format
# ---------------------------------------------------------------------------
set(7Z_FORMAT_7Z
  ${SZ}/CPP/7zip/Archive/7z/7zCompressionMode.cpp
  ${SZ}/CPP/7zip/Archive/7z/7zDecode.cpp
  ${SZ}/CPP/7zip/Archive/7z/7zEncode.cpp
  ${SZ}/CPP/7zip/Archive/7z/7zExtract.cpp
  ${SZ}/CPP/7zip/Archive/7z/7zFolderInStream.cpp
  ${SZ}/CPP/7zip/Archive/7z/7zHandler.cpp
  ${SZ}/CPP/7zip/Archive/7z/7zHandlerOut.cpp
  ${SZ}/CPP/7zip/Archive/7z/7zHeader.cpp
  ${SZ}/CPP/7zip/Archive/7z/7zIn.cpp
  ${SZ}/CPP/7zip/Archive/7z/7zOut.cpp
  ${SZ}/CPP/7zip/Archive/7z/7zProperties.cpp
  ${SZ}/CPP/7zip/Archive/7z/7zSpecStream.cpp
  ${SZ}/CPP/7zip/Archive/7z/7zUpdate.cpp
  ${SZ}/CPP/7zip/Archive/7z/7zRegister.cpp
)

# ---------------------------------------------------------------------------
# CAB, CHM, ISO, NSIS, RAR, TAR, UDF, WIM, ZIP formats
# ---------------------------------------------------------------------------
set(7Z_FORMAT_CAB
  ${SZ}/CPP/7zip/Archive/Cab/CabBlockInStream.cpp
  ${SZ}/CPP/7zip/Archive/Cab/CabHandler.cpp
  ${SZ}/CPP/7zip/Archive/Cab/CabHeader.cpp
  ${SZ}/CPP/7zip/Archive/Cab/CabIn.cpp
  ${SZ}/CPP/7zip/Archive/Cab/CabRegister.cpp
)
set(7Z_FORMAT_CHM
  ${SZ}/CPP/7zip/Archive/Chm/ChmHandler.cpp
  ${SZ}/CPP/7zip/Archive/Chm/ChmIn.cpp
)
set(7Z_FORMAT_ISO
  ${SZ}/CPP/7zip/Archive/Iso/IsoHandler.cpp
  ${SZ}/CPP/7zip/Archive/Iso/IsoHeader.cpp
  ${SZ}/CPP/7zip/Archive/Iso/IsoIn.cpp
  ${SZ}/CPP/7zip/Archive/Iso/IsoRegister.cpp
)
set(7Z_FORMAT_NSIS
  ${SZ}/CPP/7zip/Archive/Nsis/NsisDecode.cpp
  ${SZ}/CPP/7zip/Archive/Nsis/NsisHandler.cpp
  ${SZ}/CPP/7zip/Archive/Nsis/NsisIn.cpp
  ${SZ}/CPP/7zip/Archive/Nsis/NsisRegister.cpp
)
set(7Z_FORMAT_RAR
  ${SZ}/CPP/7zip/Archive/Rar/RarHandler.cpp
  ${SZ}/CPP/7zip/Archive/Rar/Rar5Handler.cpp
)
set(7Z_FORMAT_TAR
  ${SZ}/CPP/7zip/Archive/Tar/TarHandler.cpp
  ${SZ}/CPP/7zip/Archive/Tar/TarHandlerOut.cpp
  ${SZ}/CPP/7zip/Archive/Tar/TarHeader.cpp
  ${SZ}/CPP/7zip/Archive/Tar/TarIn.cpp
  ${SZ}/CPP/7zip/Archive/Tar/TarOut.cpp
  ${SZ}/CPP/7zip/Archive/Tar/TarUpdate.cpp
  ${SZ}/CPP/7zip/Archive/Tar/TarRegister.cpp
)
set(7Z_FORMAT_UDF
  ${SZ}/CPP/7zip/Archive/Udf/UdfHandler.cpp
  ${SZ}/CPP/7zip/Archive/Udf/UdfIn.cpp
)
set(7Z_FORMAT_WIM
  ${SZ}/CPP/7zip/Archive/Wim/WimHandler.cpp
  ${SZ}/CPP/7zip/Archive/Wim/WimHandlerOut.cpp
  ${SZ}/CPP/7zip/Archive/Wim/WimIn.cpp
  ${SZ}/CPP/7zip/Archive/Wim/WimRegister.cpp
)
set(7Z_FORMAT_ZIP
  ${SZ}/CPP/7zip/Archive/Zip/ZipAddCommon.cpp
  ${SZ}/CPP/7zip/Archive/Zip/ZipHandler.cpp
  ${SZ}/CPP/7zip/Archive/Zip/ZipHandlerOut.cpp
  ${SZ}/CPP/7zip/Archive/Zip/ZipIn.cpp
  ${SZ}/CPP/7zip/Archive/Zip/ZipItem.cpp
  ${SZ}/CPP/7zip/Archive/Zip/ZipOut.cpp
  ${SZ}/CPP/7zip/Archive/Zip/ZipUpdate.cpp
  ${SZ}/CPP/7zip/Archive/Zip/ZipRegister.cpp
)

# ---------------------------------------------------------------------------
# Compression codecs
# ---------------------------------------------------------------------------
set(7Z_COMPRESS
  ${SZ}/CPP/7zip/Compress/Bcj2Coder.cpp
  ${SZ}/CPP/7zip/Compress/Bcj2Register.cpp
  ${SZ}/CPP/7zip/Compress/BcjCoder.cpp
  ${SZ}/CPP/7zip/Compress/BcjRegister.cpp
  ${SZ}/CPP/7zip/Compress/BitlDecoder.cpp
  ${SZ}/CPP/7zip/Compress/BranchMisc.cpp
  ${SZ}/CPP/7zip/Compress/BranchRegister.cpp
  ${SZ}/CPP/7zip/Compress/ByteSwap.cpp
  ${SZ}/CPP/7zip/Compress/BZip2Crc.cpp
  ${SZ}/CPP/7zip/Compress/BZip2Decoder.cpp
  ${SZ}/CPP/7zip/Compress/BZip2Encoder.cpp
  ${SZ}/CPP/7zip/Compress/BZip2Register.cpp
  ${SZ}/CPP/7zip/Compress/CopyCoder.cpp
  ${SZ}/CPP/7zip/Compress/CopyRegister.cpp
  ${SZ}/CPP/7zip/Compress/Deflate64Register.cpp
  ${SZ}/CPP/7zip/Compress/DeflateDecoder.cpp
  ${SZ}/CPP/7zip/Compress/DeflateEncoder.cpp
  ${SZ}/CPP/7zip/Compress/DeflateRegister.cpp
  ${SZ}/CPP/7zip/Compress/DeltaFilter.cpp
  ${SZ}/CPP/7zip/Compress/ImplodeDecoder.cpp
  ${SZ}/CPP/7zip/Compress/LzfseDecoder.cpp
  ${SZ}/CPP/7zip/Compress/LzhDecoder.cpp
  ${SZ}/CPP/7zip/Compress/Lzma2Decoder.cpp
  ${SZ}/CPP/7zip/Compress/Lzma2Encoder.cpp
  ${SZ}/CPP/7zip/Compress/Lzma2Register.cpp
  ${SZ}/CPP/7zip/Compress/LzmaDecoder.cpp
  ${SZ}/CPP/7zip/Compress/LzmaEncoder.cpp
  ${SZ}/CPP/7zip/Compress/LzmaRegister.cpp
  ${SZ}/CPP/7zip/Compress/LzmsDecoder.cpp
  ${SZ}/CPP/7zip/Compress/LzOutWindow.cpp
  ${SZ}/CPP/7zip/Compress/LzxDecoder.cpp
  ${SZ}/CPP/7zip/Compress/PpmdDecoder.cpp
  ${SZ}/CPP/7zip/Compress/PpmdEncoder.cpp
  ${SZ}/CPP/7zip/Compress/PpmdRegister.cpp
  ${SZ}/CPP/7zip/Compress/PpmdZip.cpp
  ${SZ}/CPP/7zip/Compress/QuantumDecoder.cpp
  ${SZ}/CPP/7zip/Compress/ShrinkDecoder.cpp
  ${SZ}/CPP/7zip/Compress/XpressDecoder.cpp
  ${SZ}/CPP/7zip/Compress/XzDecoder.cpp
  ${SZ}/CPP/7zip/Compress/XzEncoder.cpp
  ${SZ}/CPP/7zip/Compress/ZlibDecoder.cpp
  ${SZ}/CPP/7zip/Compress/ZlibEncoder.cpp
  ${SZ}/CPP/7zip/Compress/ZDecoder.cpp
  ${SZ}/CPP/7zip/Compress/ZstdDecoder.cpp
  # RAR codecs
  ${SZ}/CPP/7zip/Compress/Rar1Decoder.cpp
  ${SZ}/CPP/7zip/Compress/Rar2Decoder.cpp
  ${SZ}/CPP/7zip/Compress/Rar3Decoder.cpp
  ${SZ}/CPP/7zip/Compress/Rar3Vm.cpp
  ${SZ}/CPP/7zip/Compress/Rar5Decoder.cpp
  ${SZ}/CPP/7zip/Compress/RarCodecsRegister.cpp
)

# ---------------------------------------------------------------------------
# Crypto
# ---------------------------------------------------------------------------
set(7Z_CRYPTO
  ${SZ}/CPP/7zip/Crypto/7zAes.cpp
  ${SZ}/CPP/7zip/Crypto/7zAesRegister.cpp
  ${SZ}/CPP/7zip/Crypto/HmacSha1.cpp
  ${SZ}/CPP/7zip/Crypto/HmacSha256.cpp
  ${SZ}/CPP/7zip/Crypto/MyAes.cpp
  ${SZ}/CPP/7zip/Crypto/MyAesReg.cpp
  ${SZ}/CPP/7zip/Crypto/Pbkdf2HmacSha1.cpp
  ${SZ}/CPP/7zip/Crypto/RandGen.cpp
  ${SZ}/CPP/7zip/Crypto/WzAes.cpp
  ${SZ}/CPP/7zip/Crypto/ZipCrypto.cpp
  ${SZ}/CPP/7zip/Crypto/ZipStrong.cpp
  # RAR crypto
  ${SZ}/CPP/7zip/Crypto/Rar20Crypto.cpp
  ${SZ}/CPP/7zip/Crypto/Rar5Aes.cpp
  ${SZ}/CPP/7zip/Crypto/RarAes.cpp
)

# ---------------------------------------------------------------------------
# DLL export entry points (what the archive library dlopen's)
# ---------------------------------------------------------------------------
set(7Z_EXPORTS
  ${SZ}/CPP/7zip/Archive/ArchiveExports.cpp
  ${SZ}/CPP/7zip/Archive/DllExports2.cpp
  ${SZ}/CPP/7zip/Compress/CodecExports.cpp
)

# ---------------------------------------------------------------------------
# Build the shared library
# ---------------------------------------------------------------------------
add_library(7z SHARED
  ${7Z_C_SOURCES}
  ${7Z_CPP_COMMON}
  ${7Z_CPP_WINDOWS}
  ${7Z_7ZIP_COMMON}
  ${7Z_ARCHIVE_HANDLERS}
  ${7Z_ARCHIVE_COMMON}
  ${7Z_FORMAT_7Z}
  ${7Z_FORMAT_CAB}
  ${7Z_FORMAT_CHM}
  ${7Z_FORMAT_ISO}
  ${7Z_FORMAT_NSIS}
  ${7Z_FORMAT_RAR}
  ${7Z_FORMAT_TAR}
  ${7Z_FORMAT_UDF}
  ${7Z_FORMAT_WIM}
  ${7Z_FORMAT_ZIP}
  ${7Z_COMPRESS}
  ${7Z_CRYPTO}
  ${7Z_EXPORTS}
)

# Output as "7z.so" (not "lib7z.so")
set_target_properties(7z PROPERTIES
  PREFIX ""
  OUTPUT_NAME "7z"
  POSITION_INDEPENDENT_CODE ON
)

target_include_directories(7z PRIVATE
  ${SZ}/CPP
  ${SZ}/CPP/Common
  ${SZ}/CPP/Windows
  ${SZ}/CPP/7zip
  ${SZ}/C
)

target_compile_definitions(7z PRIVATE
  -DZ7_EXTERNAL_CODECS
  -DNDEBUG
  -D_REENTRANT
  -D_FILE_OFFSET_BITS=64
  -D_LARGEFILE_SOURCE
)

target_compile_options(7z PRIVATE
  -O2
  -Wno-error
  -Wno-unused-parameter
  -Wno-sign-compare
  -Wno-missing-field-initializers
  $<$<COMPILE_LANGUAGE:CXX>:-Wno-reorder>
)

target_link_libraries(7z PRIVATE pthread dl)

# Install 7z.so next to the organizer binary (in dlls/ subdirectory)
install(TARGETS 7z LIBRARY DESTINATION dlls)
# Also install to the bin root so dlopen can find it easily
install(TARGETS 7z LIBRARY DESTINATION bin)
