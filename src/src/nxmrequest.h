#ifndef FLUORINE_NXMREQUEST_H
#define FLUORINE_NXMREQUEST_H

#include <QString>

#include <optional>

struct NxmRequest
{
  enum class Kind
  {
    RepositoryFile,
    DirectDownload,
  };

  Kind kind = Kind::RepositoryFile;
  QString target;

  // Classifies the two URL shapes accepted by the Linux desktop handler.
  // Repository links retain their original spelling; mod.pub direct links
  // carry the decoded target from their `url` query item.
  static std::optional<NxmRequest> parse(const QString& message);
};

#endif  // FLUORINE_NXMREQUEST_H
