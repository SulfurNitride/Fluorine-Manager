/*
Mod Organizer archive handling - Linux compatibility header

Copyright (C) 2024 MO2 Team. All rights reserved.

This header provides Windows COM compatibility types for Linux builds.
On Windows, the real Windows headers are used instead.
*/

#ifndef ARCHIVE_COMPAT_H
#define ARCHIVE_COMPAT_H

#ifdef _WIN32

#include <Unknwn.h>
#include <atlbase.h>
#include <initguid.h>
#include <guiddef.h>
#include <PropIdl.h>

#else // Linux

#include <Common/MyWindows.h>
#include <cstring>

// PropVariantInit / PropVariantClear - map to VariantClear from 7zip SDK
inline HRESULT PropVariantInit(PROPVARIANT* pvar) {
  pvar->vt = VT_EMPTY;
  return S_OK;
}

inline HRESULT PropVariantClear(PROPVARIANT* pvar) {
  return VariantClear(pvar);
}

// A minimal CComPtr replacement for Linux
template <class T>
class CComPtr
{
public:
  CComPtr() : p(nullptr) {}
  CComPtr(T* lp) : p(lp) { if (p) p->AddRef(); }
  CComPtr(const CComPtr<T>& other) : p(other.p) { if (p) p->AddRef(); }
  ~CComPtr() { Release(); }

  CComPtr& operator=(T* lp) {
    if (lp) lp->AddRef();
    Release();
    p = lp;
    return *this;
  }

  CComPtr& operator=(const CComPtr<T>& other) {
    if (this != &other) {
      if (other.p) other.p->AddRef();
      Release();
      p = other.p;
    }
    return *this;
  }

  void Release() {
    if (p) {
      p->Release();
      p = nullptr;
    }
  }

  T* Detach() {
    T* pt = p;
    p = nullptr;
    return pt;
  }

  operator T*() const { return p; }
  T* operator->() const { return p; }
  T** operator&() { return &p; }
  bool operator!() const { return p == nullptr; }
  bool operator==(std::nullptr_t) const { return p == nullptr; }
  bool operator!=(std::nullptr_t) const { return p != nullptr; }

  T* p;
};

#endif // _WIN32

#endif // ARCHIVE_COMPAT_H
