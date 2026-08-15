#include "archiveoutputpath.h"

#include <algorithm>
#include <cwctype>
#include <system_error>
#include <vector>

namespace archive_output
{
namespace
{

  bool isDrivePath(std::wstring_view path)
  {
    return path.size() >= 2 && std::iswalpha(path[0]) != 0 && path[1] == L':';
  }

  bool splitRelativePath(std::wstring_view input, std::vector<std::wstring>& components,
                         std::wstring& error)
  {
    if (input.empty()) {
      error = L"archive output path is empty";
      return false;
    }

    std::wstring normalized(input);
    std::replace(normalized.begin(), normalized.end(), L'\\', L'/');

    if (normalized.front() == L'/' || isDrivePath(normalized)) {
      error = L"archive output path is absolute";
      return false;
    }

    std::size_t start = 0;
    while (start <= normalized.size()) {
      const auto separator = normalized.find(L'/', start);
      const auto end = separator == std::wstring::npos ? normalized.size() : separator;
      auto component = normalized.substr(start, end - start);

      if (component.empty()) {
        error = L"archive output path contains an empty component";
        return false;
      }
      if (component == L"." || component == L"..") {
        error = L"archive output path contains a traversal component";
        return false;
      }
      if (component.find(L'\0') != std::wstring::npos) {
        error = L"archive output path contains a NUL character";
        return false;
      }
#ifdef _WIN32
      if (component.find(L':') != std::wstring::npos) {
        error = L"archive output path contains a Windows stream or drive separator";
        return false;
      }
#endif

      components.push_back(std::move(component));
      if (separator == std::wstring::npos) {
        break;
      }
      start = separator + 1;
    }

    return !components.empty();
  }

  bool validateExistingEntry(const std::filesystem::path& path, bool final,
                             bool directory, std::wstring& error)
  {
    std::error_code ec;
    const auto status = std::filesystem::symlink_status(path, ec);
    if (ec == std::errc::no_such_file_or_directory) {
      return true;
    }
    if (ec) {
      error = L"cannot inspect archive output ancestry";
      return false;
    }
    if (std::filesystem::is_symlink(status)) {
      error = L"archive output path traverses a symbolic link";
      return false;
    }
    if (status.type() == std::filesystem::file_type::not_found) {
      return true;
    }
    if (!final || directory) {
      if (!std::filesystem::is_directory(status)) {
        error = L"archive output directory collides with a non-directory";
        return false;
      }
    } else if (!std::filesystem::is_regular_file(status)) {
      error = L"archive output file collides with a non-regular entry";
      return false;
    }
    return true;
  }

  bool componentEqual(const std::filesystem::path& left,
                      const std::filesystem::path& right)
  {
#ifdef _WIN32
    auto leftText  = left.native();
    auto rightText = right.native();
    if (leftText.size() != rightText.size()) {
      return false;
    }
    return std::equal(leftText.begin(), leftText.end(), rightText.begin(),
                      [](wchar_t leftCharacter, wchar_t rightCharacter) {
                        return std::towlower(leftCharacter) ==
                               std::towlower(rightCharacter);
                      });
#else
    return left == right;
#endif
  }

  bool samePath(const std::filesystem::path& left, const std::filesystem::path& right)
  {
    auto leftPart  = left.begin();
    auto rightPart = right.begin();
    for (; leftPart != left.end() && rightPart != right.end();
         ++leftPart, ++rightPart) {
      if (!componentEqual(*leftPart, *rightPart)) {
        return false;
      }
    }
    return leftPart == left.end() && rightPart == right.end();
  }

  bool strictDescendant(const std::filesystem::path& child,
                        const std::filesystem::path& parent)
  {
    auto childPart  = child.begin();
    auto parentPart = parent.begin();
    for (; parentPart != parent.end(); ++parentPart, ++childPart) {
      if (childPart == child.end() || !componentEqual(*childPart, *parentPart)) {
        return false;
      }
    }
    return childPart != child.end();
  }

}  // namespace

bool validate(const std::filesystem::path& extractionRoot,
              std::wstring_view archivePath, bool directory, ValidatedPath& result,
              std::wstring& error)
{
  result = {};
  error.clear();

  if (extractionRoot.empty() || !extractionRoot.is_absolute()) {
    error = L"archive extraction root is not absolute";
    return false;
  }

  std::error_code ec;
  const auto rootStatus = std::filesystem::symlink_status(extractionRoot, ec);
  if (ec || !std::filesystem::is_directory(rootStatus) ||
      std::filesystem::is_symlink(rootStatus)) {
    error = L"archive extraction root is not a real directory";
    return false;
  }

  const auto physicalRoot = std::filesystem::canonical(extractionRoot, ec);
  if (ec || physicalRoot.empty()) {
    error = L"archive extraction root cannot be authenticated";
    return false;
  }

  std::vector<std::wstring> components;
  if (!splitRelativePath(archivePath, components, error)) {
    return false;
  }

  auto candidate = physicalRoot;
  for (std::size_t i = 0; i < components.size(); ++i) {
    candidate /= std::filesystem::path(components[i]);
    if (!validateExistingEntry(candidate, i + 1 == components.size(), directory,
                               error)) {
      return false;
    }
  }

  const auto relative = candidate.lexically_relative(physicalRoot);
  if (relative.empty() || relative.is_absolute()) {
    error = L"archive output path is outside the extraction root";
    return false;
  }
  for (const auto& component : relative) {
    if (component == L"..") {
      error = L"archive output path is outside the extraction root";
      return false;
    }
  }

  result.root = physicalRoot;
  result.path = std::move(candidate);
  return true;
}

bool validateAll(const std::filesystem::path& extractionRoot,
                 const std::vector<std::wstring>& archivePaths, bool directory,
                 std::vector<ValidatedPath>& results, std::wstring& error)
{
  results.clear();
  error.clear();
  results.reserve(archivePaths.size());

  for (const auto& archivePath : archivePaths) {
    ValidatedPath validated;
    if (!validate(extractionRoot, archivePath, directory, validated, error)) {
      results.clear();
      return false;
    }

    for (const auto& previous : results) {
      if (samePath(validated.path, previous.path)) {
        error = L"archive output batch contains a duplicate destination";
        results.clear();
        return false;
      }
      if (!directory && (strictDescendant(validated.path, previous.path) ||
                         strictDescendant(previous.path, validated.path))) {
        error = L"archive output batch contains file destinations that overlap";
        results.clear();
        return false;
      }
    }
    results.push_back(std::move(validated));
  }

  return true;
}

}  // namespace archive_output
