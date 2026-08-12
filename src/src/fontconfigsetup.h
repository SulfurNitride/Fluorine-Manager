#pragma once

#include <QProcessEnvironment>
#include <QString>

namespace FontconfigSetup
{

enum class State
{
  Active,
  Disabled,
  Unavailable,
};

struct Result
{
  State state{State::Unavailable};
  QString configPath;
  bool generated{false};
};

// Resolves the directory of the running executable before QApplication exists.
// Linux's /proc identity is authoritative even when argv[0] is only a PATH
// basename; argv[0] and the current directory remain compatibility fallbacks.
QString applicationDirectory(const QString& argv0);

// Configures fontconfig before QApplication can initialize its font database.
// A packaged adjacent config is preferred; the embedded copy is written only
// as a compatibility fallback for unpackaged, writable build trees.
Result configure(const QString& appDir);

// Restores the Fontconfig environment captured before Fluorine activated its
// private config, and removes the private snapshot from the child environment.
void restoreCallerEnvironment(QProcessEnvironment& environment);

}  // namespace FontconfigSetup
