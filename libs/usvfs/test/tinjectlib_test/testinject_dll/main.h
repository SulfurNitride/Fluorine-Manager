#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

extern "C"
    __declspec(dllexport) void __cdecl InitNoQuit(LPVOID userData, size_t userDataSize);
extern "C" __declspec(dllexport) void __cdecl InitNoParam(LPVOID userData,
                                                          size_t userDataSize);
extern "C" __declspec(dllexport) void __cdecl InitComplexParam(LPVOID userData,
                                                               size_t userDataSize);
