#include "curatedfomod.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <QSet>
#include <QXmlStreamReader>

#include <algorithm>

namespace
{
struct FileRule
{
  QString source;
  QString destination;
  int priority{0};
  int sequence{0};
  bool folder{false};
};

struct Condition
{
  enum class Kind { Always, And, Or, Flag, Unsupported } kind{Kind::Always};
  QString name;
  QString value;
  QVector<Condition> children;
};

struct Plugin
{
  QString name;
  QString type{"Optional"};
  QVector<FileRule> files;
  QMap<QString, QString> flags;
};

struct Group
{
  QString type;
  QVector<Plugin> plugins;
};

struct Step
{
  Condition visible;
  QVector<Group> groups;
};

struct ConditionalFiles
{
  Condition condition;
  QVector<FileRule> files;
};

struct Model
{
  QVector<FileRule> required;
  QVector<Step> steps;
  QVector<ConditionalFiles> conditional;
  int sequence{0};
};

QString archivePath(QString path)
{
  // FOMOD paths are Windows paths even when the installer runs on Linux.
  // QDir::fromNativeSeparators() intentionally leaves backslashes untouched
  // on Unix, so normalize the archive format explicitly first.
  path = path.trimmed();
  path.replace('\\', '/');
  return QDir::cleanPath(path);
}

bool safeRelative(const QString& path)
{
  if (path.isEmpty()) return true;
  const QString clean = archivePath(path);
  return QDir::isRelativePath(clean) && clean != ".." && !clean.startsWith("../");
}

void parseFiles(QXmlStreamReader& xml, QVector<FileRule>& result, Model& model)
{
  while (xml.readNextStartElement()) {
    const bool folder = xml.name().compare("folder", Qt::CaseInsensitive) == 0;
    const bool file = xml.name().compare("file", Qt::CaseInsensitive) == 0;
    if (!folder && !file) {
      xml.skipCurrentElement();
      continue;
    }
    FileRule rule;
    rule.source = archivePath(xml.attributes().value("source").toString());
    rule.destination = archivePath(xml.attributes().value("destination").toString());
    rule.priority = xml.attributes().value("priority").toInt();
    rule.sequence = model.sequence++;
    rule.folder = folder;
    result.push_back(rule);
    xml.skipCurrentElement();
  }
}

Condition parseDependencies(QXmlStreamReader& xml)
{
  Condition result;
  const QString op = xml.attributes().value("operator").toString();
  result.kind = op.compare("Or", Qt::CaseInsensitive) == 0
                    ? Condition::Kind::Or : Condition::Kind::And;
  while (xml.readNextStartElement()) {
    if (xml.name().compare("dependencies", Qt::CaseInsensitive) == 0) {
      result.children.push_back(parseDependencies(xml));
    } else if (xml.name().compare("flagDependency", Qt::CaseInsensitive) == 0) {
      Condition flag;
      flag.kind = Condition::Kind::Flag;
      flag.name = xml.attributes().value("flag").toString();
      flag.value = xml.attributes().value("value").toString();
      result.children.push_back(flag);
      xml.skipCurrentElement();
    } else {
      Condition unsupported;
      unsupported.kind = Condition::Kind::Unsupported;
      result.children.push_back(unsupported);
      xml.skipCurrentElement();
    }
  }
  return result;
}

bool evaluate(const Condition& condition, const QMap<QString, QString>& flags)
{
  switch (condition.kind) {
  case Condition::Kind::Always: return true;
  case Condition::Kind::Flag: return flags.value(condition.name) == condition.value;
  case Condition::Kind::Unsupported: return false;
  case Condition::Kind::And:
    return std::all_of(condition.children.begin(), condition.children.end(),
                       [&](const auto& child) { return evaluate(child, flags); });
  case Condition::Kind::Or:
    return std::any_of(condition.children.begin(), condition.children.end(),
                       [&](const auto& child) { return evaluate(child, flags); });
  }
  return false;
}

void parseFlags(QXmlStreamReader& xml, QMap<QString, QString>& flags)
{
  while (xml.readNextStartElement()) {
    if (xml.name().compare("flag", Qt::CaseInsensitive) != 0) {
      xml.skipCurrentElement();
      continue;
    }
    const QString name = xml.attributes().value("name").toString();
    flags.insert(name, xml.readElementText());
  }
}

void parseTypeDescriptor(QXmlStreamReader& xml, Plugin& plugin)
{
  while (xml.readNextStartElement()) {
    if (xml.name().compare("type", Qt::CaseInsensitive) == 0) {
      plugin.type = xml.attributes().value("name").toString();
      xml.skipCurrentElement();
    } else if (xml.name().compare("dependencyType", Qt::CaseInsensitive) == 0) {
      while (xml.readNextStartElement()) {
        if (xml.name().compare("defaultType", Qt::CaseInsensitive) == 0)
          plugin.type = xml.attributes().value("name").toString();
        xml.skipCurrentElement();
      }
    } else {
      xml.skipCurrentElement();
    }
  }
}

Plugin parsePlugin(QXmlStreamReader& xml, Model& model)
{
  Plugin plugin;
  plugin.name = xml.attributes().value("name").toString();
  while (xml.readNextStartElement()) {
    if (xml.name().compare("files", Qt::CaseInsensitive) == 0)
      parseFiles(xml, plugin.files, model);
    else if (xml.name().compare("conditionFlags", Qt::CaseInsensitive) == 0)
      parseFlags(xml, plugin.flags);
    else if (xml.name().compare("typeDescriptor", Qt::CaseInsensitive) == 0)
      parseTypeDescriptor(xml, plugin);
    else
      xml.skipCurrentElement();
  }
  return plugin;
}

Group parseGroup(QXmlStreamReader& xml, Model& model)
{
  Group group;
  group.type = xml.attributes().value("type").toString();
  while (xml.readNextStartElement()) {
    if (xml.name().compare("plugins", Qt::CaseInsensitive) != 0) {
      xml.skipCurrentElement();
      continue;
    }
    while (xml.readNextStartElement()) {
      if (xml.name().compare("plugin", Qt::CaseInsensitive) == 0)
        group.plugins.push_back(parsePlugin(xml, model));
      else
        xml.skipCurrentElement();
    }
  }
  return group;
}

Step parseStep(QXmlStreamReader& xml, Model& model)
{
  Step step;
  while (xml.readNextStartElement()) {
    if (xml.name().compare("visible", Qt::CaseInsensitive) == 0) {
      while (xml.readNextStartElement()) {
        if (xml.name().compare("dependencies", Qt::CaseInsensitive) == 0)
          step.visible = parseDependencies(xml);
        else
          xml.skipCurrentElement();
      }
    } else if (xml.name().compare("optionalFileGroups", Qt::CaseInsensitive) == 0) {
      while (xml.readNextStartElement()) {
        if (xml.name().compare("group", Qt::CaseInsensitive) == 0)
          step.groups.push_back(parseGroup(xml, model));
        else
          xml.skipCurrentElement();
      }
    } else {
      xml.skipCurrentElement();
    }
  }
  return step;
}

ConditionalFiles parseConditional(QXmlStreamReader& xml, Model& model)
{
  ConditionalFiles result;
  while (xml.readNextStartElement()) {
    if (xml.name().compare("dependencies", Qt::CaseInsensitive) == 0)
      result.condition = parseDependencies(xml);
    else if (xml.name().compare("files", Qt::CaseInsensitive) == 0)
      parseFiles(xml, result.files, model);
    else
      xml.skipCurrentElement();
  }
  return result;
}

bool parseModel(const QString& path, Model& model, QString& error)
{
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    error = "FOMOD ModuleConfig.xml could not be opened.";
    return false;
  }
  QXmlStreamReader xml(&file);
  while (xml.readNextStartElement()) {
    if (xml.name().compare("config", Qt::CaseInsensitive) != 0) {
      xml.skipCurrentElement();
      continue;
    }
    while (xml.readNextStartElement()) {
      if (xml.name().compare("requiredInstallFiles", Qt::CaseInsensitive) == 0) {
        parseFiles(xml, model.required, model);
      } else if (xml.name().compare("installSteps", Qt::CaseInsensitive) == 0) {
        while (xml.readNextStartElement()) {
          if (xml.name().compare("installStep", Qt::CaseInsensitive) == 0)
            model.steps.push_back(parseStep(xml, model));
          else
            xml.skipCurrentElement();
        }
      } else if (xml.name().compare("conditionalFileInstalls", Qt::CaseInsensitive) == 0) {
        while (xml.readNextStartElement()) {
          if (xml.name().compare("patterns", Qt::CaseInsensitive) != 0) {
            xml.skipCurrentElement();
            continue;
          }
          while (xml.readNextStartElement()) {
            if (xml.name().compare("pattern", Qt::CaseInsensitive) == 0)
              model.conditional.push_back(parseConditional(xml, model));
            else
              xml.skipCurrentElement();
          }
        }
      } else {
        xml.skipCurrentElement();
      }
    }
  }
  if (xml.hasError()) {
    error = QString("Invalid FOMOD XML: %1").arg(xml.errorString());
    return false;
  }
  return true;
}

QPair<QString, QString> findFomodRoot(const QString& source)
{
  QStringList configs;
  QDirIterator it(source, QDir::Files, QDirIterator::Subdirectories);
  while (it.hasNext()) {
    const QString path = it.next();
    const QFileInfo info(path);
    if (info.fileName().compare("ModuleConfig.xml", Qt::CaseInsensitive) == 0
        && info.dir().dirName().compare("fomod", Qt::CaseInsensitive) == 0)
      configs.push_back(path);
  }
  if (configs.isEmpty())
    return {{}, QString("No fomod/ModuleConfig.xml was found below %1").arg(source)};
  if (configs.size() != 1)
    return {{}, QString("Found %1 FOMOD ModuleConfig.xml files below %2; "
                        "the recipe must select one explicitly.")
                    .arg(configs.size()).arg(source)};
  QDir root = QFileInfo(configs.front()).dir();
  root.cdUp();
  return {root.absolutePath(), configs.front()};
}

bool copyOne(const QString& source, const QString& destination, QString& error)
{
  if (!QFileInfo::exists(source)) {
    error = QString("FOMOD source is missing: %1").arg(source);
    return false;
  }
  QDir().mkpath(QFileInfo(destination).absolutePath());
  if (QFileInfo::exists(destination) && !QFile::remove(destination)) {
    error = QString("Cannot replace FOMOD output: %1").arg(destination);
    return false;
  }
  if (!QFile::copy(source, destination)) {
    error = QString("Cannot copy FOMOD file %1").arg(source);
    return false;
  }
  return true;
}

bool applyRule(const FileRule& rule, const QString& root, const QString& output,
               int& installed, QString& error)
{
  if (!safeRelative(rule.source) || !safeRelative(rule.destination)) {
    error = "FOMOD contains an unsafe source or destination path.";
    return false;
  }
  const QString source = QDir(root).filePath(rule.source);
  if (!rule.folder) {
    QString target = rule.destination;
    if (target.isEmpty() || target.endsWith('/'))
      target = QDir(target).filePath(QFileInfo(source).fileName());
    if (!copyOne(source, QDir(output).filePath(target), error)) return false;
    ++installed;
    return true;
  }
  if (!QFileInfo(source).isDir()) {
    error = QString("FOMOD source folder is missing: %1").arg(source);
    return false;
  }
  QDirIterator it(source, QDir::Files, QDirIterator::Subdirectories);
  while (it.hasNext()) {
    const QString file = it.next();
    const QString relative = QDir(source).relativeFilePath(file);
    const QString target = QDir(output).filePath(QDir(rule.destination).filePath(relative));
    if (!copyOne(file, target, error)) return false;
    ++installed;
  }
  return true;
}
}

QPair<bool, QString> installCuratedFomod(const QString& source,
                                        const QString& destination,
                                        const QStringList& selectedPlugins)
{
  const auto discovered = findFomodRoot(source);
  if (discovered.first.isEmpty()) return {false, discovered.second};
  const QString root = discovered.first;
  const QString config = discovered.second;
  Model model;
  QString error;
  if (!parseModel(config, model, error)) return {false, error};

  QVector<FileRule> rules = model.required;
  QMap<QString, QString> flags;
  QSet<QString> requested;
  for (const auto& selection : selectedPlugins) requested.insert(selection.toCaseFolded());
  QSet<QString> matched;

  for (const auto& step : model.steps) {
    if (!evaluate(step.visible, flags)) continue;
    for (const auto& group : step.groups) {
      QVector<int> selected;
      for (int i = 0; i < group.plugins.size(); ++i) {
        const auto& plugin = group.plugins[i];
        if (requested.contains(plugin.name.toCaseFolded())) {
          selected.push_back(i);
          matched.insert(plugin.name.toCaseFolded());
        } else if (plugin.type.compare("Required", Qt::CaseInsensitive) == 0
                   || plugin.type.compare("Recommended", Qt::CaseInsensitive) == 0) {
          selected.push_back(i);
        }
      }
      if (group.type.compare("SelectAll", Qt::CaseInsensitive) == 0) {
        selected.clear();
        for (int i = 0; i < group.plugins.size(); ++i) selected.push_back(i);
      }
      const bool mustSelect = group.type.compare("SelectExactlyOne", Qt::CaseInsensitive) == 0
                              || group.type.compare("SelectAtLeastOne", Qt::CaseInsensitive) == 0;
      if (mustSelect && selected.isEmpty() && !group.plugins.isEmpty()) selected.push_back(0);
      if (group.type.compare("SelectExactlyOne", Qt::CaseInsensitive) == 0
          && selected.size() > 1) {
        // An explicit guide selection wins over an automatically recommended item.
        auto explicitIt = std::find_if(selected.begin(), selected.end(), [&](int index) {
          return requested.contains(group.plugins[index].name.toCaseFolded());
        });
        selected = {explicitIt == selected.end() ? selected.front() : *explicitIt};
      }
      for (const int index : selected) {
        const auto& plugin = group.plugins[index];
        rules += plugin.files;
        for (auto it = plugin.flags.cbegin(); it != plugin.flags.cend(); ++it)
          flags.insert(it.key(), it.value());
      }
    }
  }
  if (matched != requested) {
    QStringList missing;
    for (const auto& item : requested)
      if (!matched.contains(item)) missing.push_back(item);
    return {false, QString("Reviewed FOMOD choice was not found: %1").arg(missing.join(", "))};
  }
  for (const auto& conditional : model.conditional)
    if (evaluate(conditional.condition, flags)) rules += conditional.files;

  std::stable_sort(rules.begin(), rules.end(), [](const auto& left, const auto& right) {
    return left.priority == right.priority ? left.sequence < right.sequence
                                           : left.priority < right.priority;
  });
  QDir(destination).removeRecursively();
  QDir().mkpath(destination);
  int installed = 0;
  for (const auto& rule : rules)
    if (!applyRule(rule, root, destination, installed, error)) return {false, error};
  if (installed == 0) return {false, "The reviewed FOMOD selection produced no files."};
  return {true, QString::number(installed)};
}
