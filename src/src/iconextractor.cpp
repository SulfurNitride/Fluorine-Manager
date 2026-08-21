#include "iconextractor.h"

#include <QFile>
#include <cstring>

// Minimal PE resource parser to extract icons without external dependencies.
// Supports both PE32 (32-bit) and PE32+ (64-bit) executables.

namespace {

// Little-endian reads.
inline uint16_t r16(const char* p) { uint16_t v; memcpy(&v, p, 2); return v; }
inline uint32_t r32(const char* p) { uint32_t v; memcpy(&v, p, 4); return v; }

// PE resource types.
constexpr uint32_t RT_ICON       = 3;
constexpr uint32_t RT_GROUP_ICON = 14;

/// Resolve an RVA to a file offset using the section table.
int64_t rvaToOffset(uint32_t rva, const char* sections, int numSections)
{
  for (int i = 0; i < numSections; ++i) {
    const char* sec = sections + i * 40;
    uint32_t const va   = r32(sec + 12);
    uint32_t const rawSz = r32(sec + 16);
    uint32_t const rawOff = r32(sec + 20);
    if (rva >= va && rva < va + rawSz)
      return rawOff + (rva - va);
  }
  return -1;
}

/// Represents one entry in the resource directory.
struct ResDirEntry {
  uint32_t nameOrId;
  uint32_t offsetOrData;
  bool isDir;
  bool isNamedEntry;
};

/// Parse resource directory entries.
QVector<ResDirEntry> parseResDir(const char* base, uint32_t dirOffset)
{
  QVector<ResDirEntry> entries;
  const char* dir = base + dirOffset;
  uint16_t const numNamed  = r16(dir + 12);
  uint16_t const numId     = r16(dir + 14);
  int const count = numNamed + numId;

  for (int i = 0; i < count; ++i) {
    const char* e = dir + 16 + i * 8;
    ResDirEntry re;
    re.nameOrId    = r32(e);
    re.offsetOrData = r32(e + 4);
    re.isDir        = (re.offsetOrData & 0x80000000u) != 0;
    re.isNamedEntry = (re.nameOrId & 0x80000000u) != 0;
    re.offsetOrData &= 0x7FFFFFFFu;
    entries.append(re);
  }
  return entries;
}

/// Get the data entry (leaf node) bytes.
QByteArray getResourceData(const char* resBase, uint32_t dataEntryOffset,
                           const char* fileData, int64_t fileSize,
                           const char* sections, int numSections)
{
  const char* de = resBase + dataEntryOffset;
  uint32_t const dataRva  = r32(de);
  uint32_t const dataSize = r32(de + 4);

  int64_t const off = rvaToOffset(dataRva, sections, numSections);
  if (off < 0 || off + dataSize > fileSize)
    return {};
  return {fileData + off, static_cast<int>(dataSize)};
}

/// Build an ICO file from a GRPICONDIR and RT_ICON resources.
QByteArray buildIco(const QByteArray& grpData,
                    const QVector<ResDirEntry>& iconTypeEntries,
                    const char* resBase, const char* fileData,
                    int64_t fileSize, const char* sections, int numSections)
{
  if (grpData.size() < 6)
    return {};

  const int count = r16(grpData.constData() + 4);
  if (grpData.size() < 6 + count * 14)
    return {};

  struct Entry { QByteArray header; QByteArray data; };
  QVector<Entry> entries;

  for (int i = 0; i < count; ++i) {
    int const grpOff = 6 + i * 14;
    uint16_t const iconId = r16(grpData.constData() + grpOff + 12);

    // Find RT_ICON with this ID.
    for (const auto& ie : iconTypeEntries) {
      if (ie.nameOrId != iconId || !ie.isDir)
        continue;

      auto langEntries = parseResDir(resBase, ie.offsetOrData);
      if (langEntries.isEmpty())
        continue;

      const auto& langEntry = langEntries.first();
      if (langEntry.isDir)
        continue;

      QByteArray const imgData = getResourceData(
          resBase, langEntry.offsetOrData, fileData, fileSize, sections, numSections);
      if (imgData.isEmpty())
        continue;

      QByteArray const hdr(grpData.constData() + grpOff, 8);
      entries.append({.header=hdr, .data=imgData});
      break;
    }
  }

  if (entries.isEmpty())
    return {};

  // Build ICO file.
  QByteArray ico;
  uint16_t entryCount = static_cast<uint16_t>(entries.size());
  int const headerSize = 6 + entryCount * 16;
  ico.reserve(headerSize + count * 4096);

  // ICONDIR header.
  uint16_t zero = 0;
  uint16_t one = 1;
  ico.append(reinterpret_cast<const char*>(&zero), 2);
  ico.append(reinterpret_cast<const char*>(&one), 2);
  ico.append(reinterpret_cast<const char*>(&entryCount), 2);

  // ICONDIRENTRY records.
  uint32_t dataOffset = static_cast<uint32_t>(headerSize);
  for (const auto& e : entries) {
    ico.append(e.header);  // 8 bytes
    uint32_t sz = static_cast<uint32_t>(e.data.size());
    ico.append(reinterpret_cast<const char*>(&sz), 4);
    ico.append(reinterpret_cast<const char*>(&dataOffset), 4);
    dataOffset += sz;
  }

  // Image data.
  for (const auto& e : entries)
    ico.append(e.data);

  return ico;
}

/// Try to extract icons from a PE file (works for both PE32 and PE32+).
QByteArray tryExtractIcons(const QByteArray& fileData)
{
  const int64_t sz = fileData.size();
  const char* d    = fileData.constData();

  if (sz < 64 || d[0] != 'M' || d[1] != 'Z')
    return {};

  int32_t const peOffset = r32(d + 60);
  if (peOffset + 24 > sz)
    return {};
  if (memcmp(d + peOffset, "PE\0\0", 4) != 0)
    return {};

  const char* coff = d + peOffset + 4;
  uint16_t const numSections = r16(coff + 2);
  uint16_t const optHdrSize  = r16(coff + 16);

  const char* optHdr = coff + 20;
  uint16_t const magic = r16(optHdr);

  // Determine resource directory RVA.
  uint32_t resRva = 0;
  uint32_t resSize = 0;
  if (magic == 0x10b) {
    // PE32: data directories start at offset 96 in optional header.
    // Resource table is the 3rd entry (index 2), each entry is 8 bytes.
    if (optHdrSize >= 96 + 3 * 8) {
      resRva  = r32(optHdr + 96 + 2 * 8);
      resSize = r32(optHdr + 96 + 2 * 8 + 4);
    }
  } else if (magic == 0x20b) {
    // PE32+: data directories start at offset 112.
    if (optHdrSize >= 112 + 3 * 8) {
      resRva  = r32(optHdr + 112 + 2 * 8);
      resSize = r32(optHdr + 112 + 2 * 8 + 4);
    }
  } else {
    return {};
  }

  if (resRva == 0 || resSize == 0)
    return {};

  const char* sections = optHdr + optHdrSize;
  int64_t const resFileOff = rvaToOffset(resRva, sections, numSections);
  if (resFileOff < 0 || resFileOff + resSize > sz)
    return {};

  const char* resBase = d + resFileOff;

  // Parse root directory — find RT_GROUP_ICON (14) and RT_ICON (3).
  auto rootEntries = parseResDir(resBase, 0);

  const ResDirEntry* groupIconType = nullptr;
  const ResDirEntry* iconType      = nullptr;
  for (const auto& e : rootEntries) {
    if (e.nameOrId == RT_GROUP_ICON && e.isDir)
      groupIconType = &e;
    if (e.nameOrId == RT_ICON && e.isDir)
      iconType = &e;
  }

  if (!groupIconType || !iconType)
    return {};

  auto groupEntries = parseResDir(resBase, groupIconType->offsetOrData);
  auto iconEntries  = parseResDir(resBase, iconType->offsetOrData);

  // Iterate groups, pick the one with the largest total area.
  QByteArray bestIco;
  int bestArea = -1;

  for (const auto& ge : groupEntries) {
    if (!ge.isDir)
      continue;

    auto langEntries = parseResDir(resBase, ge.offsetOrData);
    if (langEntries.isEmpty())
      continue;

    const auto& langEntry = langEntries.first();
    if (langEntry.isDir)
      continue;

    QByteArray grpData = getResourceData(
        resBase, langEntry.offsetOrData, d, sz, sections, numSections);
    if (grpData.size() < 6)
      continue;

    int const count = r16(grpData.constData() + 4);
    if (grpData.size() < 6 + count * 14)
      continue;

    int totalArea = 0;
    for (int i = 0; i < count; ++i) {
      int const off = 6 + i * 14;
      int const w = (uint8_t)grpData[off]   == 0 ? 256 : (uint8_t)grpData[off];
      int const h = (uint8_t)grpData[off+1] == 0 ? 256 : (uint8_t)grpData[off+1];
      totalArea += w * h;
    }

    QByteArray const ico = buildIco(grpData, iconEntries, resBase, d, sz, sections, numSections);
    if (!ico.isEmpty() && totalArea > bestArea) {
      bestArea = totalArea;
      bestIco  = ico;
    }
  }

  return bestIco;
}

}  // namespace

QByteArray extractExeIcon(const QString& exePath)
{
  QFile f(exePath);
  if (!f.open(QIODevice::ReadOnly))
    return {};

  const qint64 size = f.size();
  if (size <= 0) {
    return {};
  }

  uchar* const mapped = f.map(0, size);
  if (mapped == nullptr) {
    return {};
  }

  // The PE parser only copies the small resource records and icon payloads it
  // needs. A raw view avoids reading large game executables into a temporary
  // QByteArray merely to inspect their resource table.
  const QByteArray view = QByteArray::fromRawData(
      reinterpret_cast<const char*>(mapped), static_cast<qsizetype>(size));
  QByteArray icon = tryExtractIcons(view);
  f.unmap(mapped);
  return icon;
}
