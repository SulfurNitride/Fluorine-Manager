#include "commandlinearguments.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>

#include <array>

namespace cl
{
namespace
{

constexpr QStringView ForwardedArgumentsPrefix =
    u"@fluorine-command-argv-v1@";

enum class OptionValue
{
  None,
  Required,
  Optional
};

struct LongOption
{
  QStringView name;
  OptionValue value;
};

// Mirrors CommandLine::createOptions(). Keeping this tiny grammar independent
// from Boost lets us stop before a bare executable without parsing its suffix.
constexpr std::array GlobalOptions{
    LongOption{u"help", OptionValue::None},
    LongOption{u"multiple", OptionValue::None},
    LongOption{u"pick", OptionValue::None},
    LongOption{u"logs", OptionValue::None},
    LongOption{u"instance", OptionValue::Optional},
    LongOption{u"profile", OptionValue::Required},
};

std::optional<OptionValue> longOptionValue(QStringView token)
{
  if (!token.startsWith(u"--") || token == u"--") {
    return std::nullopt;
  }

  QStringView name = token.sliced(2);
  const qsizetype equals = name.indexOf(u'=');
  if (equals >= 0) {
    name = name.first(equals);
  }
  if (name.isEmpty()) {
    return std::nullopt;
  }

  std::optional<OptionValue> match;
  for (const LongOption& option : GlobalOptions) {
    if (option.name.startsWith(name)) {
      if (match) {
        return std::nullopt;
      }
      match = option.value;
    }
  }
  return match;
}

bool hasInlineValue(QStringView token)
{
  return token.contains(u'=');
}

bool validArguments(const QStringList& arguments)
{
  if (arguments.isEmpty() ||
      arguments.size() > MaximumForwardedArguments) {
    return false;
  }
  for (const QString& argument : arguments) {
    if (argument.contains(QChar::Null)) {
      return false;
    }
  }
  return true;
}

}  // namespace

std::optional<QStringList> decodeUnixArguments(int argc, char* const argv[],
                                               QString* error)
{
  QStringList arguments;
  arguments.reserve(argc > 1 ? argc - 1 : 0);
  for (int i = 1; i < argc; ++i) {
    if (argv[i] == nullptr) {
      if (error != nullptr) {
        *error = QStringLiteral("argument %1 is null").arg(i);
      }
      return std::nullopt;
    }

    const QByteArray bytes(argv[i]);
    const QString decoded = QString::fromLocal8Bit(bytes);
    if (decoded.toLocal8Bit() != bytes || decoded.contains(QChar::Null)) {
      if (error != nullptr) {
        *error = QStringLiteral("argument %1 is not valid UTF-8").arg(i);
      }
      return std::nullopt;
    }
    arguments.push_back(decoded);
  }
  return arguments;
}

ProcessArgumentPartition
partitionProcessArguments(const QStringList& arguments)
{
  ProcessArgumentPartition result;
  qsizetype index = 0;
  while (index < arguments.size()) {
    const QStringView token(arguments.at(index));
    if (token == u"--") {
      ++index;
      break;
    }

    std::optional<OptionValue> value = longOptionValue(token);
    if (!value && token.startsWith(u'-') && !token.startsWith(u"--") &&
        token.size() >= 2) {
      if (token.at(1) == u'p') {
        value = OptionValue::Required;
      } else if (token.at(1) == u'i') {
        value = OptionValue::Optional;
      }
    }
    if (!value) {
      break;
    }

    result.managerArguments.push_back(arguments.at(index));
    const bool shortInline =
        token.startsWith(u'-') && !token.startsWith(u"--") &&
        token.size() > 2;
    if (*value == OptionValue::Required && !hasInlineValue(token) &&
        !shortInline && index + 1 < arguments.size()) {
      result.managerArguments.push_back(arguments.at(++index));
    } else if (*value == OptionValue::Optional && !hasInlineValue(token) &&
               !shortInline && index + 1 < arguments.size() &&
               !QStringView(arguments.at(index + 1)).startsWith(u'-')) {
      result.managerArguments.push_back(arguments.at(++index));
    }
    ++index;
  }

  result.invocation = arguments.sliced(index);
  return result;
}

bool isForwardedArgumentsMessage(QStringView message)
{
  return message.startsWith(ForwardedArgumentsPrefix);
}

std::optional<QString> encodeForwardedArguments(const QStringList& arguments)
{
  if (!validArguments(arguments)) {
    return std::nullopt;
  }

  QJsonArray array;
  for (const QString& argument : arguments) {
    array.append(argument);
  }
  return ForwardedArgumentsPrefix.toString() +
         QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact));
}

std::optional<QStringList> decodeForwardedArguments(QStringView message)
{
  if (!isForwardedArgumentsMessage(message)) {
    return std::nullopt;
  }

  QJsonParseError error;
  const QByteArray payload =
      message.sliced(ForwardedArgumentsPrefix.size()).toUtf8();
  const QJsonDocument document = QJsonDocument::fromJson(payload, &error);
  if (error.error != QJsonParseError::NoError || !document.isArray()) {
    return std::nullopt;
  }

  const QJsonArray array = document.array();
  if (array.isEmpty() || array.size() > MaximumForwardedArguments) {
    return std::nullopt;
  }

  QStringList arguments;
  arguments.reserve(array.size());
  for (const QJsonValue& value : array) {
    if (!value.isString()) {
      return std::nullopt;
    }
    const QString argument = value.toString();
    if (argument.contains(QChar::Null)) {
      return std::nullopt;
    }
    arguments.push_back(argument);
  }
  return arguments;
}

}  // namespace cl
