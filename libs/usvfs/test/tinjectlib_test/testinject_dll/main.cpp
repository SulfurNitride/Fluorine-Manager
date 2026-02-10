#include "main.h"

void __cdecl InitNoQuit(LPVOID, size_t)
{
  // nop
}

void __cdecl InitNoParam(LPVOID, size_t)
{
  ExitProcess(10001);
}

void __cdecl InitComplexParam(LPVOID userData, size_t)
{
  LPCWSTR string = (LPCWSTR)userData;
  if (wcscmp(string, L"magic_parameter") == 0) {
    ExitProcess(10002);
  } else {
    ExitProcess(20003);
  }
}

BOOL APIENTRY DllMain(HMODULE, DWORD reasonForCall, LPVOID)
{
  switch (reasonForCall) {
  case DLL_PROCESS_ATTACH: {
  } break;
  case DLL_PROCESS_DETACH: {
  } break;
  case DLL_THREAD_ATTACH: {
  } break;
  case DLL_THREAD_DETACH: {
  } break;
  }
  return TRUE;
}
