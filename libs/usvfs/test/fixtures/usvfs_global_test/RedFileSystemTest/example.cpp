#include <filesystem>
#include <fstream>

std::filesystem::path storages_path;
std::filesystem::path hudpainter_path;
bool has_mo2;

// See below main() for implementation.
void request_directory(std::filesystem::path);
std::filesystem::path restrict_path(std::filesystem::path);
std::vector<std::filesystem::path> get_files();
std::string read_file(std::filesystem::path);
void write_file(std::filesystem::path);

// Expected path when not using MO2
int main()
{
  // RedFileSystem plugin (DLL) is loaded.
  has_mo2 = GetModuleHandle(TEXT("usvfs_x64.dll")) != nullptr;

  // Setup paths
  auto path      = std::filesystem::absolute(".");    // <GAME>\bin\x64
  auto game_path = path.parent_path().parent_path();  // <GAME>

  storages_path = game_path / "r6" / "storages";  // <GAME>\r6\storages

  // Create storages directory if it is no present.
  request_directory(storages_path);  // <GAME>\r6\storages

  // Game is running...

  // HUD Painter request its storage endpoint.
  hudpainter_path = storages_path / "HUDPainter";
  request_directory(hudpainter_path);  // <GAME>\r6\storages\HUDPainter

  // HUD Painter request a file (present in archive).
  auto default_path =
      restrict_path("DEFAULT.json");  // <GAME>\r6\storages\HUDPainter\DEFAULT.json

  // HUD Painter read file in `default_path` with success.
  auto data = read_file(default_path);  // <GAME>\r6\storages\HUDPainter\DEFAULT.json

  // HUD Painter request another file.
  auto test_path =
      restrict_path("TEST.json");  // <GAME>\r6\storages\HUDPainter\TEST.json

  // (Bug A) HUD Painter write file in `test_path`.
  write_file(test_path);  // <GAME>\r6\storages\HUDPainter\TEST.json
                          // file is created with workaround, not created otherwise.

  // (Bug B) HUD Painter request list of files.
  //         TEST.json is successfully created when using workaround.
  auto files = get_files();  // [0] <GAME>\r6\storages\HUDPainter\DEFAULT.json

  // files.size() == 1

  // Expect:
  // files.size() == 2
  //
  // [0] <GAME>\r6\storages\HUDPainter\DEFAULT.json
  // [1] <GAME>\r6\storages\HUDPainter\TEST.json
}

// Create directory if it is no present
void request_directory(std::filesystem::path path)
{
  bool is_present = std::filesystem::exists(path);

  if (is_present) {
    return;
  }
  std::filesystem::create_directory(path);
}

// Resolve path for HUDPainter (security layer)
std::filesystem::path restrict_path(std::filesystem::path path)
{
  auto real_path = std::filesystem::weakly_canonical(hudpainter_path / path);

  // Workaround when using MO2
  if (has_mo2) {
    return real_path;
  }
  // Expected implementation without MO2
  if (real_path.string().find(hudpainter_path.string() + "\\") != 0) {
    // "throw" error
  }
  return real_path;
}

// List files in storage of HUDPainter.
std::vector<std::filesystem::path> get_files()
{
  std::vector<std::filesystem::path> files;
  auto entries = std::filesystem::directory_iterator(hudpainter_path);

  for (const auto& entry : entries) {
    if (entry.is_regular_file()) {
      auto file_name = entry.path().filename();
      auto file_path =
          hudpainter_path /
          file_name;  // Culprit of bug B. When using entry.path() directly, it works.

      files.emplace_back(file_path);
    }
  }
  return files;
}

std::string read_file(std::filesystem::path path)
{
  std::ifstream stream;

  // With workaround:
  // std::filesystem::create_directories(path.parent_path());
  stream.open(path);
  if (!stream.is_open()) {
    return "";
  }
  std::stringstream data;

  data << stream.rdbuf();
  stream.close();
  return data.str();
}

void write_file(std::filesystem::path path)
{
  std::ofstream stream;

  // With workaround:
  // std::filesystem::create_directories(path.parent_path());
  stream.open(path, std::ios_base::trunc);
  if (!stream.is_open()) {
    return;
  }
  stream << "<DATA>";
  stream.close();
}
