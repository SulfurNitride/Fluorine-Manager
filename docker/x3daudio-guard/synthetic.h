// Minimal PE32+ images whose code reproduces the audio copy of Skyrim SE AE
// (SkyrimSE.exe, BSXAudio2DataSrc open path) and Fallout 4. The instruction
// sequences are the games' own encodings, with branch distances preserved, so
// the scanner and trampoline see exactly what they see in the games.
//
// Each image exports one function: void f(void* object, void* buffer).
// It imports _errno and _invalid_parameter_noinfo, and calls memcpy through a
// thunk. The caller fills the import slots and the thunk target before running.
#pragma once

#include <cstdint>
#include <cstring>
#include <vector>

namespace synthetic {

constexpr std::uint32_t kImageSize = 0x3000;
constexpr std::uint32_t kCodeRva = 0x1000;
constexpr std::uint32_t kImportRva = 0x2000;
constexpr std::uint32_t kLookupRva = 0x2100;
constexpr std::uint32_t kSlotRva = 0x2200;  // [0] _errno, [1] _invalid_parameter_noinfo
constexpr std::uint32_t kNameRva = 0x2300;

struct Layout {
  const char* name;
  std::uint32_t tag, dataOffset, chunk, destination, read, capacity, size;
  std::uint32_t thunkRva;  // ff 25 00000000 <abs64 memcpy>
};

// Field offsets inside the data source object, per game.
constexpr Layout kSkyrim = {"Skyrim SE AE", 0x20, 0x60, 0x64, 0x90, 0x98, 0x9c, 0xb0, 0};
constexpr Layout kFallout4 = {"Fallout 4", 0x20, 0x78, 0x7c, 0xa8, 0xb0, 0xb4, 0xc8, 0};

class Builder {
public:
  std::vector<std::uint8_t> code;
  std::vector<std::uint32_t> errnoCalls, invalidCalls, copyCalls;  // rel32 positions

  void bytes(std::initializer_list<int> list)
  {
    for (int b : list) code.push_back(static_cast<std::uint8_t>(b));
  }
  void callSlot(std::vector<std::uint32_t>& sites)
  {
    bytes({0xff, 0x15});
    sites.push_back(static_cast<std::uint32_t>(code.size()));
    bytes({0, 0, 0, 0});
  }
  void callCopy()
  {
    bytes({0xe8});
    copyCalls.push_back(static_cast<std::uint32_t>(code.size()));
    bytes({0, 0, 0, 0});
  }
};

inline void put32(std::vector<std::uint8_t>& image, std::uint32_t at, std::uint32_t v)
{
  std::memcpy(image.data() + at, &v, 4);
}

inline void put16(std::vector<std::uint8_t>& image, std::uint32_t at, std::uint16_t v)
{
  std::memcpy(image.data() + at, &v, 2);
}

// The memcpy_s tail shared by both games:
//   test rcx,rcx ; jne src ; EINVAL ; jmp store ; <unrelated 5 bytes> ;
//   src: test rdx,rdx ; je EINVAL ; cmp rax,r8 ; jae copy ; ERANGE ; jmp store ;
//   copy: call memcpy
inline void memcpySTail(Builder& b, int unrelatedJump)
{
  b.bytes({0x48, 0x85, 0xc9, 0x75, 0x19});
  b.callSlot(b.errnoCalls);
  b.bytes({0xc7, 0x00, 0x16, 0x00, 0x00, 0x00});
  b.callSlot(b.invalidCalls);
  b.bytes({0xeb, 0x28, 0x40, 0x32, 0xed, 0xeb, unrelatedJump});
  b.bytes({0x48, 0x85, 0xd2, 0x74, 0xe2, 0x49, 0x3b, 0xc0, 0x73, 0x14});
  b.callSlot(b.errnoCalls);
  b.bytes({0xc7, 0x00, 0x22, 0x00, 0x00, 0x00});
  b.callSlot(b.invalidCalls);
  b.bytes({0xeb, 0x05});
  b.callCopy();
}

inline Builder skyrimCode()
{
  Builder b;
  b.bytes({0x53, 0x56, 0x57, 0x48, 0x83, 0xec, 0x20});  // push rbx/rsi/rdi ; sub rsp,20h
  b.bytes({0x48, 0x89, 0xcb, 0x48, 0x89, 0xd6});        // rbx=object ; rsi=buffer
  b.bytes({0xb8, 0x61, 0x01, 0x00, 0x00, 0x66, 0x3b, 0x43, 0x20, 0x75, 0x00});
  // SkyrimSE.exe 1.6.1170 +0xcdbeb9
  b.bytes({0x8b, 0x4b, 0x60, 0x8b, 0xbb, 0x98, 0x00, 0x00, 0x00, 0x2b, 0xf9,
           0x39, 0x7b, 0x64, 0x0f, 0x42, 0x7b, 0x64, 0x44, 0x8b, 0xc7,
           0x48, 0x8d, 0x14, 0x31, 0x8b, 0x83, 0x9c, 0x00, 0x00, 0x00,
           0x48, 0x8b, 0x8b, 0x90, 0x00, 0x00, 0x00, 0x85, 0xff, 0x74, 0x41});
  memcpySTail(b, 0x55);
  b.bytes({0x89, 0xbb, 0xb0, 0x00, 0x00, 0x00});         // mov [rbx+0B0h],edi
  b.bytes({0x48, 0x83, 0xc4, 0x20, 0x5f, 0x5e, 0x5b, 0xc3});
  return b;
}

inline Builder fallout4Code()
{
  Builder b;
  b.bytes({0x57, 0x56, 0x41, 0x56, 0x48, 0x83, 0xec, 0x50});  // push rdi/rsi/r14 ; sub rsp,50h
  b.bytes({0x48, 0x89, 0xcf, 0x49, 0x89, 0xd6});              // rdi=object ; r14=buffer
  b.bytes({0xb8, 0x61, 0x01, 0x00, 0x00, 0x66, 0x3b, 0x47, 0x20, 0x75, 0x00});
  // Fallout4.exe +0x164b601
  b.bytes({0x8b, 0x57, 0x78, 0x8b, 0x87, 0xb4, 0x00, 0x00, 0x00,
           0x48, 0x89, 0x74, 0x24, 0x40, 0x8b, 0xb7, 0xb0, 0x00, 0x00, 0x00,
           0x2b, 0xf2, 0x39, 0x77, 0x7c, 0x0f, 0x42, 0x77, 0x7c,
           0x49, 0x03, 0xd6, 0x44, 0x8b, 0xc6, 0x85, 0xf6, 0x74, 0x48,
           0x48, 0x8b, 0x8f, 0xa8, 0x00, 0x00, 0x00});
  memcpySTail(b, 0x5b);
  b.bytes({0x8b, 0x47, 0x0c, 0xc1, 0xe8, 0x19, 0x89, 0xb7, 0xc8, 0x00, 0x00, 0x00});
  b.bytes({0x48, 0x8b, 0x74, 0x24, 0x40});                     // restore rsi
  b.bytes({0x48, 0x83, 0xc4, 0x50, 0x41, 0x5e, 0x5e, 0x5f, 0xc3});
  return b;
}

// Builds the image. `thunkRva` receives the memcpy thunk's location; the
// absolute target is at thunkRva + 6.
inline std::vector<std::uint8_t> buildImage(const Builder& b, std::uint32_t& thunkRva)
{
  std::vector<std::uint8_t> image(kImageSize, 0);
  image[0] = 'M';
  image[1] = 'Z';
  put32(image, 0x3c, 0x40);
  std::memcpy(image.data() + 0x40, "PE\0\0", 4);
  put16(image, 0x44, 0x8664);  // machine
  put16(image, 0x46, 1);       // sections
  put16(image, 0x54, 0xf0);    // optional header size
  const std::uint32_t opt = 0x58;
  put16(image, opt, 0x20b);
  put32(image, opt + 56, kImageSize);  // SizeOfImage
  put32(image, opt + 60, 0x400);       // SizeOfHeaders
  put32(image, opt + 108, 16);         // NumberOfRvaAndSizes
  put32(image, opt + 112 + 8, kImportRva);
  put32(image, opt + 112 + 12, 40);
  const std::uint32_t section = opt + 0xf0;
  std::memcpy(image.data() + section, ".text\0\0\0", 8);
  put32(image, section + 8, 0x1000);
  put32(image, section + 12, kCodeRva);
  put32(image, section + 36, 0x60000020u);  // code | execute | read

  // ucrtbase.dll!_errno, _invalid_parameter_noinfo
  put32(image, kImportRva + 0, kLookupRva);
  put32(image, kImportRva + 12, kNameRva);
  put32(image, kImportRva + 16, kSlotRva);
  std::memcpy(image.data() + kNameRva, "ucrtbase.dll", 13);
  std::memcpy(image.data() + kNameRva + 0x20 + 2, "_errno", 7);
  std::memcpy(image.data() + kNameRva + 0x40 + 2, "_invalid_parameter_noinfo", 26);
  const std::uint64_t n1 = kNameRva + 0x20, n2 = kNameRva + 0x40;
  std::memcpy(image.data() + kLookupRva, &n1, 8);
  std::memcpy(image.data() + kLookupRva + 8, &n2, 8);
  std::memcpy(image.data() + kSlotRva, &n1, 8);
  std::memcpy(image.data() + kSlotRva + 8, &n2, 8);

  std::memcpy(image.data() + kCodeRva, b.code.data(), b.code.size());
  thunkRva = kCodeRva + static_cast<std::uint32_t>((b.code.size() + 15) & ~15u);
  image[thunkRva] = 0xff;
  image[thunkRva + 1] = 0x25;  // jmp [rip+0] ; dq target

  auto link = [&](const std::vector<std::uint32_t>& sites, std::uint32_t target) {
    for (std::uint32_t at : sites) {
      const std::uint32_t rva = kCodeRva + at;
      put32(image, rva, target - (rva + 4));
    }
  };
  link(b.errnoCalls, kSlotRva);
  link(b.invalidCalls, kSlotRva + 8);
  link(b.copyCalls, thunkRva);
  return image;
}

}  // namespace synthetic
