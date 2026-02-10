#include <uibase/modrepositoryfileinfo.h>
#include <uibase/json.h>

MOBase::ModRepositoryFileInfo::ModRepositoryFileInfo(
    const ModRepositoryFileInfo& reference)
    : QObject(reference.parent()), name(reference.name), uri(reference.uri),
      description(reference.description), version(reference.version),
      categoryID(reference.categoryID), modName(reference.modName),
      gameName(reference.gameName), modID(reference.modID), fileID(reference.fileID),
      fileSize(reference.fileSize), fileCategory(reference.fileCategory),
      repository(reference.repository), userData(reference.userData),
      author(reference.author), uploader(reference.uploader),
      uploaderUrl(reference.uploaderUrl)
{}

MOBase::ModRepositoryFileInfo::ModRepositoryFileInfo(QString gameName, int modID,
                                                     int fileID)
    : name(), uri(), description(), version(), categoryID(0), modName(),
      gameName(gameName), modID(modID), fileID(fileID), fileSize(0),
      fileCategory(TYPE_UNKNOWN), repository(), userData(), author(), uploader(),
      uploaderUrl()

{}

MOBase::ModRepositoryFileInfo
MOBase::ModRepositoryFileInfo::createFromJson(const QString& data)
{
  QVariantList result = QtJson::parse(data).toList();

  while (result.length() < 18) {
    result.append(QVariant());
  }

  ModRepositoryFileInfo newInfo;

  newInfo.gameName = result.at(0).toString();
  newInfo.fileID   = result.at(1).toInt();
  newInfo.name     = result.at(2).toString();
  newInfo.uri      = result.at(3).toString();
  newInfo.version.parse(result.at(4).toString());
  newInfo.description = result.at(5).toString();
  newInfo.categoryID  = result.at(6).toInt();
  newInfo.fileSize    = result.at(7).toUInt();
  newInfo.modID       = result.at(8).toInt();
  newInfo.modName     = result.at(9).toString();
  newInfo.newestVersion.parse(result.at(10).toString());
  newInfo.fileName     = result.at(11).toString();
  newInfo.fileCategory = result.at(12).toInt();
  newInfo.repository   = result.at(13).toString();
  newInfo.userData     = result.at(14).toMap();
  newInfo.author       = result.at(15).toString();
  newInfo.uploader     = result.at(16).toString();
  newInfo.uploaderUrl  = result.at(17).toString();

  return newInfo;
}

QString MOBase::ModRepositoryFileInfo::toString() const
{
  return QString("[ "
                 "\"%1\",%2,\"%3\",\"%4\",\"%5\",\"%6\",%7,%8,%9,\"%10\",\"%11\",\"%"
                 "12\",%13,\"%14\",%15 ]")
      .arg(gameName)
      .arg(fileID)
      .arg(name)
      .arg(uri)
      .arg(version.canonicalString())
      .arg(description.mid(0).replace("\"", "'"))
      .arg(categoryID)
      .arg(fileSize)
      .arg(modID)
      .arg(modName)
      .arg(newestVersion.canonicalString())
      .arg(fileName)
      .arg(fileCategory)
      .arg(repository)
      .arg(QString(QtJson::serialize(userData)))
      .arg(author)
      .arg(uploader)
      .arg(uploaderUrl);
}
