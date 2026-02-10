/*
 * Copyright (C) 2013 Sebastian Herbord. All rights reserved.
 *
 * This file is part of the basic diagnosis plugin for Mod Organizer
 *
 * This plugin is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This plugin is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this plugin.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "diagnosebasic.h"

#include <uibase/ifiletree.h>
#include <uibase/imodinterface.h>
#include <uibase/imodlist.h>
#include <uibase/iplugingame.h>
#include <uibase/ipluginlist.h>
#include <uibase/report.h>
#include <uibase/utility.h>

#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QLabel>
#include <QMessageBox>
#include <QProgressDialog>
#include <QPushButton>
#include <QtPlugin>

#include <algorithm>
#include <functional>
#include <regex>
#include <vector>

using namespace MOBase;

const QRegularExpression DiagnoseBasic::RE_LOG_FILE(".*[.]log[0-9]*$");
const unsigned int DiagnoseBasic::PROBLEM_ERRORLOG;
const unsigned int DiagnoseBasic::PROBLEM_OVERWRITE;
const unsigned int DiagnoseBasic::PROBLEM_INVALIDFONT;
const unsigned int DiagnoseBasic::PROBLEM_NITPICKINSTALLED;
const unsigned int DiagnoseBasic::PROBLEM_PROFILETWEAKS;
const unsigned int DiagnoseBasic::PROBLEM_MISSINGMASTERS;
const unsigned int DiagnoseBasic::PROBLEM_ALTERNATE;
const unsigned int DiagnoseBasic::NUM_CONTEXT_ROWS;

DiagnoseBasic::DiagnoseBasic() : m_MOInfo(nullptr) {}

bool DiagnoseBasic::init(IOrganizer* moInfo)
{
  m_MOInfo = moInfo;

  m_MOInfo->modList()->onModStateChanged(
      [&](const std::map<QString, IModList::ModStates>& mods) {
        if (mods.contains("Overwrite"))
          invalidate();
      });
  m_MOInfo->modList()->onModMoved([&](const QString&, int, int) {
    // invalidates only the assetOrder check but there is currently no way to recheck
    // individual checks
    invalidate();
  });
  m_MOInfo->pluginList()->onPluginMoved([&](const QString&, int, int) {
    invalidate();
  });
  m_MOInfo->pluginList()->onRefreshed([&]() {
    invalidate();
  });
  m_MOInfo->pluginList()->onPluginStateChanged([&](auto const&) {
    invalidate();
  });
  m_MOInfo->onAboutToRun([&](const QString& executable) {
    return fileAttributes(executable);
  });

  return true;
}

QString DiagnoseBasic::name() const
{
  return "Basic diagnosis plugin";
}

QString DiagnoseBasic::author() const
{
  return "Tannin";
}

QString DiagnoseBasic::description() const
{
  return tr("Checks for problems unrelated to other plugins");
}

VersionInfo DiagnoseBasic::version() const
{
  return VersionInfo(1, 1, 3, VersionInfo::RELEASE_FINAL);
}

bool DiagnoseBasic::isActive() const
{
  return true;
}

QList<PluginSetting> DiagnoseBasic::settings() const
{
  return QList<PluginSetting>()
         << PluginSetting(
                "check_errorlog",
                tr("Warn when an error occurred last time an application was run"),
                true)
         << PluginSetting("check_overwrite",
                          tr("Warn when there are files in the overwrite directory"),
                          true)
         << PluginSetting("check_font",
                          tr("Warn when the font configuration refers to files that "
                             "aren't installed"),
                          true)
         << PluginSetting(
                "check_conflict",
                tr("Warn when mods are installed that conflict with MO functionality"),
                true)
         << PluginSetting("check_missingmasters",
                          tr("Warn when there are esps with missing masters"), true)
         << PluginSetting(
                "check_alternategames",
                tr("Warn when an installed mod came from an alternative game source"),
                false)
         << PluginSetting("check_fileattributes",
                          tr("Warn when files have unwanted attributes"), false)
         << PluginSetting(
                "ow_ignore_empty",
                tr("Ignore empty directories when checking overwrite directory"), false)
         << PluginSetting("ow_ignore_log",
                          tr("Ignore .log files and empty directories when checking "
                             "overwrite directory"),
                          false);
}

bool DiagnoseBasic::errorReported() const
{
  QDir dir(qApp->property("dataPath").toString() + "/logs");
  QFileInfoList files =
      dir.entryInfoList(QStringList("ModOrganizer_??_??_??_??_??.log"), QDir::Files,
                        QDir::Name | QDir::Reversed);

  if (files.count() > 0) {
    QString logFile = files.at(0).absoluteFilePath();
    QFile file(logFile);
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
      char buffer[1024];
      int line = 0;
      qint64 lineLengths[NUM_CONTEXT_ROWS];
      for (int i = 0; i < NUM_CONTEXT_ROWS; ++i) {
        lineLengths[i] = 0;
      }
      while (!file.atEnd()) {
        lineLengths[line % NUM_CONTEXT_ROWS] = file.readLine(buffer, 1024) + 1;
        if (strncmp(buffer, "ERROR", 5) == 0) {
          qint64 sumChars = 0;
          for (int i = 0; i < NUM_CONTEXT_ROWS; ++i) {
            sumChars += lineLengths[i];
          }
          file.seek(file.pos() - sumChars);
          m_ErrorMessage = "";
          for (int i = 0; i < 2 * NUM_CONTEXT_ROWS; ++i) {
            file.readLine(buffer, 1024);
            QString lineString = QString::fromUtf8(buffer);
            if (lineString.startsWith("ERROR")) {
              m_ErrorMessage += "<b>" + lineString + "</b>";
            } else {
              m_ErrorMessage += lineString;
            }
          }
          return true;
        }

        // prevent this function from taking forever
        if (line++ >= 50000) {
          break;
        }
      }
    }
  }

  return false;
}

bool DiagnoseBasic::checkEmpty(QString const& path) const
{
  QDir dir(path);
  dir.setFilter(QDir::Files | QDir::Hidden | QDir::System);

  // Search files first
  for (auto const& file : dir.entryList()) {
    if (!m_MOInfo->pluginSetting(name(), "ow_ignore_log").toBool() ||
        !RE_LOG_FILE.match(file).hasMatch()) {
      return false;
    }
  }

  // Then directories
  dir.setFilter(QDir::AllDirs | QDir::NoDotAndDotDot | QDir::NoSymLinks);
  for (QFileInfo const& subdir : dir.entryInfoList()) {
    if (!checkEmpty(subdir.absoluteFilePath())) {
      return false;
    }
  }

  return true;
}

bool DiagnoseBasic::overwriteFiles() const
{
  // QString dirname(qApp->property("dataPath").toString() + "/overwrite");
  QString dirname(m_MOInfo->overwritePath());
  if (m_MOInfo->pluginSetting(name(), "ow_ignore_empty").toBool() ||
      m_MOInfo->pluginSetting(name(), "ow_ignore_log").toBool()) {
    return !checkEmpty(dirname);
  }
  QDir dir(dirname);
  bool checkDirs = m_MOInfo->managedGame()->getModMappings().keys().size() > 1 ||
                   m_MOInfo->managedGame()->getModMappings().keys().first() != "";
  if (checkDirs) {
    bool empty = true;
    for (auto dir : m_MOInfo->managedGame()->getModMappings().keys()) {
      auto mapDir = QDir(dirname).filePath(dir);
      if (QDir(mapDir).exists()) {
        empty = QDir(mapDir).count() == 2;  // account for . and ..
      }
      if (!empty)
        break;
    }
    return !empty;
  }
  return dir.count() != 2;  // account for . and ..
}

bool DiagnoseBasic::nitpickInstalled() const
{
  QString path = m_MOInfo->resolvePath("skse/plugins/nitpick.dll");

  return !path.isEmpty();
}

/// unused code to remove duplicates from a vector
template <typename T>
void makeUnique(std::vector<T>& vector)
{
  std::set<T> done;

  auto read  = vector.begin();
  auto write = vector.begin();

  for (; read != vector.end(); ++read) {
    if (done.insert(*read).second) {
      *write = *read;
      ++write;
    }
  }

  vector.erase(write, vector.end());
}

bool DiagnoseBasic::missingMasters() const
{
  std::set<QString> enabledPlugins;

  QStringList esps = m_MOInfo->findFiles("", [](const QString& fileName) -> bool {
    return fileName.endsWith(".esp", FileNameComparator::CaseSensitivity) ||
           fileName.endsWith(".esm", FileNameComparator::CaseSensitivity) ||
           fileName.endsWith(".esl", FileNameComparator::CaseSensitivity);
  });
  // gather enabled masters first
  for (const QString& esp : esps) {
    QString baseName = QFileInfo(esp).fileName();
    if (m_MOInfo->pluginList()->state(baseName) == IPluginList::STATE_ACTIVE) {
      enabledPlugins.insert(baseName.toLower());
    }
  }

  m_MissingMasters.clear();
  m_PluginChildren.clear();
  // for each required master in each esp, test if it's in the list of enabled masters.
  for (const QString& esp : esps) {
    QString baseName = QFileInfo(esp).fileName();
    if (m_MOInfo->pluginList()->state(baseName) == IPluginList::STATE_ACTIVE) {
      for (const QString master : m_MOInfo->pluginList()->masters(baseName)) {
        if (enabledPlugins.find(master.toLower()) == enabledPlugins.end()) {
          m_MissingMasters.insert(master);
          m_PluginChildren[master].insert(baseName);
        }
      }
    }
  }
  return !m_MissingMasters.empty();
}

bool DiagnoseBasic::alternateGame() const
{
  QStringList mods = m_MOInfo->modList()->allMods();
  for (QString mod : mods) {
    if (m_MOInfo->modList()->state(mod) & MOBase::IModList::STATE_ALTERNATE &&
        m_MOInfo->modList()->state(mod) & MOBase::IModList::STATE_ACTIVE)
      return true;
  }
  return false;
}

bool DiagnoseBasic::invalidFontConfig() const
{
  if ((m_MOInfo->managedGame()->gameName() != "Skyrim") &&
      (m_MOInfo->managedGame()->gameShortName() != "SkyrimSE")) {
    // this check is only for skyrim
    return false;
  }

  // files from skyrim_interface.bsa
  static std::vector<QString> defaultFonts{"interface\\fonts_console.swf",
                                           "interface\\fonts_en.swf",
                                           "interface\\fonts_cclub.swf"};

  QString configPath = m_MOInfo->resolvePath("interface/fontconfig.txt");
  if (configPath.isEmpty()) {
    return false;
  }
  QFile config(configPath);
  if (!config.open(QIODevice::ReadOnly | QIODevice::Text)) {
    qDebug("failed to open %s", qUtf8Printable(configPath));
    return false;
  }

  std::regex exp("^fontlib \"([^\"]*)\"$");
  while (!config.atEnd()) {
    QByteArray row = config.readLine();
    std::cmatch match;
    if (std::regex_search(row.constData(), match, exp)) {
      std::string temp = match[1];
      QString path(temp.c_str());
      bool isDefault = false;
      for (const QString& def : defaultFonts) {
        if (QString::compare(def, path, FileNameComparator::CaseSensitivity) == 0) {
          isDefault = true;
          break;
        }
      }

      if (!isDefault && m_MOInfo->resolvePath(path).isEmpty()) {
        return true;
      }
    }
  }
  return false;
}

static bool checkFileAttributes(const QString& path)
{
#ifdef _WIN32
  WCHAR w_path[32767];
  memset(w_path, 0, sizeof(w_path));
  path.toWCharArray(w_path);

  DWORD attrs = GetFileAttributes(w_path);
  if (attrs != INVALID_FILE_ATTRIBUTES) {
    if (!(attrs & FILE_ATTRIBUTE_ARCHIVE) && !(attrs & FILE_ATTRIBUTE_NORMAL) &&
        (attrs & ~(FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_ARCHIVE))) {
      QString debug;
      debug += QString("%1 ").arg(attrs, 8, 16, QLatin1Char('0'));

      // A C D H I O P R S U V X Z
      debug += (attrs & FILE_ATTRIBUTE_DIRECTORY) ? "D" : " ";
      debug += (attrs & FILE_ATTRIBUTE_ARCHIVE) ? "A" : " ";
      debug += (attrs & FILE_ATTRIBUTE_READONLY) ? "R" : " ";
      debug += (attrs & FILE_ATTRIBUTE_SYSTEM) ? "S" : " ";
      debug += (attrs & FILE_ATTRIBUTE_HIDDEN) ? "H" : " ";
      debug += (attrs & FILE_ATTRIBUTE_OFFLINE) ? "O" : " ";
      debug += (attrs & FILE_ATTRIBUTE_NOT_CONTENT_INDEXED) ? "I" : " ";
      debug += (attrs & FILE_ATTRIBUTE_NO_SCRUB_DATA) ? "X" : " ";
      debug += (attrs & FILE_ATTRIBUTE_INTEGRITY_STREAM) ? "V" : " ";
      debug += (attrs & FILE_ATTRIBUTE_PINNED) ? "P" : " ";
      debug += (attrs & FILE_ATTRIBUTE_UNPINNED) ? "U" : " ";
      debug += (attrs & FILE_ATTRIBUTE_COMPRESSED) ? "C" : " ";
      debug += (attrs & FILE_ATTRIBUTE_SPARSE_FILE) ? "Z" : " ";

      debug += QString(" %1").arg(path);

      qDebug() << debug;

      return true;
    }
  } else {
    DWORD error = ::GetLastError();
    qWarning(qUtf8Printable(QString("Unable to get file attributes for %1 (error %2)")
                                .arg(w_path)
                                .arg(error)));
  }
  return false;
#else
  Q_UNUSED(path);
  return false;
#endif
}

static bool fixFileAttributes(const QString& path)
{
#ifdef _WIN32
  bool success = true;

  WCHAR w_path[32767];
  memset(w_path, 0, sizeof(w_path));
  path.toWCharArray(w_path);

  DWORD attrs = GetFileAttributes(w_path);
  if (attrs != INVALID_FILE_ATTRIBUTES) {
    // Clear all the attributes possible, except ARCHIVE, with SetFileAttributes
    if (!SetFileAttributes(
            w_path, attrs & FILE_ATTRIBUTE_ARCHIVE ? FILE_ATTRIBUTE_ARCHIVE : 0)) {
      DWORD error = GetLastError();
      qWarning(qUtf8Printable(QString("Unable to set file attributes for %1 (error %2)")
                                  .arg(path)
                                  .arg(error)));
      success = false;
    }

    // Compression requires DeviceIoControl
    if (attrs & FILE_ATTRIBUTE_COMPRESSED) {
      HANDLE hndl = CreateFile(w_path, GENERIC_READ | GENERIC_WRITE,
                               FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
                               FILE_ATTRIBUTE_NORMAL, NULL);
      if (hndl != INVALID_HANDLE_VALUE) {
        USHORT compressionSetting = COMPRESSION_FORMAT_NONE;
        DWORD bytesReturned       = 0;
        if (!DeviceIoControl(hndl, FSCTL_SET_COMPRESSION, &compressionSetting,
                             sizeof(compressionSetting), NULL, 0, &bytesReturned,
                             NULL)) {
          DWORD error = GetLastError();
          qWarning(qUtf8Printable(
              QString("Unable to disable compression for file %1 (error %2)")
                  .arg(path)
                  .arg(error)));
          success = false;
        }
        CloseHandle(hndl);
      } else {
        DWORD error = GetLastError();
        qWarning(qUtf8Printable(
            QString("Unable to open file %1 (error %2)").arg(path).arg(error)));
        success = false;
      }
    }

    // Sparseness requires DeviceIoControl
    if (attrs & FILE_ATTRIBUTE_SPARSE_FILE) {
      HANDLE hndl = CreateFile(w_path, GENERIC_READ | GENERIC_WRITE,
                               FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
                               FILE_ATTRIBUTE_NORMAL, NULL);
      if (hndl != INVALID_HANDLE_VALUE) {
        FILE_SET_SPARSE_BUFFER setting = {FALSE};
        DWORD bytesReturned            = 0;
        if (!DeviceIoControl(hndl, FSCTL_SET_SPARSE, &setting, sizeof(setting), NULL, 0,
                             &bytesReturned, NULL)) {
          DWORD error = GetLastError();
          qWarning(qUtf8Printable(
              QString("Unable to disable sparseness for file %1 (error %2)")
                  .arg(path)
                  .arg(error)));
          success = false;
        }
        CloseHandle(hndl);
      } else {
        DWORD error = GetLastError();
        qWarning(qUtf8Printable(
            QString("Unable to open file %1 (error %2)").arg(path).arg(error)));
        success = false;
      }
    }

    // As a last ditch effort, set the archive flag
    if (!success) {
      attrs = GetFileAttributes(w_path);
      if (attrs != INVALID_FILE_ATTRIBUTES) {
        if (!SetFileAttributes(w_path, attrs | FILE_ATTRIBUTE_ARCHIVE)) {
          DWORD error = GetLastError();
          qWarning(
              qUtf8Printable(QString("Unable to set file attributes for %1 (error %2)")
                                 .arg(path)
                                 .arg(error)));
        } else {
          success = true;
        }
      } else {
        DWORD error = GetLastError();
        qWarning(
            qUtf8Printable(QString("Unable to get file attributes for %1 (error %2)")
                               .arg(path)
                               .arg(error)));
      }
    }
  } else {
    DWORD error = GetLastError();
    qWarning(qUtf8Printable(QString("Unable to get file attributes for %1 (error %2)")
                                .arg(path)
                                .arg(error)));
    success = false;
  }

  return success;
#else
  Q_UNUSED(path);
  return true;
#endif
}

bool DiagnoseBasic::fileAttributes(const QString& executable) const
{
  if (!m_MOInfo->pluginSetting(name(), "check_fileattributes").toBool())
    return true;

  QStringList filesToFix;
  QStringList directoriesToSearch;

  // Search the game directory for problems
  directoriesToSearch << m_MOInfo->managedGame()->dataDirectory().absolutePath();

  // Find the active mods to search them too
  for (QString mod : m_MOInfo->modList()->allMods()) {
    if (m_MOInfo->modList()->state(mod) & MOBase::IModList::STATE_ACTIVE) {
      directoriesToSearch << m_MOInfo->modList()->getMod(mod)->absolutePath();
    }
  }

  // Find the subdirectories of all the directories
  for (int i = 0; i < directoriesToSearch.length(); i++) {
    for (QString dir :
         QDir(directoriesToSearch[i])
             .entryList(QDir::Hidden | QDir::AllDirs | QDir::NoDotAndDotDot)) {
      directoriesToSearch << directoriesToSearch[i] + "\\" + dir;
    }
  }

  // Set up a progress bar since this can take a while
  QPushButton* progressButton = new QPushButton("Cancel");
  progressButton->setEnabled(false);

  QLabel* progressLabel = new QLabel;
  progressLabel->setText(tr("File attribute checker\nSearching for problems..."));
  progressLabel->setAlignment(Qt::AlignCenter);

  QProgressDialog dialog;
  dialog.setWindowModality(Qt::ApplicationModal);
  dialog.setCancelButton(progressButton);
  dialog.setLabel(progressLabel);
  dialog.setMinimumDuration(1);
  dialog.show();
  dialog.setMaximum(directoriesToSearch.length());

  // Find problems with the directories and files
  for (int i = 0; i < directoriesToSearch.length(); i++) {
    QString* dirPath  = &directoriesToSearch[i];
    QString fixedPath = QString("\\\\?\\%1").arg(QDir::toNativeSeparators(*dirPath));
    if (checkFileAttributes(fixedPath))
      filesToFix << fixedPath;

    for (QString file : QDir(*dirPath).entryList(QDir::Hidden | QDir::AllEntries |
                                                 QDir::NoDotAndDotDot)) {
      fixedPath =
          QString("\\\\?\\%1").arg(QDir::toNativeSeparators(*dirPath + "\\" + file));
      if (checkFileAttributes(fixedPath))
        filesToFix << fixedPath;
    }
    dialog.setValue(i);
  }

  if (filesToFix.length() == 0) {
    return true;
  }

  QMessageBox::StandardButton userResponse =
      QMessageBox::question(nullptr, tr("Invalid file attributes found"),
                            tr("One or more of your files has attributes that may "
                               "prevent the game from reading the files. "
                               "This can result in missing plugins, missing textures, "
                               "and other such problems.\n\n"
                               "Fix the file attributes?"),
                            QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);
  if (userResponse == QMessageBox::Cancel) {
    qDebug() << "User canceled executable launch due to file attributes";
    return false;
  } else if (userResponse == QMessageBox::No) {
    return true;
  }

  // Reset progress bar
  progressLabel->setText(tr("File attribute checker\nFixing file attributes..."));
  dialog.setValue(0);
  dialog.setMaximum(filesToFix.length());

  // Start iterating through files fixing problems
  bool success = true;
  for (int i = 0; i < filesToFix.length(); i++) {
    if (!fixFileAttributes(filesToFix[i]))
      success = false;
    dialog.setValue(i);
  }

  if (!success) {
    if (QMessageBox::question(nullptr, tr("Unable to set file attributes"),
                              tr("Mod Organizer was unable to fix the file attributes "
                                 "of at least one file.\n\n"
                                 "Continue launching %1?")
                                  .arg(executable),
                              QMessageBox::Yes | QMessageBox::No) == QMessageBox::No) {
      return false;
    }
  }

  return true;
}

std::vector<unsigned int> DiagnoseBasic::activeProblems() const
{
  std::vector<unsigned int> result;

  if (m_MOInfo->pluginSetting(name(), "check_errorlog").toBool() && errorReported()) {
    result.push_back(PROBLEM_ERRORLOG);
  }
  if (m_MOInfo->pluginSetting(name(), "check_overwrite").toBool() && overwriteFiles()) {
    result.push_back(PROBLEM_OVERWRITE);
  }
  if (m_MOInfo->pluginSetting(name(), "check_font").toBool() && invalidFontConfig()) {
    result.push_back(PROBLEM_INVALIDFONT);
  }
  if (m_MOInfo->pluginSetting(name(), "check_conflict").toBool() &&
      nitpickInstalled()) {
    result.push_back(PROBLEM_NITPICKINSTALLED);
  }
  if (m_MOInfo->pluginSetting(name(), "check_missingmasters").toBool() &&
      missingMasters()) {
    result.push_back(PROBLEM_MISSINGMASTERS);
  }
  if (m_MOInfo->pluginSetting(name(), "check_alternategames").toBool() &&
      alternateGame()) {
    result.push_back(PROBLEM_ALTERNATE);
  }
  if (QFile::exists(m_MOInfo->profilePath() + "/profile_tweaks.ini")) {
    result.push_back(PROBLEM_PROFILETWEAKS);
  }

  return result;
}

QString DiagnoseBasic::shortDescription(unsigned int key) const
{
  switch (key) {
  case PROBLEM_ERRORLOG:
    return tr("There was an error reported recently");
  case PROBLEM_OVERWRITE:
    return tr("There are files in your Overwrite mod directory");
  case PROBLEM_INVALIDFONT:
    return tr("Your font configuration may be broken");
  case PROBLEM_NITPICKINSTALLED:
    return tr("Nitpick installed");
  case PROBLEM_PROFILETWEAKS:
    return tr("INI Tweaks overwritten");
  case PROBLEM_MISSINGMASTERS:
    return tr("Missing Masters");
  case PROBLEM_ALTERNATE:
    return tr("At least one unverified mod is using an alternative game source");
  default:
    throw MyException(tr("invalid problem key %1").arg(key));
  }
}

QString DiagnoseBasic::fullDescription(unsigned int key) const
{
  switch (key) {
  case PROBLEM_ERRORLOG:
    return "<code>" + m_ErrorMessage.replace("\n", "<br>") + "</code>";
  case PROBLEM_OVERWRITE:
    return tr(
        "There are currently files in your <span style=\"color: "
        "red;\"><i>Overwrite</i></span> directory. These files are typically newly "
        "created files, usually generated by an external mod tool (i.e. Wrye Bash, "
        "xEdit, FNIS, ...). Creation Club ESL files will also end up here when "
        "downloaded. Any files in <span style=\"font-weight: bold;\">Overwrite</span> "
        "will take top priority when loading your mod files and will always overwrite "
        "any other mod in your profile.<br>"
        "<br>"
        "It is recommended that you review the files in <span style=\"font-weight: "
        "bold;\">Overwrite</span> and move any relevant files to a new or existing "
        "mod. You can do this by double-clicking the <span style=\"font-weight: "
        "bold;\">Overwrite</span> mod and dragging files from the Overwrite window to "
        "a mod entry in the main mod list. It is also possible to move all current "
        "<span style=\"font-weight: bold;\">Overwrite</span> files to a new mod by "
        "right-clicking on the Overwrite mod.<br>"
        "<br>"
        "Not all files in <span style=\"font-weight: bold;\">Overwrite</span> need to "
        "be removed, but there are several reasons to do so. Some generated files will "
        "be directly related to the active mods in your profile and will be "
        "incompatible with different mod setups. Since <span style=\"font-weight: "
        "bold;\">Overwrite</span> is always active, this could cause conflicts between "
        "profiles. Additionally, moving relevant game files into a normal mod will "
        "give you greater control over those files. Some files can live safely in "
        "<span style=\"font-weight: bold;\">Overwrite</span>, such as basic logs and "
        "cache files. It is up to you to understand how best to manage these files.<br>"
        "<br>"
        "If you do not wish to see this warning and understand how to handle your "
        "<span style=\"font-weight: bold;\">Overwrite</span> directory, you can open "
        "the Mod Organizer settings and disable this warning under the \"Diagnose "
        "Basic\" plugin configuration.");
  case PROBLEM_INVALIDFONT:
    return tr("Your current configuration seems to reference a font that is not "
              "installed. You may see only boxes instead of letters.<br>"
              "The font configuration is in Data\\interface\\fontconfig.txt. Most "
              "likely you have a broken installation of a font replacer mod.");
  case PROBLEM_NITPICKINSTALLED:
    return tr("You have the nitpick skse plugin installed. This plugin is not needed "
              "with Mod Organizer because MO already offers the same functionality. "
              "Worse: The two solutions may conflict so it's strongly suggested you "
              "remove this plugin.");
  case PROBLEM_PROFILETWEAKS: {
    QString fileContent = readFileText(m_MOInfo->profilePath() + "/profile_tweaks.ini");
    return tr("Settings provided in ini tweaks have been overwritten in-game or in an "
              "applications.<br>"
              "These overwrites are stored in a separate file "
              "(<i>profile_tweaks.ini</i> within the profile directory)<br>"
              "to keep ini-tweaks in their original state but you should really get "
              "rid of this file as there is<br>"
              "no tool support in MO to work on it. <br>"
              "Advice: Copy settings you want to keep to an appropriate ini tweak, "
              "then delete <i>profile_tweaks.ini</i>.<br>"
              "Hitting the <i>Fix</i> button will delete that file") +
           "<hr><i>profile_tweaks.ini:</i><pre>" + fileContent + "</pre>";
  } break;
  case PROBLEM_MISSINGMASTERS: {
    QString masterInfo;
    for (auto master : m_MissingMasters) {
      masterInfo += "<tr>";
      masterInfo += "<td style=\"padding-left: 20px\">" + master + "</td>";
      masterInfo += "<td style=\"padding-left: 20px\">" +
                    SetJoin(m_PluginChildren[master], ", ") + "</td>";
      masterInfo += "</tr>";
    }
    return tr("The masters for some plugins (esp/esl/esm) are not enabled.<br>"
              "The game will crash unless you install and enable the following "
              "plugins: ") +
           "<br/><table><tr>" + "<th style=\"padding-left: 20px; text-align: left\">" +
           tr("Master") + "</th>" +
           "<th style=\"padding-left: 20px; text-align: left\">" + tr("Required By") +
           "</th></tr>" + masterInfo + "</table>";
  } break;
  case PROBLEM_ALTERNATE: {
    return tr(
        "You have at least one active mod installed from an alternative game "
        "source.<br>"
        "This means that the mod was downloaded from a game source which does not "
        "match<br>"
        "the expected primary game.<br><br>"
        "Depending on the type of mod, this may require converting various files to "
        "run correctly.<br><br>"
        "Advice: Once you have verified the mod is working correctly, you can use the "
        "context menu<br>"
        "and select \"Mark as converted/working\" to remove the flag and warning.");
  } break;
  default:
    throw MyException(tr("invalid problem key %1").arg(key));
  }
}

bool DiagnoseBasic::hasGuidedFix(unsigned int key) const
{
  return (key == PROBLEM_PROFILETWEAKS);
}

void DiagnoseBasic::startGuidedFix(unsigned int key) const
{
  switch (key) {
  case PROBLEM_PROFILETWEAKS: {
    shellDeleteQuiet(m_MOInfo->profilePath() + "/profile_tweaks.ini");
  } break;
  default:
    throw MyException(tr("invalid problem key %1").arg(key));
  }
}
