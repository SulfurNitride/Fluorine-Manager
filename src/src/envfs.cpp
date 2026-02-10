#include "envfs.h"
#include "env.h"
#include "shared/util.h"
#include <log.h>
#include <utility.h>

using namespace MOBase;

#ifdef _WIN32

typedef struct _UNICODE_STRING
{
  USHORT Length;
  USHORT MaximumLength;
  PWSTR Buffer;
} UNICODE_STRING, *PUNICODE_STRING;
typedef const UNICODE_STRING* PCUNICODE_STRING;

typedef struct _OBJECT_ATTRIBUTES
{
  ULONG Length;
  HANDLE RootDirectory;
  PUNICODE_STRING ObjectName;
  ULONG Attributes;
  PVOID SecurityDescriptor;
  PVOID SecurityQualityOfService;
} OBJECT_ATTRIBUTES, *POBJECT_ATTRIBUTES;

typedef struct _FILE_DIRECTORY_INFORMATION
{
  ULONG NextEntryOffset;
  ULONG FileIndex;
  LARGE_INTEGER CreationTime;
  LARGE_INTEGER LastAccessTime;
  LARGE_INTEGER LastWriteTime;
  LARGE_INTEGER ChangeTime;
  LARGE_INTEGER EndOfFile;
  LARGE_INTEGER AllocationSize;
  ULONG FileAttributes;
  ULONG FileNameLength;
  WCHAR FileName[1];
} FILE_DIRECTORY_INFORMATION, *PFILE_DIRECTORY_INFORMATION;

#define FILE_SHARE_VALID_FLAGS 0x00000007

// copied from ntstatus.h
#define STATUS_SUCCESS ((NTSTATUS)0x00000000L)
#define STATUS_BUFFER_OVERFLOW ((NTSTATUS)0x80000005L)
#define STATUS_NO_MORE_FILES ((NTSTATUS)0x80000006L)
#define STATUS_NO_SUCH_FILE ((NTSTATUS)0xC000000FL)

typedef struct _IO_STATUS_BLOCK IO_STATUS_BLOCK;

typedef struct _IO_STATUS_BLOCK* PIO_STATUS_BLOCK;
typedef VOID(NTAPI* PIO_APC_ROUTINE)(PVOID ApcContext, PIO_STATUS_BLOCK IoStatusBlock,
                                     ULONG Reserved);

typedef enum _FILE_INFORMATION_CLASS
{
  FileDirectoryInformation = 1
} FILE_INFORMATION_CLASS;

typedef NTSTATUS(WINAPI* NtQueryDirectoryFile_type)(HANDLE, HANDLE, PIO_APC_ROUTINE,
                                                    PVOID, PIO_STATUS_BLOCK, PVOID,
                                                    ULONG, FILE_INFORMATION_CLASS,
                                                    BOOLEAN, PUNICODE_STRING, BOOLEAN);

typedef NTSTATUS(WINAPI* NtOpenFile_type)(PHANDLE, ACCESS_MASK, POBJECT_ATTRIBUTES,
                                          PIO_STATUS_BLOCK, ULONG, ULONG);

typedef NTSTATUS(WINAPI* NtClose_type)(HANDLE);

NtOpenFile_type NtOpenFile                     = nullptr;
NtQueryDirectoryFile_type NtQueryDirectoryFile = nullptr;
extern NtClose_type NtClose                    = nullptr;

#define FILE_DIRECTORY_FILE 0x00000001
#define FILE_WRITE_THROUGH 0x00000002
#define FILE_SEQUENTIAL_ONLY 0x00000004
#define FILE_NO_INTERMEDIATE_BUFFERING 0x00000008

#define FILE_SYNCHRONOUS_IO_ALERT 0x00000010
#define FILE_SYNCHRONOUS_IO_NONALERT 0x00000020
#define FILE_NON_DIRECTORY_FILE 0x00000040
#define FILE_CREATE_TREE_CONNECTION 0x00000080

#define FILE_COMPLETE_IF_OPLOCKED 0x00000100
#define FILE_NO_EA_KNOWLEDGE 0x00000200
#define FILE_OPEN_REMOTE_INSTANCE 0x00000400
#define FILE_RANDOM_ACCESS 0x00000800

#define FILE_DELETE_ON_CLOSE 0x00001000
#define FILE_OPEN_BY_FILE_ID 0x00002000
#define FILE_OPEN_FOR_BACKUP_INTENT 0x00004000
#define FILE_NO_COMPRESSION 0x00008000

#if (_WIN32_WINNT >= _WIN32_WINNT_WIN7)
#define FILE_OPEN_REQUIRING_OPLOCK 0x00010000
#endif

#define FILE_RESERVE_OPFILTER 0x00100000
#define FILE_OPEN_REPARSE_POINT 0x00200000
#define FILE_OPEN_NO_RECALL 0x00400000
#define FILE_OPEN_FOR_FREE_SPACE_QUERY 0x00800000

#define FILE_VALID_OPTION_FLAGS 0x00ffffff
#define FILE_VALID_PIPE_OPTION_FLAGS 0x00000032
#define FILE_VALID_MAILSLOT_OPTION_FLAGS 0x00000032
#define FILE_VALID_SET_FLAGS 0x00000036

typedef struct _IO_STATUS_BLOCK
{
#pragma warning(push)
#pragma warning(disable : 4201)  // we'll always use the Microsoft compiler
  union
  {
    NTSTATUS Status;
    PVOID Pointer;
  } DUMMYUNIONNAME;
#pragma warning(pop)

  ULONG_PTR Information;
} IO_STATUS_BLOCK, *PIO_STATUS_BLOCK;

namespace env
{

std::wstring_view toStringView(const UNICODE_STRING* s)
{
  if (s && s->Buffer) {
    return {s->Buffer, (s->Length / sizeof(wchar_t))};
  } else {
    return {};
  }
}

std::wstring_view toStringView(POBJECT_ATTRIBUTES poa)
{
  if (poa->ObjectName) {
    return toStringView(poa->ObjectName);
  }

  return {};
}

QString toString(POBJECT_ATTRIBUTES poa)
{
  const auto sv = toStringView(poa);
  return QString::fromWCharArray(sv.data(), static_cast<int>(sv.size()));
}

class HandleCloserThread
{
public:
  HandleCloserThread() : m_ready(false) { m_handles.reserve(50000); }

  void shrink() { m_handles.shrink_to_fit(); }

  void add(HANDLE h) { m_handles.push_back(h); }

  void wakeup()
  {
    {
      std::unique_lock lock(m_mutex);
      m_ready = true;
    }

    m_cv.notify_one();
  }

  void run()
  {
    MOShared::SetThisThreadName("HandleCloserThread");

    std::unique_lock lock(m_mutex);
    m_cv.wait(lock, [&] {
      return m_ready;
    });

    closeHandles();
  }

private:
  std::vector<HANDLE> m_handles;
  std::condition_variable m_cv;
  std::mutex m_mutex;
  bool m_ready;

  void closeHandles()
  {
    for (auto& h : m_handles) {
      NtClose(h);
    }

    m_handles.clear();
    m_ready = false;
  }
};

constexpr std::size_t AllocSize = 1024 * 1024;
static ThreadPool<HandleCloserThread> g_handleClosers;

void setHandleCloserThreadCount(std::size_t n)
{
  g_handleClosers.setMax(n);
}

void forEachEntryImpl(void* cx, HandleCloserThread& hc,
                      std::vector<std::unique_ptr<unsigned char[]>>& buffers,
                      POBJECT_ATTRIBUTES poa, std::size_t depth, DirStartF* dirStartF,
                      DirEndF* dirEndF, FileF* fileF)
{
  IO_STATUS_BLOCK iosb;
  UNICODE_STRING ObjectName;
  OBJECT_ATTRIBUTES oa = {sizeof(oa), 0, &ObjectName};
  NTSTATUS status;

  status = NtOpenFile(&oa.RootDirectory, FILE_GENERIC_READ, poa, &iosb,
                      FILE_SHARE_VALID_FLAGS,
                      FILE_SYNCHRONOUS_IO_NONALERT | FILE_OPEN_FOR_BACKUP_INTENT);

  if (status < 0) {
    log::error("failed to open directory '{}': {}", toString(poa),
               formatNtMessage(status));

    return;
  }

  hc.add(oa.RootDirectory);
  unsigned char* buffer;

  if (depth >= buffers.size()) {
    buffers.emplace_back(std::make_unique<unsigned char[]>(AllocSize));
    buffer = buffers.back().get();
  } else {
    buffer = buffers[depth].get();
  }

  union
  {
    PVOID pv;
    PBYTE pb;
    PFILE_DIRECTORY_INFORMATION DirInfo;
  };

  for (;;) {
    status =
        NtQueryDirectoryFile(oa.RootDirectory, NULL, NULL, NULL, &iosb, buffer,
                             AllocSize, FileDirectoryInformation, FALSE, NULL, FALSE);

    if (status == STATUS_NO_MORE_FILES) {
      break;
    } else if (status < 0) {
      log::error("failed to read directory '{}': {}", toString(poa),
                 formatNtMessage(status));

      break;
    }

    ULONG NextEntryOffset = 0;

    pv = buffer;

    auto isDotDir = [](auto* o) {
      if (o->Length == 2 && o->Buffer[0] == '.') {
        return true;
      }

      if (o->Length == 4 && o->Buffer[0] == '.' && o->Buffer[1] == '.') {
        return true;
      }

      return false;
    };

    std::size_t count = 0;

    for (;;) {
      ++count;
      pb += NextEntryOffset;

      ObjectName.Buffer = DirInfo->FileName;
      ObjectName.Length = (USHORT)DirInfo->FileNameLength;

      if (!isDotDir(&ObjectName)) {
        ObjectName.MaximumLength = ObjectName.Length;

        if (DirInfo->FileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
          if (dirStartF && dirEndF) {
            dirStartF(cx, toStringView(&oa));
            forEachEntryImpl(cx, hc, buffers, &oa, depth + 1, dirStartF, dirEndF,
                             fileF);
            dirEndF(cx, toStringView(&oa));
          }
        } else {
          FILETIME ft;
          ft.dwLowDateTime  = DirInfo->LastWriteTime.LowPart;
          ft.dwHighDateTime = DirInfo->LastWriteTime.HighPart;

          fileF(cx, toStringView(&oa), ft, DirInfo->AllocationSize.QuadPart);
        }
      }

      NextEntryOffset = DirInfo->NextEntryOffset;

      if (NextEntryOffset == 0) {
        break;
      }
    }
  }
}

std::wstring makeNtPath(const std::wstring& path)
{
  constexpr const wchar_t* nt_prefix     = L"\\??\\";
  constexpr const wchar_t* nt_unc_prefix = L"\\??\\UNC\\";
  constexpr const wchar_t* share_prefix  = L"\\\\";

  if (path.starts_with(nt_prefix)) {
    // already an nt path
    return path;
  } else if (path.starts_with(share_prefix)) {
    // network shared need \??\UNC\ as a prefix
    return nt_unc_prefix + path.substr(2);
  } else {
    // prepend the \??\ prefix
    return nt_prefix + path;
  }
}

void DirectoryWalker::forEachEntry(const std::wstring& path, void* cx,
                                   DirStartF* dirStartF, DirEndF* dirEndF, FileF* fileF)
{
  auto& hc = g_handleClosers.request();

  if (!NtOpenFile) {
    LibraryPtr m(::LoadLibraryW(L"ntdll.dll"));
    NtOpenFile = (NtOpenFile_type)::GetProcAddress(m.get(), "NtOpenFile");
    NtQueryDirectoryFile =
        (NtQueryDirectoryFile_type)::GetProcAddress(m.get(), "NtQueryDirectoryFile");
    NtClose = (NtClose_type)::GetProcAddress(m.get(), "NtClose");
  }

  const std::wstring ntpath = makeNtPath(path);

  UNICODE_STRING ObjectName = {};
  ObjectName.Buffer         = const_cast<wchar_t*>(ntpath.c_str());
  ObjectName.Length         = (USHORT)ntpath.size() * sizeof(wchar_t);
  ObjectName.MaximumLength  = ObjectName.Length;

  OBJECT_ATTRIBUTES oa = {};
  oa.Length            = sizeof(oa);
  oa.ObjectName        = &ObjectName;

  forEachEntryImpl(cx, hc, m_buffers, &oa, 0, dirStartF, dirEndF, fileF);
  hc.wakeup();
}

void forEachEntry(const std::wstring& path, void* cx, DirStartF* dirStartF,
                  DirEndF* dirEndF, FileF* fileF)
{
  DirectoryWalker().forEachEntry(path, cx, dirStartF, dirEndF, fileF);
}

Directory getFilesAndDirs(const std::wstring& path)
{
  struct Context
  {
    std::stack<Directory*> current;
  };

  Directory root;

  Context cx;
  cx.current.push(&root);

  env::forEachEntry(
      path, &cx,
      [](void* pcx, std::wstring_view path) {
        Context* cx = (Context*)pcx;

        cx->current.top()->dirs.push_back(Directory(path));
        cx->current.push(&cx->current.top()->dirs.back());
      },

      [](void* pcx, std::wstring_view path) {
        Context* cx = (Context*)pcx;
        cx->current.pop();
      },

      [](void* pcx, std::wstring_view path, FILETIME ft, uint64_t s) {
        Context* cx = (Context*)pcx;

        cx->current.top()->files.push_back(File(path, ft, s));
      });

  return root;
}

void getFilesAndDirsWithFindImpl(const std::wstring& path, Directory& d)
{
  const std::wstring searchString = path + L"\\*";

  WIN32_FIND_DATAW findData;

  HANDLE searchHandle =
      ::FindFirstFileExW(searchString.c_str(), FindExInfoBasic, &findData,
                         FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH);

  if (searchHandle != INVALID_HANDLE_VALUE) {
    BOOL result = true;

    while (result) {
      if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
        if ((wcscmp(findData.cFileName, L".") != 0) &&
            (wcscmp(findData.cFileName, L"..") != 0)) {
          const std::wstring newPath = path + L"\\" + findData.cFileName;
          d.dirs.push_back(Directory(findData.cFileName));
          getFilesAndDirsWithFindImpl(newPath, d.dirs.back());
        }
      } else {
        const auto size =
            (findData.nFileSizeHigh * (MAXDWORD + 1)) + findData.nFileSizeLow;

        d.files.push_back(File(findData.cFileName, findData.ftLastWriteTime, size));
      }

      result = ::FindNextFileW(searchHandle, &findData);
    }
  }

  ::FindClose(searchHandle);
}

Directory getFilesAndDirsWithFind(const std::wstring& path)
{
  Directory d;
  getFilesAndDirsWithFindImpl(path, d);
  return d;
}

}  // namespace env

#else // Linux

#include <dirent.h>
#include <QDir>
#include <QProcess>
#include <sys/stat.h>
#include <cstring>
#include <cctype>
#include <stack>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace env
{

// Convert timespec to FILETIME (100-nanosecond intervals since Jan 1, 1601)
static FILETIME timespecToFiletime(const struct timespec& ts)
{
  // Unix epoch to Windows epoch offset: 11644473600 seconds
  constexpr uint64_t EPOCH_DIFF = 116444736000000000ULL;
  uint64_t winTime = static_cast<uint64_t>(ts.tv_sec) * 10000000ULL +
                     static_cast<uint64_t>(ts.tv_nsec) / 100ULL + EPOCH_DIFF;
  FILETIME ft;
  ft.dwLowDateTime  = static_cast<DWORD>(winTime & 0xFFFFFFFF);
  ft.dwHighDateTime = static_cast<DWORD>(winTime >> 32);
  return ft;
}

// Convert std::wstring path to narrow string for POSIX APIs
static std::string toNarrow(const std::wstring& ws)
{
  return QString::fromStdWString(ws).toStdString();
}

// Convert narrow string to std::wstring
static std::wstring toWide(const std::string& s)
{
  return QString::fromStdString(s).toStdWString();
}

static std::string decodeProcMountField(const std::string& in)
{
  std::string out;
  out.reserve(in.size());

  for (size_t i = 0; i < in.size();) {
    if (in[i] == '\\' && i + 3 < in.size() && std::isdigit(in[i + 1]) &&
        std::isdigit(in[i + 2]) && std::isdigit(in[i + 3])) {
      const std::string oct = in.substr(i + 1, 3);
      const int value       = std::stoi(oct, nullptr, 8);
      out.push_back(static_cast<char>(value));
      i += 4;
      continue;
    }

    out.push_back(in[i]);
    ++i;
  }

  return out;
}

static bool isMountPoint(const std::string& path)
{
  const std::string cleanPath = QDir::cleanPath(QString::fromStdString(path)).toStdString();
  std::ifstream mounts("/proc/mounts");
  if (!mounts.is_open()) {
    return false;
  }

  std::string line;
  while (std::getline(mounts, line)) {
    std::istringstream iss(line);
    std::string device;
    std::string mountPoint;
    if (!(iss >> device >> mountPoint)) {
      continue;
    }

    const std::string decoded =
        QDir::cleanPath(QString::fromStdString(decodeProcMountField(mountPoint)))
            .toStdString();
    if (decoded == cleanPath) {
      return true;
    }
  }

  return false;
}

static bool runUnmountCommand(const QString& program, const QStringList& args)
{
  QProcess p;
  p.start(program, args);
  if (!p.waitForFinished(3000)) {
    p.kill();
    return false;
  }

  return p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0;
}

static bool tryRecoverStaleMount(const std::string& dirPath)
{
  if (!isMountPoint(dirPath)) {
    return false;
  }

  const QString clean = QDir::cleanPath(QString::fromStdString(dirPath));
  log::warn("stale mount detected at '{}', attempting recovery", clean);

  runUnmountCommand("fusermount3", {"-u", clean});
  runUnmountCommand("fusermount", {"-u", clean});
  runUnmountCommand("umount", {clean});
  runUnmountCommand("umount", {"-l", clean});
  runUnmountCommand("fusermount3", {"-uz", clean});
  runUnmountCommand("fusermount", {"-uz", clean});

  const bool recovered = !isMountPoint(dirPath);
  if (recovered) {
    log::info("recovered stale mount at '{}'", clean);
  } else {
    log::warn("failed to recover stale mount at '{}'", clean);
  }

  return recovered;
}

// HandleCloserThread stub for Linux (no NT handles to close)
class HandleCloserThread
{
public:
  HandleCloserThread() : m_ready(false) {}
  void shrink() {}
  void add(void*) {}
  void wakeup()
  {
    std::unique_lock lock(m_mutex);
    m_ready = true;
    m_cv.notify_one();
  }
  void run()
  {
    std::unique_lock lock(m_mutex);
    m_cv.wait(lock, [&] { return m_ready; });
    m_ready = false;
  }

private:
  std::condition_variable m_cv;
  std::mutex m_mutex;
  bool m_ready;
};

static ThreadPool<HandleCloserThread> g_handleClosers;

void setHandleCloserThreadCount(std::size_t n)
{
  g_handleClosers.setMax(n);
}

// Recursive directory walker using POSIX opendir/readdir
static void forEachEntryImpl(const std::string& dirPath, void* cx,
                             DirStartF* dirStartF, DirEndF* dirEndF, FileF* fileF)
{
  DIR* dir = opendir(dirPath.c_str());
  if (!dir && errno == ENOTCONN) {
    if (tryRecoverStaleMount(dirPath)) {
      dir = opendir(dirPath.c_str());
    }
  }
  if (!dir) {
    log::error("failed to open directory '{}': {}",
               QString::fromStdString(dirPath), strerror(errno));
    return;
  }

  struct dirent* entry;
  while ((entry = readdir(dir)) != nullptr) {
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
      continue;
    }

    std::string fullPath = dirPath + "/" + entry->d_name;
    std::wstring wname = toWide(entry->d_name);

    struct stat st;
    if (lstat(fullPath.c_str(), &st) != 0) {
      continue;
    }

    if (S_ISDIR(st.st_mode)) {
      if (dirStartF && dirEndF) {
        std::wstring_view nameView(wname);
        dirStartF(cx, nameView);
        forEachEntryImpl(fullPath, cx, dirStartF, dirEndF, fileF);
        dirEndF(cx, nameView);
      }
    } else {
      FILETIME ft = timespecToFiletime(st.st_mtim);
      fileF(cx, std::wstring_view(wname), ft, static_cast<uint64_t>(st.st_size));
    }
  }

  closedir(dir);
}

void DirectoryWalker::forEachEntry(const std::wstring& path, void* cx,
                                   DirStartF* dirStartF, DirEndF* dirEndF, FileF* fileF)
{
  forEachEntryImpl(toNarrow(path), cx, dirStartF, dirEndF, fileF);
}

void forEachEntry(const std::wstring& path, void* cx, DirStartF* dirStartF,
                  DirEndF* dirEndF, FileF* fileF)
{
  DirectoryWalker().forEachEntry(path, cx, dirStartF, dirEndF, fileF);
}

Directory getFilesAndDirs(const std::wstring& path)
{
  struct Context
  {
    std::stack<Directory*> current;
  };

  Directory root;

  Context cx;
  cx.current.push(&root);

  env::forEachEntry(
      path, &cx,
      [](void* pcx, std::wstring_view path) {
        Context* cx = (Context*)pcx;
        cx->current.top()->dirs.push_back(Directory(path));
        cx->current.push(&cx->current.top()->dirs.back());
      },

      [](void* pcx, std::wstring_view) {
        Context* cx = (Context*)pcx;
        cx->current.pop();
      },

      [](void* pcx, std::wstring_view path, FILETIME ft, uint64_t s) {
        Context* cx = (Context*)pcx;
        cx->current.top()->files.push_back(File(path, ft, s));
      });

  return root;
}

// Linux equivalent of getFilesAndDirsWithFind - same as getFilesAndDirs
Directory getFilesAndDirsWithFind(const std::wstring& path)
{
  return getFilesAndDirs(path);
}

}  // namespace env

#endif // _WIN32

// Cross-platform constructors
namespace env
{

File::File(std::wstring_view n, FILETIME ft, uint64_t s)
    : name(n.begin(), n.end()), lcname(MOShared::ToLowerCopy(name)), lastModified(ft),
      size(s)
{}

Directory::Directory() {}

Directory::Directory(std::wstring_view n)
    : name(n.begin(), n.end()), lcname(MOShared::ToLowerCopy(name))
{}

}  // namespace env
