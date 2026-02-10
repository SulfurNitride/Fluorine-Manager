#pragma once

#define USVFS_VERSION_MAJOR 0
#define USVFS_VERSION_MINOR 5
#define USVFS_VERSION_BUILD 7
#define USVFS_VERSION_REVISION 2

#define USVFS_BUILD_STRING ""
#define USVFS_BUILD_WSTRING L""

#ifndef USVFS_STRINGIFY
#define USVFS_STRINGIFY(x) USVFS_STRINGIFY_HELPER(x)
#define USVFS_STRINGIFY_HELPER(x) #x
#endif

#ifndef USVFS_STRINGIFYW
#define USVFS_STRINGIFYW(x) USVFS_STRINGIFYW_HELPER(x)
#define USVFS_STRINGIFYW_HELPER(x) L## #x
#endif

#define USVFS_VERSION_STRING                                                           \
  USVFS_STRINGIFY(USVFS_VERSION_MAJOR)                                                 \
  "." USVFS_STRINGIFY(USVFS_VERSION_MINOR) "." USVFS_STRINGIFY(                        \
      USVFS_VERSION_BUILD) "." USVFS_STRINGIFY(USVFS_VERSION_REVISION)                 \
      USVFS_BUILD_STRING
#define USVFS_VERSION_WSTRING                                                          \
  USVFS_STRINGIFYW(USVFS_VERSION_MAJOR)                                                \
  L"." USVFS_STRINGIFYW(USVFS_VERSION_MINOR) L"." USVFS_STRINGIFYW(                    \
      USVFS_VERSION_BUILD) L"." USVFS_STRINGIFYW(USVFS_VERSION_REVISION)               \
      USVFS_BUILD_WSTRING
