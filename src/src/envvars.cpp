#include "envvars.h"

namespace CustomEnvVars
{

namespace
{

// Shell-like tokenizer: whitespace separates tokens unless inside single or
// double quotes. Quotes are stripped; inside double quotes a backslash
// escapes the next character, outside quotes it escapes any next character,
// inside single quotes everything is literal. Returns false on unbalanced
// quotes or a trailing escape.
bool splitEnvTokens(const QString& raw, QStringList& tokensOut)
{
  tokensOut.clear();
  QString current;
  bool inSingle = false;
  bool inDouble = false;
  bool inToken  = false;

  const int n = raw.size();
  for (int i = 0; i < n; ++i) {
    const QChar c = raw[i];
    if (inSingle) {
      if (c == QChar('\'')) {
        inSingle = false;
      } else {
        current += c;
      }
      continue;
    }
    if (inDouble) {
      if (c == QChar('"')) {
        inDouble = false;
      } else if (c == QChar('\\') && i + 1 < n) {
        current += raw[++i];
      } else {
        current += c;
      }
      continue;
    }
    if (c == QChar('\'')) {
      inSingle = true;
      inToken  = true;
    } else if (c == QChar('"')) {
      inDouble = true;
      inToken  = true;
    } else if (c == QChar('\\') && i + 1 < n) {
      current += raw[++i];
      inToken = true;
    } else if (c.isSpace()) {
      if (inToken) {
        tokensOut.append(current);
        current.clear();
        inToken = false;
      }
    } else {
      current += c;
      inToken = true;
    }
  }

  if (inSingle || inDouble) {
    return false;
  }
  // A trailing lone backslash is kept literally (matches splitCommand
  // tolerance) rather than failing the whole line.
  if (inToken) {
    tokensOut.append(current);
  }
  return true;
}

}  // namespace

bool isValidEnvKey(const QString& key)
{
  if (key.isEmpty()) {
    return false;
  }

  const QChar first = key.front();
  if (!(first.isLetter() || first == QChar('_'))) {
    return false;
  }

  for (const QChar c : key) {
    if (!(c.isLetterOrNumber() || c == QChar('_'))) {
      return false;
    }
  }

  return true;
}

bool parseEnvAssignment(const QString& token, QString& keyOut, QString& valueOut)
{
  const int eq = token.indexOf('=');
  if (eq <= 0) {
    return false;
  }

  const QString key = token.left(eq);
  if (!isValidEnvKey(key)) {
    return false;
  }

  keyOut   = key;
  valueOut = token.mid(eq + 1);
  return true;
}

bool parseCustomEnvVars(const QString& raw, QMap<QString, QString>& out,
                        QString& errorToken)
{
  out.clear();
  errorToken.clear();

  if (raw.trimmed().isEmpty()) {
    return true;
  }

  QStringList tokens;
  if (!splitEnvTokens(raw, tokens)) {
    // Unbalanced quote: block the launch with the raw input as context.
    errorToken = raw.trimmed();
    return false;
  }
  if (tokens.isEmpty()) {
    errorToken = raw.trimmed();
    return false;
  }

  for (const QString& token : tokens) {
    QString key;
    QString value;
    if (!parseEnvAssignment(token, key, value)) {
      errorToken = token;
      return false;
    }
    // Later duplicates overwrite earlier ones.
    out.insert(key, value);
  }

  return true;
}

}  // namespace CustomEnvVars
