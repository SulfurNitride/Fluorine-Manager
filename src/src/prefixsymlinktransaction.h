#pragma once

#include <QString>
#include <QVector>

#include <functional>

namespace PrefixSymlinkTransaction {

struct Candidate {
  QString prefixPath;
  QString name;
};

struct Result {
  bool success{false};
  int created{0};
  int adopted{0};
  int preserved{0};
  QString error;
};

struct Options {
  // Production leaves this disabled. Focused tests use it to prove that a
  // terminal commit failure rolls back only links created by this call.
  int failAfterCreations{-1};
  std::function<void()> beforePublicationForTesting;
  std::function<void(const QString &)> afterSymlinkCallForTesting;
  std::function<void(const QString &)> afterSymlinkPublicationForTesting;
  std::function<void(const QString &)> afterCreationForTesting;
};

[[nodiscard]] Result apply(const QString &prefixPath,
                           const QVector<Candidate> &rankedCandidates,
                           const Options &options = {});

[[nodiscard]] bool ensureTempDirectory(const QString &prefixPath,
                                       QString &error);
[[nodiscard]] bool ensureTempDirectory(const QString &prefixPath,
                                       QString &error, const Options &options);

} // namespace PrefixSymlinkTransaction
