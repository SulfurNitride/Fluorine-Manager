#ifndef ARCHIVEOUTPUTPATH_H
#define ARCHIVEOUTPUTPATH_H

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace archive_output
{

struct ValidatedPath
{
  std::filesystem::path root;
  std::filesystem::path path;
};

bool validate(const std::filesystem::path& extractionRoot,
              std::wstring_view archivePath, bool directory, ValidatedPath& result,
              std::wstring& error);

bool validateAll(const std::filesystem::path& extractionRoot,
                 const std::vector<std::wstring>& archivePaths, bool directory,
                 std::vector<ValidatedPath>& results, std::wstring& error);

}  // namespace archive_output

#endif  // ARCHIVEOUTPUTPATH_H
