#include "pandorapreviousoutput.h"

#include <uibase/transactionalwritefile.h>

#include <QByteArray>
#include <QObject>

namespace PandoraPreviousOutput {
namespace {

QString normalizeText(const QString &text) {
  QString normalized;
  normalized.reserve(text.size());

  qsizetype start = 0;
  while (start < text.size()) {
    qsizetype ending = start;
    while (ending < text.size() && text[ending] != '\r' &&
           text[ending] != '\n') {
      ++ending;
    }

    QString line = text.sliced(start, ending - start);
    const qsizetype meshes =
        line.indexOf(QStringLiteral("\\meshes\\"), 0, Qt::CaseInsensitive);
    if (meshes >= 0) {
      line = line.first(meshes) + line.sliced(meshes).toLower();
    }
    normalized.append(line);

    if (ending < text.size()) {
      normalized.append(text[ending]);
      if (text[ending] == '\r' && ending + 1 < text.size() &&
          text[ending + 1] == '\n') {
        normalized.append('\n');
        ++ending;
      }
    }
    start = ending + 1;
  }
  return normalized;
}

} // namespace

Result normalize(const QString &path) {
  MOBase::TransactionalWriteFile transaction(path);
  QByteArray original;
  bool present = false;
  if (!transaction.readOriginal(original, present)) {
    return {Status::Failed, transaction.errorString()};
  }
  if (!present) {
    return {Status::Missing, {}};
  }

  const QString text = QString::fromUtf8(original);
  if (text.toUtf8() != original) {
    return {Status::Failed,
            QObject::tr("Pandora's PreviousOutput.txt is not valid UTF-8.")};
  }
  const QByteArray replacement = normalizeText(text).toUtf8();
  if (replacement == original) {
    return {Status::Unchanged, {}};
  }
  if (!transaction.replaceWith(replacement)) {
    return {Status::Failed, transaction.errorString()};
  }
  return {Status::Updated, {}};
}

} // namespace PandoraPreviousOutput
