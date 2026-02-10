/*
Userspace Virtual Filesystem

Copyright (C) 2015 Sebastian Herbord. All rights reserved.

This file is part of usvfs.

usvfs is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

usvfs is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with usvfs. If not, see <http://www.gnu.org/licenses/>.
*/
#pragma once

#include "windows_sane.h"

#pragma warning(push)
#pragma warning(disable : 4201)

typedef LONG NTSTATUS;

typedef struct _IO_STATUS_BLOCK
{
  union
  {
    NTSTATUS Status;
    PVOID Pointer;
  } DUMMYUNIONNAME;

  ULONG_PTR Information;
} IO_STATUS_BLOCK, *PIO_STATUS_BLOCK;

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

typedef struct _FILE_FULL_DIR_INFORMATION
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
  WCHAR FileName[1];
} FILE_FULL_DIR_INFORMATION, *PFILE_FULL_DIR_INFORMATION;

typedef struct _FILE_ID_FULL_DIR_INFORMATION
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
  LARGE_INTEGER FileId;
  WCHAR FileName[1];
} FILE_ID_FULL_DIR_INFORMATION, *PFILE_ID_FULL_DIR_INFORMATION;

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

typedef struct _FILE_ID_BOTH_DIR_INFORMATION
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
  LARGE_INTEGER FileId;
  WCHAR FileName[1];
} FILE_ID_BOTH_DIR_INFORMATION, *PFILE_ID_BOTH_DIR_INFORMATION;

typedef struct _FILE_BASIC_INFORMATION
{
  LARGE_INTEGER CreationTime;
  LARGE_INTEGER LastAccessTime;
  LARGE_INTEGER LastWriteTime;
  LARGE_INTEGER ChangeTime;
  ULONG FileAttributes;
} FILE_BASIC_INFORMATION, *PFILE_BASIC_INFORMATION;

typedef struct _FILE_RENAME_INFORMATION
{
#if (_WIN32_WINNT >= _WIN32_WINNT_WIN10_RS1)
  union
  {
    BOOLEAN ReplaceIfExists;  // FileRenameInformation
    ULONG Flags;              // FileRenameInformationEx
  } DUMMYUNIONNAME;
#else
  BOOLEAN ReplaceIfExists;
#endif
  HANDLE RootDirectory;
  ULONG FileNameLength;
  WCHAR FileName[1];
} FILE_RENAME_INFORMATION, *PFILE_RENAME_INFORMATION;

typedef struct _FILE_STANDARD_INFORMATION
{
  LARGE_INTEGER AllocationSize;
  LARGE_INTEGER EndOfFile;
  ULONG NumberOfLinks;
  BOOLEAN DeletePending;
  BOOLEAN Directory;
} FILE_STANDARD_INFORMATION, *PFILE_STANDARD_INFORMATION;

typedef struct _FILE_NAMES_INFORMATION
{
  ULONG NextEntryOffset;
  ULONG FileIndex;
  ULONG FileNameLength;
  WCHAR FileName[1];
} FILE_NAMES_INFORMATION, *PFILE_NAMES_INFORMATION;

typedef struct _FILE_INTERNAL_INFORMATION
{
  LARGE_INTEGER IndexNumber;
} FILE_INTERNAL_INFORMATION, *PFILE_INTERNAL_INFORMATION;

typedef struct _FILE_EA_INFORMATION
{
  ULONG EaSize;
} FILE_EA_INFORMATION, *PFILE_EA_INFORMATION;

typedef struct _FILE_ACCESS_INFORMATION
{
  ACCESS_MASK AccessFlags;
} FILE_ACCESS_INFORMATION, *PFILE_ACCESS_INFORMATION;

typedef struct _FILE_POSITION_INFORMATION
{
  LARGE_INTEGER CurrentByteOffset;
} FILE_POSITION_INFORMATION, *PFILE_POSITION_INFORMATION;

typedef struct _FILE_MODE_INFORMATION
{
  ULONG Mode;
} FILE_MODE_INFORMATION, *PFILE_MODE_INFORMATION;

typedef struct _FILE_ALIGNMENT_INFORMATION
{
  ULONG AlignmentRequirement;
} FILE_ALIGNMENT_INFORMATION, *PFILE_ALIGNMENT_INFORMATION;

typedef struct _FILE_NAME_INFORMATION
{
  ULONG FileNameLength;
  WCHAR FileName[1];
} FILE_NAME_INFORMATION, *PFILE_NAME_INFORMATION;

typedef struct _FILE_ID_EXTD_DIR_INFORMATION
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
  ULONG ReparsePointTag;
  FILE_ID_128 FileId;
  WCHAR FileName[1];
} FILE_ID_EXTD_DIR_INFORMATION, *PFILE_ID_EXTD_DIR_INFORMATION;

typedef struct _FILE_ID_EXTD_BOTH_DIR_INFORMATION
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
  ULONG ReparsePointTag;
  FILE_ID_128 FileId;
  CCHAR ShortNameLength;
  WCHAR ShortName[12];
  WCHAR FileName[1];
} FILE_ID_EXTD_BOTH_DIR_INFORMATION, *PFILE_ID_EXTD_BOTH_DIR_INFORMATION;

typedef struct _FILE_ID_64_EXTD_DIR_INFORMATION
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
  ULONG ReparsePointTag;
  LARGE_INTEGER FileId;
  WCHAR FileName[1];
} FILE_ID_64_EXTD_DIR_INFORMATION, *PFILE_ID_64_EXTD_DIR_INFORMATION;

typedef struct _FILE_ID_64_EXTD_BOTH_DIR_INFORMATION
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
  ULONG ReparsePointTag;
  LARGE_INTEGER FileId;
  CCHAR ShortNameLength;
  WCHAR ShortName[12];
  WCHAR FileName[1];
} FILE_ID_64_EXTD_BOTH_DIR_INFORMATION, *PFILE_ID_64_EXTD_BOTH_DIR_INFORMATION;

typedef struct _FILE_ID_ALL_EXTD_DIR_INFORMATION
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
  ULONG ReparsePointTag;
  LARGE_INTEGER FileId;
  FILE_ID_128 FileId128;
  WCHAR FileName[1];
} FILE_ID_ALL_EXTD_DIR_INFORMATION, *PFILE_ID_ALL_EXTD_DIR_INFORMATION;

typedef struct _FILE_ID_ALL_EXTD_BOTH_DIR_INFORMATION
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
  ULONG ReparsePointTag;
  LARGE_INTEGER FileId;
  FILE_ID_128 FileId128;
  CCHAR ShortNameLength;
  WCHAR ShortName[12];
  WCHAR FileName[1];
} FILE_ID_ALL_EXTD_BOTH_DIR_INFORMATION, *PFILE_ID_ALL_EXTD_BOTH_DIR_INFORMATION;

typedef struct _FILE_ALL_INFORMATION
{
  FILE_BASIC_INFORMATION BasicInformation;
  FILE_STANDARD_INFORMATION StandardInformation;
  FILE_INTERNAL_INFORMATION InternalInformation;
  FILE_EA_INFORMATION EaInformation;
  FILE_ACCESS_INFORMATION AccessInformation;
  FILE_POSITION_INFORMATION PositionInformation;
  FILE_MODE_INFORMATION ModeInformation;
  FILE_ALIGNMENT_INFORMATION AlignmentInformation;
  FILE_NAME_INFORMATION NameInformation;
} FILE_ALL_INFORMATION, *PFILE_ALL_INFORMATION;

typedef struct _FILE_OBJECTID_INFORMATION
{
  LONGLONG FileReference;
  UCHAR ObjectId[16];
  union
  {
    struct
    {
      UCHAR BirthVolumeId[16];
      UCHAR BirthObjectId[16];
      UCHAR DomainId[16];
    };
    UCHAR ExtendedInfo[48];
  };
} FILE_OBJECTID_INFORMATION, *PFILE_OBJECTID_INFORMATION;

typedef struct _FILE_REPARSE_POINT_INFORMATION
{
  LONGLONG FileReference;
  ULONG Tag;
} FILE_REPARSE_POINT_INFORMATION, *PFILE_REPARSE_POINT_INFORMATION;

// copied from ntstatus.h
#define STATUS_SUCCESS ((NTSTATUS)0x00000000L)
#define STATUS_BUFFER_OVERFLOW ((NTSTATUS)0x80000005L)
#define STATUS_NO_MORE_FILES ((NTSTATUS)0x80000006L)
#define STATUS_INFO_LENGTH_MISMATCH ((NTSTATUS)0xC0000004L)
#define STATUS_NO_SUCH_FILE ((NTSTATUS)0xC000000FL)

#define SL_RESTART_SCAN 0x01
#define SL_RETURN_SINGLE_ENTRY 0x02
#define SL_INDEX_SPECIFIED 0x04
#define SL_RETURN_ON_DISK_ENTRIES_ONLY 0x08

#define SL_QUERY_DIRECTORY_MASK 0x0b

typedef enum _FILE_INFORMATION_CLASS
{
  FileDirectoryInformation              = 1,
  FileFullDirectoryInformation          = 2,
  FileBothDirectoryInformation          = 3,
  FileStandardInformation               = 5,
  FileNameInformation                   = 9,
  FileRenameInformation                 = 10,
  FileNamesInformation                  = 12,
  FileAllInformation                    = 18,
  FileObjectIdInformation               = 29,
  FileReparsePointInformation           = 33,
  FileIdBothDirectoryInformation        = 37,
  FileIdFullDirectoryInformation        = 38,
  FileNormalizedNameInformation         = 48,
  FileIdExtdDirectoryInformation        = 60,
  FileIdExtdBothDirectoryInformation    = 63,
  FileId64ExtdDirectoryInformation      = 78,
  FileId64ExtdBothDirectoryInformation  = 79,
  FileIdAllExtdDirectoryInformation     = 80,
  FileIdAllExtdBothDirectoryInformation = 81
} FILE_INFORMATION_CLASS,
    *PFILE_INFORMATION_CLASS;

typedef enum _MODE
{
  KernelMode,
  UserMode,
  MaximumMode
} MODE;

typedef struct _IO_STATUS_BLOCK IO_STATUS_BLOCK;

typedef struct _IO_STATUS_BLOCK* PIO_STATUS_BLOCK;
// typedef VOID (NTAPI *PIO_APC_ROUTINE )(__in PVOID ApcContext, __in
// PIO_STATUS_BLOCK IoStatusBlock, __in ULONG Reserved);
typedef VOID(NTAPI* PIO_APC_ROUTINE)(PVOID ApcContext, PIO_STATUS_BLOCK IoStatusBlock,
                                     ULONG Reserved);
typedef enum _FILE_INFORMATION_CLASS FILE_INFORMATION_CLASS;

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

typedef enum _POOL_TYPE
{
  NonPagedPool,
  NonPagedPoolExecute = NonPagedPool,
  PagedPool,
  NonPagedPoolMustSucceed = NonPagedPool + 2,
  DontUseThisType,
  NonPagedPoolCacheAligned = NonPagedPool + 4,
  PagedPoolCacheAligned,
  NonPagedPoolCacheAlignedMustS = NonPagedPool + 6,
  MaxPoolType,
  NonPagedPoolBase                     = 0,
  NonPagedPoolBaseMustSucceed          = NonPagedPoolBase + 2,
  NonPagedPoolBaseCacheAligned         = NonPagedPoolBase + 4,
  NonPagedPoolBaseCacheAlignedMustS    = NonPagedPoolBase + 6,
  NonPagedPoolSession                  = 32,
  PagedPoolSession                     = NonPagedPoolSession + 1,
  NonPagedPoolMustSucceedSession       = PagedPoolSession + 1,
  DontUseThisTypeSession               = NonPagedPoolMustSucceedSession + 1,
  NonPagedPoolCacheAlignedSession      = DontUseThisTypeSession + 1,
  PagedPoolCacheAlignedSession         = NonPagedPoolCacheAlignedSession + 1,
  NonPagedPoolCacheAlignedMustSSession = PagedPoolCacheAlignedSession + 1,
  NonPagedPoolNx                       = 512,
  NonPagedPoolNxCacheAligned           = NonPagedPoolNx + 4,
  NonPagedPoolSessionNx                = NonPagedPoolNx + 32
} POOL_TYPE;

typedef struct _OBJECT_TYPE_INITIALIZER
{
  WORD Length;
  UCHAR ObjectTypeFlags;
  ULONG CaseInsensitive : 1;
  ULONG UnnamedObjectsOnly : 1;
  ULONG UseDefaultObject : 1;
  ULONG SecurityRequired : 1;
  ULONG MaintainHandleCount : 1;
  ULONG MaintainTypeList : 1;
  ULONG ObjectTypeCode;
  ULONG InvalidAttributes;
  GENERIC_MAPPING GenericMapping;
  ULONG ValidAccessMask;
  POOL_TYPE PoolType;
  ULONG DefaultPagedPoolCharge;
  ULONG DefaultNonPagedPoolCharge;
  PVOID DumpProcedure;
  LONG* OpenProcedure;
  PVOID CloseProcedure;
  PVOID DeleteProcedure;
  LONG* ParseProcedure;
  LONG* SecurityProcedure;
  LONG* QueryNameProcedure;
  UCHAR* OkayToCloseProcedure;
} OBJECT_TYPE_INITIALIZER, *POBJECT_TYPE_INITIALIZER;

typedef CCHAR KPROCESSOR_MODE;

typedef struct _OBJECT_HANDLE_INFORMATION
{
  ULONG HandleAttributes;
  ACCESS_MASK GrantedAccess;
} OBJECT_HANDLE_INFORMATION, *POBJECT_HANDLE_INFORMATION;

typedef struct _OBJECT_NAME_INFORMATION
{
  UNICODE_STRING Name;
} OBJECT_NAME_INFORMATION, *POBJECT_NAME_INFORMATION;

typedef enum _OBJECT_INFORMATION_CLASS
{
  ObjectBasicInformation = 0,
  ObjectNameInformation  = 1,
  ObjectTypeInformation  = 2
} OBJECT_INFORMATION_CLASS;

typedef struct _RTL_RELATIVE_NAME
{
  UNICODE_STRING RelativeName;
  HANDLE ContainingDirectory;
  void* CurDirRef;
} RTL_RELATIVE_NAME, *PRTL_RELATIVE_NAME;

typedef struct _FILE_NETWORK_OPEN_INFORMATION
{
  LARGE_INTEGER CreationTime;
  LARGE_INTEGER LastAccessTime;
  LARGE_INTEGER LastWriteTime;
  LARGE_INTEGER ChangeTime;
  LARGE_INTEGER AllocationSize;
  LARGE_INTEGER EndOfFile;
  ULONG FileAttributes;
} FILE_NETWORK_OPEN_INFORMATION, *PFILE_NETWORK_OPEN_INFORMATION;

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
#define FILE_OPEN_REQUIRING_OPLOCK 0x00010000
#define FILE_DISALLOW_EXCLUSIVE 0x00020000
#define FILE_SESSION_AWARE 0x00040000
#define FILE_RESERVE_OPFILTER 0x00100000
#define FILE_OPEN_REPARSE_POINT 0x00200000
#define FILE_OPEN_NO_RECALL 0x00400000
#define FILE_OPEN_FOR_FREE_SPACE_QUERY 0x00800000
#define FILE_CONTAINS_EXTENDED_CREATE_INFORMATION 0x10000000

// Nt

using NtQueryDirectoryFile_type   = NTSTATUS(WINAPI*)(HANDLE, HANDLE, PIO_APC_ROUTINE,
                                                    PVOID, PIO_STATUS_BLOCK, PVOID,
                                                    ULONG, FILE_INFORMATION_CLASS,
                                                    BOOLEAN, PUNICODE_STRING, BOOLEAN);
using NtQueryDirectoryFileEx_type = NTSTATUS(WINAPI*)(HANDLE, HANDLE, PIO_APC_ROUTINE,
                                                      PVOID, PIO_STATUS_BLOCK, PVOID,
                                                      ULONG, FILE_INFORMATION_CLASS,
                                                      ULONG, PUNICODE_STRING);

using NtQueryFullAttributesFile_type =
    NTSTATUS(WINAPI*)(POBJECT_ATTRIBUTES, PFILE_NETWORK_OPEN_INFORMATION);
using NtQueryAttributesFile_type = NTSTATUS(WINAPI*)(POBJECT_ATTRIBUTES,
                                                     PFILE_BASIC_INFORMATION);

using NtQueryObject_type = NTSTATUS(WINAPI*)(
    HANDLE Handle, OBJECT_INFORMATION_CLASS ObjectInformationClass,
    PVOID ObjectInformation, ULONG ObjectInformationLength, PULONG ReturnLength);
using NtQueryInformationFile_type = NTSTATUS(WINAPI*)(
    HANDLE FileHandle, PIO_STATUS_BLOCK IoStatusBlock, PVOID FileInformation,
    ULONG Length, FILE_INFORMATION_CLASS FileInformationClass);

using NtQueryInformationByName_type = NTSTATUS(WINAPI*)(
    HANDLE FileHandle, PIO_STATUS_BLOCK IoStatusBlock, PVOID FileInformation,
    ULONG Length, FILE_INFORMATION_CLASS FileInformationClass);

using NtOpenFile_type   = NTSTATUS(WINAPI*)(PHANDLE, ACCESS_MASK, POBJECT_ATTRIBUTES,
                                          PIO_STATUS_BLOCK, ULONG, ULONG);
using NtCreateFile_type = NTSTATUS(WINAPI*)(PHANDLE, ACCESS_MASK, POBJECT_ATTRIBUTES,
                                            PIO_STATUS_BLOCK, PLARGE_INTEGER, ULONG,
                                            ULONG, ULONG, ULONG, PVOID, ULONG);

using NtClose_type = NTSTATUS(WINAPI*)(HANDLE);

using NtTerminateProcess_type = NTSTATUS(WINAPI*)(HANDLE ProcessHandle,
                                                  NTSTATUS ExitStatus);

// Rtl

using RtlDoesFileExists_U_type = NTSYSAPI BOOLEAN(NTAPI*)(PCWSTR);
using RtlDosPathNameToRelativeNtPathName_U_WithStatus_type =
    NTSTATUS(NTAPI*)(PCWSTR DosFileName, PUNICODE_STRING NtFileName, PWSTR* FilePath,
                     PRTL_RELATIVE_NAME RelativeName);
using RtlReleaseRelativeName_type = void(NTAPI*)(PRTL_RELATIVE_NAME RelativeName);
using RtlGetVersion_type          = NTSTATUS(NTAPI*)(PRTL_OSVERSIONINFOW);

extern NtQueryDirectoryFile_type NtQueryDirectoryFile;
extern NtQueryDirectoryFileEx_type NtQueryDirectoryFileEx;
extern NtQueryFullAttributesFile_type NtQueryFullAttributesFile;
extern NtQueryAttributesFile_type NtQueryAttributesFile;
extern NtQueryObject_type NtQueryObject;
extern NtQueryInformationFile_type NtQueryInformationFile;
extern NtQueryInformationByName_type NtQueryInformationByName;
extern NtOpenFile_type NtOpenFile;
extern NtCreateFile_type NtCreateFile;
extern NtClose_type NtClose;
extern NtTerminateProcess_type NtTerminateProcess;
extern RtlDoesFileExists_U_type RtlDoesFileExists_U;
extern RtlDosPathNameToRelativeNtPathName_U_WithStatus_type
    RtlDosPathNameToRelativeNtPathName_U_WithStatus;
extern RtlReleaseRelativeName_type RtlReleaseRelativeName;
extern RtlGetVersion_type RtlGetVersion;

// ensures ntdll functions have been initialized (only needed during static objects
// initialization)
void ntdll_declarations_init();

#pragma warning(pop)
