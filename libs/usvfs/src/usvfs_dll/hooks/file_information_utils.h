#pragma once

/**
 * This header defines function to deal with the various FileInformationClass
 * enumeration.
 */

#include <type_traits>

#include <ntdll_declarations.h>

#include <windows.h>

namespace usvfs::details
{

#define DECLARE_HAS_FIELD(Field)                                                       \
  template <typename T>                                                                \
  constexpr auto HasFieldImpl##Field(int)->decltype(std::declval<T>().Field, void(),   \
                                                    std::true_type())                  \
  {                                                                                    \
    return {};                                                                         \
  }                                                                                    \
  template <typename T>                                                                \
  constexpr auto HasFieldImpl##Field(...)                                              \
  {                                                                                    \
    return std::false_type{};                                                          \
  }                                                                                    \
  template <typename T>                                                                \
  constexpr auto HasField##Field()                                                     \
  {                                                                                    \
    return HasFieldImpl##Field<T>(0);                                                  \
  }

DECLARE_HAS_FIELD(FileName)
DECLARE_HAS_FIELD(NextEntryOffset)
DECLARE_HAS_FIELD(ShortName)

#undef DECLARE_HAS_FIELD

template <FILE_INFORMATION_CLASS fileInformationClass, class FileInformationClass>
struct FileInformationClassUtilsImpl
{
  static void get_data(LPCVOID address, ULONG& offset, std::wstring& fileName)
  {
    const FileInformationClass* info =
        reinterpret_cast<const FileInformationClass*>(address);

    // default value
    offset = sizeof(FileInformationClass);

    if constexpr (HasFieldFileName<FileInformationClass>()) {
      if constexpr (HasFieldNextEntryOffset<FileInformationClass>()) {
        offset = info->NextEntryOffset;
      }
      fileName = std::wstring(info->FileName, info->FileNameLength / sizeof(WCHAR));
    }
  }

  static void set_offset(LPVOID address, ULONG offset)
  {
    FileInformationClass* info = reinterpret_cast<FileInformationClass*>(address);

    if constexpr (HasFieldNextEntryOffset<FileInformationClass>()) {
      info->NextEntryOffset = offset;
    }
  }

  static void set_filename(LPVOID address, const std::wstring& fileName)
  {
    FileInformationClass* info = reinterpret_cast<FileInformationClass*>(address);

    if constexpr (HasFieldFileName<FileInformationClass>()) {
      memset(info->FileName, L'\0', info->FileNameLength);
      info->FileNameLength = static_cast<ULONG>(fileName.length() * sizeof(WCHAR));

      // doesn't need to be 0-terminated
      memcpy(info->FileName, fileName.c_str(), info->FileNameLength);

      if constexpr (HasFieldShortName<FileInformationClass>()) {
        if (info->ShortNameLength > 0) {
          info->ShortNameLength = static_cast<CCHAR>(
              GetShortPathNameW(fileName.c_str(), info->ShortName, 8));
        }
      }
    }
  }
};

template <FILE_INFORMATION_CLASS fileInformationClass>
struct FileInformationClassUtils;

template <>
struct FileInformationClassUtils<FileDirectoryInformation>
    : FileInformationClassUtilsImpl<FileDirectoryInformation,
                                    FILE_DIRECTORY_INFORMATION>
{};
template <>
struct FileInformationClassUtils<FileFullDirectoryInformation>
    : FileInformationClassUtilsImpl<FileFullDirectoryInformation,
                                    FILE_FULL_DIR_INFORMATION>
{};
template <>
struct FileInformationClassUtils<FileBothDirectoryInformation>
    : FileInformationClassUtilsImpl<FileBothDirectoryInformation,
                                    FILE_BOTH_DIR_INFORMATION>
{};
template <>
struct FileInformationClassUtils<FileStandardInformation>
    : FileInformationClassUtilsImpl<FileStandardInformation, FILE_STANDARD_INFORMATION>
{};
template <>
struct FileInformationClassUtils<FileNameInformation>
    : FileInformationClassUtilsImpl<FileNameInformation, FILE_NAME_INFORMATION>
{};
template <>
struct FileInformationClassUtils<FileRenameInformation>
    : FileInformationClassUtilsImpl<FileRenameInformation, FILE_RENAME_INFORMATION>
{};
template <>
struct FileInformationClassUtils<FileNamesInformation>
    : FileInformationClassUtilsImpl<FileNamesInformation, FILE_NAMES_INFORMATION>
{};
template <>
struct FileInformationClassUtils<FileObjectIdInformation>
    : FileInformationClassUtilsImpl<FileObjectIdInformation, FILE_OBJECTID_INFORMATION>
{};
template <>
struct FileInformationClassUtils<FileReparsePointInformation>
    : FileInformationClassUtilsImpl<FileReparsePointInformation,
                                    FILE_REPARSE_POINT_INFORMATION>
{};
template <>
struct FileInformationClassUtils<FileIdBothDirectoryInformation>
    : FileInformationClassUtilsImpl<FileIdBothDirectoryInformation,
                                    FILE_ID_BOTH_DIR_INFORMATION>
{};
template <>
struct FileInformationClassUtils<FileIdFullDirectoryInformation>
    : FileInformationClassUtilsImpl<FileIdFullDirectoryInformation,
                                    FILE_ID_FULL_DIR_INFORMATION>
{};
template <>
struct FileInformationClassUtils<FileNormalizedNameInformation>
    : FileInformationClassUtilsImpl<FileNormalizedNameInformation,
                                    FILE_NAME_INFORMATION>
{};
template <>
struct FileInformationClassUtils<FileIdExtdDirectoryInformation>
    : FileInformationClassUtilsImpl<FileIdExtdDirectoryInformation,
                                    FILE_ID_EXTD_DIR_INFORMATION>
{};
template <>
struct FileInformationClassUtils<FileIdExtdBothDirectoryInformation>
    : FileInformationClassUtilsImpl<FileIdExtdBothDirectoryInformation,
                                    FILE_ID_EXTD_BOTH_DIR_INFORMATION>
{};
template <>
struct FileInformationClassUtils<FileId64ExtdDirectoryInformation>
    : FileInformationClassUtilsImpl<FileId64ExtdDirectoryInformation,
                                    FILE_ID_64_EXTD_DIR_INFORMATION>
{};
template <>
struct FileInformationClassUtils<FileId64ExtdBothDirectoryInformation>
    : FileInformationClassUtilsImpl<FileId64ExtdBothDirectoryInformation,
                                    FILE_ID_64_EXTD_BOTH_DIR_INFORMATION>
{};
template <>
struct FileInformationClassUtils<FileIdAllExtdDirectoryInformation>
    : FileInformationClassUtilsImpl<FileIdAllExtdDirectoryInformation,
                                    FILE_ID_ALL_EXTD_DIR_INFORMATION>
{};
template <>
struct FileInformationClassUtils<FileIdAllExtdBothDirectoryInformation>
    : FileInformationClassUtilsImpl<FileIdAllExtdBothDirectoryInformation,
                                    FILE_ID_ALL_EXTD_BOTH_DIR_INFORMATION>
{};

// FILE_ALL_INFORMATION needs to be handled differently because it has a field that
// is itself a structure
template <>
struct FileInformationClassUtils<FileAllInformation>
{
  static void get_data(LPCVOID address, ULONG& offset, std::wstring& fileName)
  {
    FileInformationClassUtils<FileNameInformation>::get_data(
        &reinterpret_cast<const FILE_ALL_INFORMATION*>(address)->NameInformation,
        offset, fileName);
  }

  static void set_offset(LPVOID address, ULONG offset)
  {
    // this is a no-op but it's consistent to do that everywhere
    FileInformationClassUtils<FileNameInformation>::set_offset(
        &reinterpret_cast<FILE_ALL_INFORMATION*>(address)->NameInformation, offset);
  }

  static void set_filename(LPVOID address, const std::wstring& fileName)
  {
    FileInformationClassUtils<FileNameInformation>::set_filename(
        &reinterpret_cast<FILE_ALL_INFORMATION*>(address)->NameInformation, fileName);
  }
};

}  // namespace usvfs::details

#define _APP_FINFO_CASE(clazz, fn, ...)                                                \
  case clazz:                                                                          \
    return usvfs::details::FileInformationClassUtils<clazz>::fn(__VA_ARGS__);

#define _APPLY_FILEINFO_FN(fn, ...)                                                    \
  _APP_FINFO_CASE(FileAllInformation, fn, __VA_ARGS__)                                 \
  _APP_FINFO_CASE(FileDirectoryInformation, fn, __VA_ARGS__)                           \
  _APP_FINFO_CASE(FileFullDirectoryInformation, fn, __VA_ARGS__)                       \
  _APP_FINFO_CASE(FileBothDirectoryInformation, fn, __VA_ARGS__)                       \
  _APP_FINFO_CASE(FileStandardInformation, fn, __VA_ARGS__)                            \
  _APP_FINFO_CASE(FileNameInformation, fn, __VA_ARGS__)                                \
  _APP_FINFO_CASE(FileRenameInformation, fn, __VA_ARGS__)                              \
  _APP_FINFO_CASE(FileNamesInformation, fn, __VA_ARGS__)                               \
  _APP_FINFO_CASE(FileObjectIdInformation, fn, __VA_ARGS__)                            \
  _APP_FINFO_CASE(FileReparsePointInformation, fn, __VA_ARGS__)                        \
  _APP_FINFO_CASE(FileIdBothDirectoryInformation, fn, __VA_ARGS__)                     \
  _APP_FINFO_CASE(FileIdFullDirectoryInformation, fn, __VA_ARGS__)                     \
  _APP_FINFO_CASE(FileNormalizedNameInformation, fn, __VA_ARGS__)                      \
  _APP_FINFO_CASE(FileIdExtdDirectoryInformation, fn, __VA_ARGS__)                     \
  _APP_FINFO_CASE(FileIdExtdBothDirectoryInformation, fn, __VA_ARGS__)                 \
  _APP_FINFO_CASE(FileId64ExtdDirectoryInformation, fn, __VA_ARGS__)                   \
  _APP_FINFO_CASE(FileId64ExtdBothDirectoryInformation, fn, __VA_ARGS__)               \
  _APP_FINFO_CASE(FileIdAllExtdDirectoryInformation, fn, __VA_ARGS__)                  \
  _APP_FINFO_CASE(FileIdAllExtdBothDirectoryInformation, fn, __VA_ARGS__)

void GetFileInformationData(FILE_INFORMATION_CLASS fileInformationClass,
                            LPCVOID address, ULONG& offset, std::wstring& fileName)
{
  switch (fileInformationClass) {
    _APPLY_FILEINFO_FN(get_data, address, offset, fileName);
  default:
    offset = ULONG_MAX;
  }
}

void SetFileInformationOffset(FILE_INFORMATION_CLASS fileInformationClass,
                              LPVOID address, ULONG offset)
{
  switch (fileInformationClass) {
    _APPLY_FILEINFO_FN(set_offset, address, offset);
  default:
    break;
  }
}

void SetFileInformationFileName(FILE_INFORMATION_CLASS fileInformationClass,
                                LPVOID address, const std::wstring& fileName)
{
  switch (fileInformationClass) {
    _APPLY_FILEINFO_FN(set_filename, address, fileName);
  default:
    break;
  }
}

#undef _APP_FINFO_CASE
#undef _APPLY_FILEINFO_FN
