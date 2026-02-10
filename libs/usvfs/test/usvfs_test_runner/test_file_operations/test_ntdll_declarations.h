#pragma once

#define STATUS_NO_MORE_FILES 0x80000006
#define STATUS_END_OF_FILE 0xC0000011
#define STATUS_OBJECT_NAME_NOT_FOUND 0xC0000034
#define STATUS_OBJECT_PATH_NOT_FOUND 0xC000003A

#define FILE_WRITE_TO_END_OF_FILE 0xffffffff
#define FILE_USE_FILE_POINTER_POSITION 0xfffffffe

#define InitializeObjectAttributes(p, n, a, r, s)                                      \
  {                                                                                    \
    (p)->Length                   = sizeof(OBJECT_ATTRIBUTES);                         \
    (p)->RootDirectory            = r;                                                 \
    (p)->Attributes               = a;                                                 \
    (p)->ObjectName               = n;                                                 \
    (p)->SecurityDescriptor       = s;                                                 \
    (p)->SecurityQualityOfService = NULL;                                              \
  }

// winternl.h of course defines a joke FILE_INFORMATION_CLASS (why?!)
// so added MY_ to avoid name collision but this is FILE_INFORMATION_CLASS

typedef enum _MY_FILE_INFORMATION_CLASS
{
  MyFileDirectoryInformation = 1,
  MyFileFullDirectoryInformation,
  MyFileBothDirectoryInformation,
  MyFileBasicInformation,
  MyFileStandardInformation,
  MyFileInternalInformation,
  MyFileEaInformation,
  MyFileAccessInformation,
  MyFileNameInformation,
  MyFileRenameInformation,
  MyFileLinkInformation,
  MyFileNamesInformation,
  MyFileDispositionInformation,
  MyFilePositionInformation,
  MyFileFullEaInformation,
  MyFileModeInformation,
  MyFileAlignmentInformation,
  MyFileAllInformation,
  MyFileAllocationInformation,
  MyFileEndOfFileInformation,
  MyFileAlternateNameInformation,
  MyFileStreamInformation,
  MyFilePipeInformation,
  MyFilePipeLocalInformation,
  MyFilePipeRemoteInformation,
  MyFileMailslotQueryInformation,
  MyFileMailslotSetInformation,
  MyFileCompressionInformation,
  MyFileObjectIdInformation,
  MyFileCompletionInformation,
  MyFileMoveClusterInformation,
  MyFileQuotaInformation,
  MyFileReparsePointInformation,
  MyFileNetworkOpenInformation,
  MyFileAttributeTagInformation,
  MyFileTrackingInformation,
  MyFileIdBothDirectoryInformation,
  MyFileIdFullDirectoryInformation,
  MyFileValidDataLengthInformation,
  MyFileShortNameInformation,
  MyFileIoCompletionNotificationInformation,
  MyFileIoStatusBlockRangeInformation,
  MyFileIoPriorityHintInformation,
  MyFileSfioReserveInformation,
  MyFileSfioVolumeInformation,
  MyFileHardLinkInformation,
  MyFileProcessIdsUsingFileInformation,
  MyFileNormalizedNameInformation,
  MyFileNetworkPhysicalNameInformation,
  MyFileIdGlobalTxDirectoryInformation,
  MyFileIsRemoteDeviceInformation,
  MyFileUnusedInformation,
  MyFileNumaNodeInformation,
  MyFileStandardLinkInformation,
  MyFileRemoteProtocolInformation,
  MyFileRenameInformationBypassAccessCheck,
  MyFileLinkInformationBypassAccessCheck,
  MyFileVolumeNameInformation,
  MyFileIdInformation,
  MyFileIdExtdDirectoryInformation,
  MyFileReplaceCompletionInformation,
  MyFileHardLinkFullIdInformation,
  MyFileIdExtdBothDirectoryInformation,
  MyFileDispositionInformationEx,
  MyFileRenameInformationEx,
  MyFileRenameInformationExBypassAccessCheck,
  MyFileMaximumInformation
} MY_FILE_INFORMATION_CLASS,
    *PMY_FILE_INFORMATION_CLASS;

typedef struct _FILE_BOTH_DIR_INFORMATION
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
  ULONG EaSize;
  CCHAR ShortNameLength;
  WCHAR ShortName[12];
  WCHAR FileName[1];
} FILE_BOTH_DIR_INFORMATION, *PFILE_BOTH_DIR_INFORMATION;

typedef struct _FILE_BASIC_INFORMATION
{
  LARGE_INTEGER CreationTime;
  LARGE_INTEGER LastAccessTime;
  LARGE_INTEGER LastWriteTime;
  LARGE_INTEGER ChangeTime;
  ULONG FileAttributes;
} FILE_BASIC_INFORMATION, *PFILE_BASIC_INFORMATION;

typedef struct _FILE_END_OF_FILE_INFORMATION
{
  LARGE_INTEGER EndOfFile;
} FILE_END_OF_FILE_INFORMATION, *PFILE_END_OF_FILE_INFORMATION;

typedef struct _FILE_RENAME_INFORMATION
{
  BOOLEAN ReplaceIfExists;
  HANDLE RootDirectory;
  ULONG FileNameLength;
  WCHAR FileName[1];
} FILE_RENAME_INFORMATION, *PFILE_RENAME_INFORMATION;

extern "C" __kernel_entry NTSTATUS NTAPI NtQueryDirectoryFile(
    IN HANDLE FileHandle, IN HANDLE Event, IN PIO_APC_ROUTINE ApcRoutine,
    IN PVOID ApcContext, OUT PIO_STATUS_BLOCK IoStatusBlock, OUT PVOID FileInformation,
    IN ULONG Length, IN MY_FILE_INFORMATION_CLASS FileInformationClass,
    IN BOOLEAN ReturnSingleEntry, IN PUNICODE_STRING FileName, IN BOOLEAN RestartScan);

extern "C" __kernel_entry NTSTATUS NTAPI
NtReadFile(IN HANDLE FileHandle, IN HANDLE Event, IN PIO_APC_ROUTINE ApcRoutine,
           IN PVOID ApcContext, OUT PIO_STATUS_BLOCK IoStatusBlock, OUT PVOID Buffer,
           IN ULONG Length, IN PLARGE_INTEGER ByteOffset, IN PULONG Key);

extern "C" __kernel_entry NTSTATUS NTAPI
NtWriteFile(IN HANDLE FileHandle, IN HANDLE Event, IN PIO_APC_ROUTINE ApcRoutine,
            IN PVOID ApcContext, OUT PIO_STATUS_BLOCK IoStatusBlock, IN PVOID Buffer,
            IN ULONG Length, IN PLARGE_INTEGER ByteOffset, IN PULONG Key);

extern "C" __kernel_entry NTSTATUS NTAPI NtSetInformationFile(
    IN HANDLE FileHandle, OUT PIO_STATUS_BLOCK IoStatusBlock, IN PVOID FileInformation,
    IN ULONG Length, IN MY_FILE_INFORMATION_CLASS FileInformationClass);

extern "C" __kernel_entry NTSTATUS NTAPI
NtDeleteFile(IN POBJECT_ATTRIBUTES ObjectAttributes);
