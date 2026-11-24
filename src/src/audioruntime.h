#pragma once

#include <QString>

// Fluorine's X3DAudio1_7.dll audio copy guard. Prefixes use Microsoft's
// XACT/XAudio/X3DAudio DLLs from the DirectX June 2010 redistributable; the
// guard wraps Microsoft's 64-bit X3DAudio1_7.dll, which games load at start-up.

// Installs the guard over Microsoft's 64-bit X3DAudio1_7.dll, which is moved
// to x3daudio1_7_ms.dll for the guard to forward to. Idempotent. If Microsoft's
// DLL is not installed (Wine's builtin is in use), nothing is changed and
// `skipped` explains why. Returns false only on a missing bundle or I/O failure.
bool installAudioGuard(const QString& prefix, const QString& bundleDir, bool& changed,
                       QString& skipped, QString& error);

// True for prefixes set up by Fluorine 0.4.x with its bundled FAudio, which
// must be recreated to use Microsoft's audio runtime and the guard.
bool prefixUsesBundledFAudio(const QString& prefix);
