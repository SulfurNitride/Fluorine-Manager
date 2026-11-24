// Native scanner tests. With no arguments, checks the synthetic Skyrim and
// Fallout 4 images and negative cases. Given paths to game executables, maps
// each by section and reports the site found (exactly one is required).
#include "scanner.h"
#include "synthetic.h"

#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>

namespace {
int failures = 0;

void check(bool ok, const std::string& what)
{
  std::printf("%s %s\n", ok ? "PASS" : "FAIL", what.c_str());
  if (!ok) ++failures;
}

void describe(const audioguard::Site& s)
{
  std::printf("  check=+0x%x copy=+0x%x anchor=+0x%x base=%s count=%s\n"
              "  capacity=0x%x destination=0x%x read=0x%x chunk=0x%x data=0x%x size=0x%x\n",
              s.checkRva, s.copyRva, s.anchorRva, audioguard::registerName64(s.baseReg),
              audioguard::registerName32(s.countReg), s.capacityOffset,
              s.destinationOffset, s.readOffset, s.chunkOffset, s.dataOffset,
              s.sizeOffset);
}

void expectLayout(const audioguard::ScanResult& r, const synthetic::Layout& l,
                  std::uint8_t base, std::uint8_t count)
{
  const std::string n = l.name;
  check(r.sites.size() == 1, n + ": exactly one site");
  if (r.sites.size() != 1) return;
  const auto& s = r.sites[0];
  describe(s);
  check(s.baseReg == base && s.countReg == count, n + ": registers");
  check(s.capacityOffset == int(l.capacity) && s.destinationOffset == int(l.destination),
        n + ": capacity and destination offsets");
  check(s.readOffset == int(l.read) && s.chunkOffset == int(l.chunk) &&
            s.dataOffset == int(l.dataOffset) && s.sizeOffset == int(l.size),
        n + ": diagnostic offsets");
}

audioguard::ScanResult scanBuilder(const synthetic::Builder& b)
{
  std::uint32_t thunk;
  auto image = synthetic::buildImage(b, thunk);
  return audioguard::scan(image.data(), image.size());
}

bool mapFile(const char* path, std::vector<std::uint8_t>& image)
{
  std::ifstream in(path, std::ios::binary);
  std::vector<std::uint8_t> file((std::istreambuf_iterator<char>(in)), {});
  auto u16 = [&](std::size_t at) { std::uint16_t v; std::memcpy(&v, &file[at], 2); return v; };
  auto u32 = [&](std::size_t at) { std::uint32_t v; std::memcpy(&v, &file[at], 4); return v; };
  if (file.size() < 0x400) return false;
  const std::uint32_t nt = u32(0x3c), opt = nt + 24;
  if (opt + 64 > file.size()) return false;
  image.assign(u32(opt + 56), 0);
  const std::uint32_t headers = std::min<std::uint32_t>(u32(opt + 60), file.size());
  std::memcpy(image.data(), file.data(), std::min<std::size_t>(headers, image.size()));
  const std::uint32_t sections = opt + u16(nt + 20);
  for (std::uint16_t i = 0; i < u16(nt + 6); ++i) {
    const std::size_t h = sections + i * 40;
    const std::uint32_t va = u32(h + 12), raw = u32(h + 16), ptr = u32(h + 20);
    const std::size_t n = std::min<std::size_t>({raw, file.size() - std::min<std::size_t>(ptr, file.size()),
                                                 image.size() - std::min<std::size_t>(va, image.size())});
    std::memcpy(image.data() + va, file.data() + ptr, n);
  }
  return true;
}
}  // namespace

int main(int argc, char** argv)
{
  if (argc > 1) {
    for (int i = 1; i < argc; ++i) {
      std::vector<std::uint8_t> image;
      if (!mapFile(argv[i], image)) {
        check(false, std::string(argv[i]) + ": readable PE image");
        continue;
      }
      const auto r = audioguard::scan(image.data(), image.size());
      std::printf("%s: valid=%d imports=%d sites=%zu\n", argv[i], r.validImage,
                  r.hasImports, r.sites.size());
      for (const auto& s : r.sites) describe(s);
      check(r.sites.size() == 1, std::string(argv[i]) + ": exactly one site");
    }
    return failures ? 1 : 0;
  }

  expectLayout(scanBuilder(synthetic::skyrimCode()), synthetic::kSkyrim, 3, 7);
  expectLayout(scanBuilder(synthetic::fallout4Code()), synthetic::kFallout4, 7, 6);

  {  // ERANGE replaced by another errno: not memcpy_s' bounds failure.
    auto b = synthetic::skyrimCode();
    for (std::size_t i = 0; i + 3 < b.code.size(); ++i)
      if (b.code[i] == 0xc7 && b.code[i + 1] == 0x00 && b.code[i + 2] == 0x22) b.code[i + 2] = 0x21;
    check(scanBuilder(b).sites.empty(), "rejects a non-ERANGE failure path");
  }
  {  // No WMA format check: not the audio data source.
    auto b = synthetic::skyrimCode();
    for (std::size_t i = 0; i + 5 < b.code.size(); ++i)
      if (b.code[i] == 0xb8 && b.code[i + 1] == 0x61 && b.code[i + 2] == 0x01) b.code[i + 1] = 0x62;
    check(scanBuilder(b).sites.empty(), "rejects code without the WMA format check");
  }
  {  // Import renamed: the calls are not the CRT's.
    std::uint32_t thunk;
    auto image = synthetic::buildImage(synthetic::skyrimCode(), thunk);
    image[synthetic::kNameRva + 0x40 + 2] = 'X';
    const auto r = audioguard::scan(image.data(), image.size());
    check(!r.hasImports && r.sites.empty(), "rejects calls not through _invalid_parameter_noinfo");
  }
  {  // Two candidate sites: reported, so the DLL can refuse to choose.
    auto a = synthetic::skyrimCode();
    const auto b = synthetic::skyrimCode();
    const std::size_t shift = a.code.size();
    a.code.insert(a.code.end(), b.code.begin(), b.code.end());
    for (auto at : b.errnoCalls) a.errnoCalls.push_back(at + shift);
    for (auto at : b.invalidCalls) a.invalidCalls.push_back(at + shift);
    for (auto at : b.copyCalls) a.copyCalls.push_back(at + shift);
    check(scanBuilder(a).sites.size() == 2, "reports every candidate site");
  }
  {  // Truncated and malformed images.
    std::vector<std::uint8_t> junk(64, 0);
    check(!audioguard::scan(junk.data(), junk.size()).validImage, "rejects a non-PE buffer");
  }
  return failures ? 1 : 0;
}
