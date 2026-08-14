#ifndef FLUORINE_COMMANDLINEARGUMENTS_H
#define FLUORINE_COMMANDLINEARGUMENTS_H

#include <QStringList>
#include <QStringView>

#include <optional>

namespace cl
{

inline constexpr qsizetype MaximumForwardedArguments = 4096;

struct ProcessArgumentPartition
{
  QStringList managerArguments;
  QStringList invocation;
};

// Decode the semantic Unix process arguments (argv[1..]) without losing token
// boundaries. Arguments that cannot round-trip through Qt's system encoding
// are rejected instead of being silently replaced.
std::optional<QStringList> decodeUnixArguments(int argc, char* const argv[],
                                               QString* error = nullptr);

// Split native arguments at the first command/executable token. This keeps
// manager global options conventional while making every later token opaque to
// the manager parser for old-style executable launches.
ProcessArgumentPartition
partitionProcessArguments(const QStringList& arguments);

// Forwarded commands use a structured envelope because shell-like text cannot
// represent every argv vector (notably empty arguments) losslessly.
bool isForwardedArgumentsMessage(QStringView message);
std::optional<QString> encodeForwardedArguments(const QStringList& arguments);
std::optional<QStringList> decodeForwardedArguments(QStringView message);

}  // namespace cl

#endif
