#ifndef MODREPOSITORYFILEINFO_H
#define MODREPOSITORYFILEINFO_H

#include "versioninfo.h"
#include <QDateTime>
#include <QString>
#include <QVariantMap>

namespace MOBase
{
enum EFileCategory
{
  TYPE_UNKNOWN = 0,
  TYPE_MAIN,
  TYPE_UPDATE,
  TYPE_OPTION
};

class QDLLEXPORT ModRepositoryFileInfo : public QObject
{
  Q_OBJECT

public:
  ModRepositoryFileInfo(const ModRepositoryFileInfo& reference);
  ModRepositoryFileInfo(QString gameName = "", int modID = 0, int fileID = 0);
  QString toString() const;

  static ModRepositoryFileInfo createFromJson(const QString& data);
  QString name;
  QString uri;
  QString description;
  VersionInfo version;
  VersionInfo newestVersion;
  int categoryID;
  QString modName;
  QString gameName;
  QString nexusKey;
  int modID;
  int fileID;
  int nexusExpires;
  int nexusDownloadUser;
  size_t fileSize;
  QString fileName;
  int fileCategory;
  QDateTime fileTime;
  QString repository;

  QVariantMap userData;

  QString author;
  QString uploader;
  QString uploaderUrl;
};
}  // namespace MOBase

#endif  // MODREPOSITORYFILEINFO_H
