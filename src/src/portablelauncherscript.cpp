#include "portablelauncherscript.h"

#include <QDir>
#include <QFileInfo>
#include <QSaveFile>

namespace portable_launcher_script
{
namespace
{

constexpr auto script = R"SCRIPT(#!/usr/bin/env bash
set -euo pipefail

SELF="$(readlink -f -- "$0")"
INSTANCE_DIR="$(CDPATH= cd -- "$(dirname -- "$SELF")" && pwd -P)"

MANAGER="$(type -P fluorine-manager 2>/dev/null || true)"
if [[ -z "$MANAGER" || ! -x "$MANAGER" ]]; then
    FALLBACK="${HOME:-}/.local/share/fluorine/bin/fluorine-manager"
    if [[ -n "${HOME:-}" && -x "$FALLBACK" ]]; then
        MANAGER="$FALLBACK"
    else
        echo "ERROR: fluorine-manager launcher was not found on PATH or in the user installation." >&2
        echo "Install Fluorine Manager, or add its launcher directory to PATH." >&2
        exit 127
    fi
fi

exec "$MANAGER" --instance "$INSTANCE_DIR" "$@"
)SCRIPT";

Result failed(const QString& path, const QString& operation,
              const QString& detail)
{
  return {Status::Failed, path,
          QStringLiteral("%1 '%2': %3").arg(operation, path, detail)};
}

}

Result create(const QString& instanceDirectory)
{
  const QString path = QDir(instanceDirectory).filePath(
      QStringLiteral("ModOrganizer.sh"));
  const QFileInfo existing(path);
  if (existing.exists() || existing.isSymLink()) {
    return {Status::Preserved, path, {}};
  }

  QSaveFile output(path);
  if (!output.open(QIODevice::WriteOnly)) {
    return failed(path, QStringLiteral("Cannot create launcher"),
                  output.errorString());
  }

  const QByteArray contents(script);
  if (output.write(contents) != contents.size()) {
    const QString detail = output.errorString();
    output.cancelWriting();
    return failed(path, QStringLiteral("Cannot write launcher"), detail);
  }

  const auto permissions =
      QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner |
      QFileDevice::ReadGroup | QFileDevice::ExeGroup | QFileDevice::ReadOther |
      QFileDevice::ExeOther;
  if (!output.setPermissions(permissions)) {
    const QString detail = output.errorString();
    output.cancelWriting();
    return failed(path, QStringLiteral("Cannot make launcher executable"),
                  detail);
  }

  if (!output.commit()) {
    return failed(path, QStringLiteral("Cannot commit launcher"),
                  output.errorString());
  }

  return {Status::Created, path, {}};
}

}
