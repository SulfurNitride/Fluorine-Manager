#ifndef EXECUTABLEINFO_H
#define EXECUTABLEINFO_H

#include "dllimport.h"
#include <QDir>
#include <QFileInfo>
#include <QList>
#include <QString>

namespace MOBase
{

class QDLLEXPORT ExecutableForcedLoadSetting
{
public:
  ExecutableForcedLoadSetting(const QString& process, const QString& library);

  ExecutableForcedLoadSetting& withForced(bool forced = true);

  ExecutableForcedLoadSetting& withEnabled(bool enabled = true);

  bool enabled() const;
  bool forced() const;
  QString library() const;
  QString process() const;

private:
  bool m_Enabled;
  QString m_Process;
  QString m_Library;
  bool m_Forced;
};

class QDLLEXPORT ExecutableInfo
{
public:
  ExecutableInfo(const QString& title, const QFileInfo& binary);

  ExecutableInfo& withArgument(const QString& argument);

  ExecutableInfo& withWorkingDirectory(const QDir& workingDirectory);

  ExecutableInfo& withSteamAppId(const QString& appId);

  ExecutableInfo& asCustom();

  bool isValid() const;

  QString title() const;
  QFileInfo binary() const;
  QStringList arguments() const;
  QDir workingDirectory() const;
  QString steamAppID() const;
  bool isCustom() const;

private:
  QString m_Title;
  QFileInfo m_Binary;
  QStringList m_Arguments;
  QDir m_WorkingDirectory;
  QString m_SteamAppID;
  bool m_Custom{false};
};

}  // namespace MOBase

#endif  // EXECUTABLEINFO_H
