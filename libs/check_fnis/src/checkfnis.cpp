#include "checkfnis.h"

#include <uibase/iplugingame.h>
#include <uibase/pluginsetting.h>
#include <uibase/questionboxmemory.h>
#include <uibase/report.h>
#include <uibase/scopeguard.h>

#include <QCryptographicHash>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMessageBox>
#include <QStringList>
#include <QVariant>

#include <Qt>       // for Qt::CaseInsensitive
#include <QtDebug>  // for qCritical, qDebug

#include <functional>

using namespace MOBase;

CheckFNIS::CheckFNIS()
    : m_MOInfo(nullptr), m_Active(false),
      m_MatchExpressions(std::vector<QRegularExpression>{
          // MSVC2013 bug. The (std::vector<QRegExp> shouldn't be necessary
          QRegularExpression("\\\\FNIS_.*_List\\.txt$",
                             QRegularExpression::PatternOption::CaseInsensitiveOption),
          QRegularExpression("\\\\FNIS.*Behavior\\.txt$",
                             QRegularExpression::PatternOption::CaseInsensitiveOption),
          QRegularExpression("\\\\PatchList\\.txt$",
                             QRegularExpression::PatternOption::CaseInsensitiveOption),
          QRegularExpression(
              "\\\\skeleton.*\\.hkx$",
              QRegularExpression::PatternOption::CaseInsensitiveOption)}),
      m_SensitiveMatchExpressions(std::vector<QRegularExpression>{
          QRegularExpression("\\\\animations\\\\.*\\.hkx$",
                             QRegularExpression::PatternOption::CaseInsensitiveOption)})
{}

CheckFNIS::~CheckFNIS() {}

bool CheckFNIS::init(IOrganizer* moInfo)
{
  m_MOInfo = moInfo;

  if (!moInfo->onAboutToRun([this](const auto& binary) {
        return fnisCheck(binary);
      })) {
    qCritical("failed to connect to about to run event");
    return false;
  }

  if (!moInfo->onFinishedRun(std::bind(&CheckFNIS::fnisEndCheck, this,
                                       std::placeholders::_1, std::placeholders::_2))) {
    qCritical("failed to connect to finished run event");
    return false;
  }

  return true;
}

QString CheckFNIS::name() const
{
  return "FNIS Checker";
}

QString CheckFNIS::localizedName() const
{
  return tr("FNIS Checker");
}

QString CheckFNIS::author() const
{
  return "Tannin";
}

QString CheckFNIS::description() const
{
  return tr("Checks if FNIS behaviours need to be updated whenever you start the game."
            " This is only relevant for Skyrim and if FNIS is installed.<br>");
}

VersionInfo CheckFNIS::version() const
{
  return VersionInfo(1, 0, 1, VersionInfo::RELEASE_FINAL);
}

std::vector<std::shared_ptr<const MOBase::IPluginRequirement>>
CheckFNIS::requirements() const
{
  return {Requirements::gameDependency("Skyrim")};
}

QList<PluginSetting> CheckFNIS::settings() const
{
  QList<PluginSetting> result;
  result.push_back(PluginSetting("sensitive",
                                 "check changes on non-fnis animations. Makes this "
                                 "more reliable but will cause FNIS to be called more "
                                 "often than necessary.",
                                 QVariant(false)));
  return result;
}

bool CheckFNIS::testFileRelevant(const IOrganizer::FileInfo& fileName) const
{
  if (!fileName.archive.isEmpty()) {
    return false;
  }

  for (auto& expr : m_MatchExpressions) {
    auto match = expr.match(fileName.filePath);
    if (match.hasMatch()) {
      return true;
    }
  }

  if (m_MOInfo->pluginSetting(name(), "sensitive").toBool()) {
    for (auto& expr : m_SensitiveMatchExpressions) {
      auto match = expr.match(fileName.filePath);
      if (match.hasMatch()) {
        return true;
      }
    }
  }

  return false;
}

void CheckFNIS::findRelevantFilesRecursive(const QString& path,
                                           QMap<QString, QString>& fileList) const
{
  // find all relevant files
  QList<IOrganizer::FileInfo> files = m_MOInfo->findFileInfos(
      path, std::bind(&CheckFNIS::testFileRelevant, this, std::placeholders::_1));
  foreach (const IOrganizer::FileInfo& fileInfo, files) {
    QFile file(fileInfo.filePath);
    if (file.open(QIODevice::ReadOnly)) {
      QString hash(
          QCryptographicHash::hash(file.readAll(), QCryptographicHash::Md5).toHex());
      fileList.insert(fileInfo.filePath, hash);
    } else {
      qCritical("failed to open %s", qUtf8Printable(fileInfo.filePath));
    }
  }

  QStringList subDirectories = m_MOInfo->listDirectories(path);
  foreach (const QString& directory, subDirectories) {
    findRelevantFilesRecursive(path + "\\" + directory, fileList);
  }
}

QString CheckFNIS::generateIdentifier() const
{
  QMap<QString, QString> fileList;

  findRelevantFilesRecursive("meshes\\actors", fileList);

  QStringList flattenedList;
  for (auto iter = fileList.begin(); iter != fileList.end(); ++iter) {
    flattenedList.append(iter.key() + "=" + iter.value());
  }

  return QCryptographicHash::hash(flattenedList.join(",").toUtf8(),
                                  QCryptographicHash::Md5)
      .toHex();
}

bool CheckFNIS::appIsFNIS(QString const& application, QString const& fnisApp)
{
  return QString::compare(QDir::fromNativeSeparators(application),
                          QDir::fromNativeSeparators(fnisApp),
                          Qt::CaseInsensitive) == 0;
}

QString CheckFNIS::getFnisPath() const
{
  if (!m_MOInfo->pluginSetting(name(), "enabled").toBool()) {
    return "";
  }

  // Check if it's actually installed (...)
  QStringList fnisBinaryList = m_MOInfo->findFiles(
      "tools/GenerateFNIS_for_Users", [](const QString& fileName) -> bool {
        return fileName.endsWith("GenerateFNISforUsers.exe", Qt::CaseInsensitive);
      });

  if (fnisBinaryList.count() == 0) {
    // fnis seems not to be installed even though this is enabled
    qDebug("fnis not installed");
    return "";
  }

  // As this is looking in the vfs, there can be precisely 0 or 1 instances of FNIS
  return fnisBinaryList.at(0);
}

bool CheckFNIS::fnisCheck(const QString& application)
{

  QString fnisApp(getFnisPath());
  if (fnisApp.isEmpty() || appIsFNIS(application, fnisApp)) {
    return true;
  }

  // prevent this check from being called recursively
  if (m_Active) {
    return true;
  }

  m_Active = true;
  ON_BLOCK_EXIT([&] {
    m_Active = false;
  });

  QString const newHash = generateIdentifier();

  if (newHash == m_MOInfo->persistent(name(), m_MOInfo->profileName(), "").toString()) {
    // Don't need to run fnis as nothing relevant has changed.
    return true;
  }

  QDialogButtonBox::StandardButton res = QuestionBoxMemory::query(
      nullptr, "fnisCheck", QFileInfo(application).fileName(),
      tr("Run FNIS before %1?").arg(application),
      tr("FNIS source data has been changed. You should run GenerateFNIS.exe now."),
      QDialogButtonBox::Yes | QDialogButtonBox::No | QDialogButtonBox::Cancel,
      QDialogButtonBox::Yes);

  if (res == QDialogButtonBox::Yes) {
    HANDLE process = m_MOInfo->startApplication(fnisApp);
    bool cont      = true;
    if (process == INVALID_HANDLE_VALUE) {
      reportError(tr("Failed to start %1").arg(fnisApp));
    } else {
      DWORD exitCodeU;
      if (m_MOInfo->waitForApplication(process, &exitCodeU)) {
        int exitCode = static_cast<int>(exitCodeU);
        if (exitCode != 0) {
          cont = QMessageBox::question(
                     nullptr, tr("Start %1?").arg(application),
                     tr("FNIS reported a %1, do you want to run the application "
                        "anyway?")
                         .arg(exitCode < 0 ? tr("warning") : tr("critical error")),
                     QMessageBox::Yes | QMessageBox::No,
                     QMessageBox::No) == QMessageBox::Yes;
        }
      } else {
        cont = QMessageBox::question(nullptr, tr("Start %1?").arg(application),
                                     tr("Failed to determine FNIS exit code, do you "
                                        "want to run the application "
                                        "anyway?"),
                                     QMessageBox::Yes | QMessageBox::No,
                                     QMessageBox::No) == QMessageBox::Yes;
      }
      if (cont) {
        m_MOInfo->setPersistent(name(), m_MOInfo->profileName(), newHash);
      }
    }
    return cont;
  } else if (res == QDialogButtonBox::No) {
    return true;
  } else {
    // Don't run the app if they pressed cancel
    return false;
  }
}

void CheckFNIS::fnisEndCheck(const QString& application, unsigned int code)
{
  if (appIsFNIS(application, getFnisPath())) {
    bool update  = true;
    int exitCode = static_cast<int>(code);
    if (exitCode != 0) {
      update =
          QMessageBox::question(
              nullptr, tr("Start %1?").arg(application),
              tr("FNIS reported a %1. Do you want to assume it worked?")
                  .arg(exitCode < 0 ? tr("warning") : tr("critical error")),
              QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::Yes;
    }
    if (update) {
      m_MOInfo->setPersistent(name(), m_MOInfo->profileName(), generateIdentifier());
    }
  }
}

#if QT_VERSION < QT_VERSION_CHECK(5, 0, 0)
Q_EXPORT_PLUGIN2(checkfnis, CheckFNIS)
#endif
