#ifndef ENVARS_H
#define ENVARS_H

#include <QMap>
#include <QString>

namespace CustomEnvVars
{

// Shell-like `KEY=value` key validation: letter/`_` start,
// alphanumerics/`_` only. Mirrors the semantics used by
// ProtonLauncher's wrapper parsing.
bool isValidEnvKey(const QString& key);

// Split a single `KEY=value` token on the first `=`. Returns false when
// there is no `=` or the key is invalid. `FOO=` yields an empty value.
bool parseEnvAssignment(const QString& token, QString& keyOut, QString& valueOut);

// Parse a single-line custom env var string, e.g.
// `FOO=bar BAZ='lorem ipsum'`, using shell-like quoting semantics
// (single- and double-quoted spans group spaces; quotes are stripped).
// This matches `QProcess::splitCommand` for double quotes while also
// supporting single quotes, which `splitCommand` does not group mid-token.
// Later duplicate keys overwrite earlier ones.
//
// Returns true on success (including empty/whitespace-only input, which
// yields an empty map). On failure returns false and sets `errorToken` to
// the first offending token for the block-launch dialog.
bool parseCustomEnvVars(const QString& raw, QMap<QString, QString>& out,
                        QString& errorToken);

}  // namespace CustomEnvVars

#endif  // ENVARS_H
