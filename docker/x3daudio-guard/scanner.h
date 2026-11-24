// Locates the inlined memcpy_s that copies audio data while a Creation Engine
// game (Skyrim SE/AE, Fallout 4) finishes opening a sound. When the data size
// recorded for a sound is larger than the buffer allocated for it, memcpy_s
// reports ERANGE through _invalid_parameter_noinfo and the CRT ends the
// process.
//
// The scanner needs no per-game or per-version table. It requires, in one
// short span of code:
//   1. the WMA format check: mov eax,0x161 ; cmp ax,[base+0x20]
//   2. the copy size moved into r8d from a callee-saved register
//   3. memcpy_s' bounds check: cmp rax,r8 ; jae copy
//      followed by errno=ERANGE through the imported _errno, a call through the
//      imported _invalid_parameter_noinfo, and the copy call itself.
// Imports are identified by name from the image's import table. Offsets of
// the object's fields are decoded from the matched instructions.
//
// Portable C++ (no Windows headers) so it can also be tested on Linux against
// an image mapped by RVA.
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace audioguard {

struct Site {
  std::uint32_t anchorRva = 0;  // mov eax,0x161
  std::uint32_t checkRva = 0;   // cmp rax,r8 ; jae copy (5 bytes, patched)
  std::uint32_t copyRva = 0;    // call memcpy (jae target)
  std::uint8_t baseReg = 0;     // register holding the audio data source object
  std::uint8_t countReg = 0;    // 32-bit register holding the copy size
  // Object field offsets decoded from the code; -1 when not recognised.
  std::int32_t capacityOffset = -1;     // destination buffer capacity
  std::int32_t destinationOffset = -1;  // destination buffer pointer
  std::int32_t readOffset = -1;         // bytes read for this sound
  std::int32_t chunkOffset = -1;        // RIFF data chunk size
  std::int32_t dataOffset = -1;         // offset of the data in the read buffer
  std::int32_t sizeOffset = -1;         // audio size the game records afterwards
};

struct ScanResult {
  bool validImage = false;
  bool hasImports = false;  // both _errno and _invalid_parameter_noinfo imported
  std::vector<Site> sites;
};

inline const char* registerName32(std::uint8_t reg)
{
  static const char* names[] = {"eax", "ecx", "edx", "ebx", "esp", "ebp", "esi", "edi"};
  return reg < 8 ? names[reg] : "?";
}

inline const char* registerName64(std::uint8_t reg)
{
  static const char* names[] = {"rax", "rcx", "rdx", "rbx", "rsp", "rbp", "rsi", "rdi"};
  return reg < 8 ? names[reg] : "?";
}

namespace detail {

template <class T>
inline T read(const std::uint8_t* p)
{
  T value;
  std::memcpy(&value, p, sizeof(value));
  return value;
}

// Registers the trampoline can read and write without a REX prefix and that
// the surrounding code does not use for the copy arguments.
inline bool usableRegister(std::uint8_t reg)
{
  return reg == 3 || reg == 5 || reg == 6 || reg == 7;
}

// REX.W: a 64-bit operand prefix. (A bare 0x40-0x47 is far more often the
// displacement byte of the previous instruction, e.g. [rsp+40h].)
inline bool isRex(std::uint8_t b) { return b >= 0x48 && b <= 0x4f; }

// Finds `[opcode...] modrm disp` with mod 01/10, the given reg field and
// r/m == base, between [from, to). Returns the decoded displacement of the
// last (or first) match, and its position through `at`.
inline bool findMemoryOperand(const std::uint8_t* code, std::size_t from,
                              std::size_t to, const std::uint8_t* opcode,
                              std::size_t opcodeSize, std::uint8_t reg,
                              std::uint8_t base, bool last, std::int32_t& disp,
                              std::size_t* at = nullptr)
{
  bool found = false;
  for (std::size_t i = from; i + opcodeSize + 1 <= to; ++i) {
    if (std::memcmp(code + i, opcode, opcodeSize) != 0)
      continue;
    // A bare opcode match inside a longer REX-prefixed instruction is a
    // different instruction.
    if (opcode[0] != 0x48 && i > 0 && isRex(code[i - 1]))
      continue;
    const std::uint8_t modrm = code[i + opcodeSize];
    const std::uint8_t mod = modrm >> 6;
    if (((modrm >> 3) & 7) != reg || (modrm & 7) != base || (mod != 1 && mod != 2))
      continue;
    const std::size_t dispAt = i + opcodeSize + 1;
    const std::size_t dispSize = mod == 1 ? 1 : 4;
    if (dispAt + dispSize > to)
      continue;
    disp = mod == 1 ? static_cast<std::int8_t>(code[dispAt])
                    : read<std::int32_t>(code + dispAt);
    if (at) *at = i;
    found = true;
    if (!last)
      return true;
  }
  return found;
}

}  // namespace detail

inline ScanResult scan(const std::uint8_t* image, std::size_t imageSize)
{
  using detail::read;
  ScanResult result;
  auto inImage = [&](std::uint64_t rva, std::uint64_t size) {
    return rva <= imageSize && size <= imageSize - rva;
  };
  if (!inImage(0, 0x40) || image[0] != 'M' || image[1] != 'Z')
    return result;
  const std::uint32_t nt = read<std::uint32_t>(image + 0x3c);
  if (!inImage(nt, 24) || std::memcmp(image + nt, "PE\0\0", 4) != 0 ||
      read<std::uint16_t>(image + nt + 4) != 0x8664)
    return result;
  const std::uint16_t sectionCount = read<std::uint16_t>(image + nt + 6);
  const std::uint16_t optionalSize = read<std::uint16_t>(image + nt + 20);
  const std::uint32_t optional = nt + 24;
  if (optionalSize < 112 + 16 || !inImage(optional, optionalSize) ||
      read<std::uint16_t>(image + optional) != 0x20b)
    return result;
  const std::uint32_t sections = optional + optionalSize;
  if (!inImage(sections, std::uint64_t(sectionCount) * 40))
    return result;
  result.validImage = true;

  // Import address table slots of the two CRT functions, by name.
  std::uint32_t errnoSlot = 0, invalidParameterSlot = 0;
  const std::uint32_t importRva = read<std::uint32_t>(image + optional + 112 + 8);
  for (std::uint32_t d = importRva; importRva && inImage(d, 20); d += 20) {
    const std::uint32_t names = read<std::uint32_t>(image + d);
    const std::uint32_t slots = read<std::uint32_t>(image + d + 16);
    if (!names && !slots)
      break;
    if (!names)
      continue;  // no lookup table: names are not recoverable after binding
    for (std::uint32_t i = 0; inImage(names + i * 8ull, 8); ++i) {
      const std::uint64_t thunk = read<std::uint64_t>(image + names + i * 8ull);
      if (!thunk)
        break;
      if (thunk >> 63 || !inImage(thunk + 2, 32))
        continue;
      const char* name = reinterpret_cast<const char*>(image + thunk + 2);
      const std::size_t room = imageSize - (thunk + 2);
      if (std::strncmp(name, "_errno", room) == 0)
        errnoSlot = slots + i * 8;
      else if (std::strncmp(name, "_invalid_parameter_noinfo", room) == 0)
        invalidParameterSlot = slots + i * 8;
    }
  }
  result.hasImports = errnoSlot && invalidParameterSlot;
  if (!result.hasImports)
    return result;

  for (std::uint16_t s = 0; s < sectionCount; ++s) {
    const std::uint8_t* header = image + sections + s * 40;
    const std::uint32_t size = read<std::uint32_t>(header + 8);
    const std::uint32_t start = read<std::uint32_t>(header + 12);
    const std::uint32_t flags = read<std::uint32_t>(header + 36);
    if (!(flags & 0x20000000u) || !inImage(start, size))
      continue;
    const std::uint32_t end = start + size;
    for (std::uint32_t p = start; p + 30 <= end; ++p) {
      const std::uint8_t* c = image + p;
      // cmp rax,r8 (either encoding) ; jae +0x14
      const bool compare = (c[0] == 0x49 && c[1] == 0x3b && c[2] == 0xc0) ||
                           (c[0] == 0x4c && c[1] == 0x39 && c[2] == 0xc0);
      if (!compare || c[3] != 0x73 || c[4] != 0x14)
        continue;
      static const std::uint8_t erange[] = {0xc7, 0x00, 0x22, 0x00, 0x00, 0x00};
      if (c[5] != 0xff || c[6] != 0x15 || std::memcmp(c + 11, erange, 6) != 0 ||
          c[17] != 0xff || c[18] != 0x15 || c[23] != 0xeb || c[24] != 0x05 ||
          c[25] != 0xe8)
        continue;
      const std::uint64_t slot1 = p + 11 + std::int64_t(read<std::int32_t>(c + 7));
      const std::uint64_t slot2 = p + 23 + std::int64_t(read<std::int32_t>(c + 19));
      if (slot1 != errnoSlot || slot2 != invalidParameterSlot)
        continue;

      Site site;
      site.checkRva = p;
      site.copyRva = p + 25;

      // WMA format check on the same object, closest one before the copy.
      const std::uint32_t windowStart = p - start > 0x180 ? p - 0x180 : start;
      bool anchored = false;
      for (std::uint32_t a = p; a-- > windowStart && !anchored;) {
        if (image[a] != 0xb8 || read<std::uint32_t>(image + a + 1) != 0x161)
          continue;
        for (std::uint32_t k = a + 5; k + 4 <= p && k <= a + 21; ++k) {
          const std::uint8_t modrm = image[k + 2];
          if (image[k] == 0x66 && image[k + 1] == 0x3b && (modrm & 0xf8) == 0x40 &&
              image[k + 3] == 0x20 && detail::usableRegister(modrm & 7)) {
            site.anchorRva = a;
            site.baseReg = modrm & 7;
            anchored = true;
            break;
          }
        }
      }
      if (!anchored)
        continue;

      // mov r8d, count (the last one before the check)
      bool counted = false;
      for (std::uint32_t k = site.anchorRva; k + 3 <= p; ++k) {
        const std::uint8_t modrm = image[k + 2];
        if (image[k] == 0x44 && image[k + 1] == 0x8b && (modrm & 0xf8) == 0xc0 &&
            detail::usableRegister(modrm & 7) && (modrm & 7) != site.baseReg) {
          site.countReg = modrm & 7;
          counted = true;
        }
      }
      if (!counted)
        continue;

      const std::size_t from = site.anchorRva;
      static const std::uint8_t mov32[] = {0x8b};
      static const std::uint8_t mov64[] = {0x48, 0x8b};
      static const std::uint8_t cmpMem[] = {0x39};
      static const std::uint8_t store[] = {0x89};
      // mov eax,[base+capacity] and mov rcx,[base+destination] are required.
      if (!detail::findMemoryOperand(image, from, p, mov32, 1, 0, site.baseReg, true,
                                     site.capacityOffset) ||
          !detail::findMemoryOperand(image, from, p, mov64, 2, 1, site.baseReg, true,
                                     site.destinationOffset))
        continue;
      // The rest only enrich the diagnostics.
      detail::findMemoryOperand(image, from, p, mov32, 1, site.countReg, site.baseReg,
                                true, site.readOffset);
      detail::findMemoryOperand(image, from, p, cmpMem, 1, site.countReg, site.baseReg,
                                true, site.chunkOffset);
      for (std::uint32_t k = from; k + 2 <= p; ++k) {
        const std::uint8_t modrm = image[k + 1];
        if (image[k] == 0x2b && (k == 0 || !detail::isRex(image[k - 1])) &&
            (modrm >> 6) == 3 && ((modrm >> 3) & 7) == site.countReg) {
          std::int32_t disp;
          if (detail::findMemoryOperand(image, from, k, mov32, 1, modrm & 7,
                                        site.baseReg, true, disp))
            site.dataOffset = disp;
        }
      }
      std::int32_t disp;
      if (detail::findMemoryOperand(image, site.copyRva + 5,
                                    std::min<std::size_t>(end, site.copyRva + 0x25),
                                    store, 1, site.countReg, site.baseReg, false, disp))
        site.sizeOffset = disp;
      result.sites.push_back(site);
    }
  }
  return result;
}

}  // namespace audioguard
