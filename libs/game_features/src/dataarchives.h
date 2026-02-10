#ifndef DATAARCHIVES
#define DATAARCHIVES

namespace MOBase { class IProfile; }

class QString;
class QStringList;

class DataArchives {

public:

  virtual ~DataArchives() {}

  virtual QStringList vanillaArchives() const = 0;

  virtual QStringList archives(const MOBase::IProfile *profile) const = 0;

  /**
   * @brief add an archive to the archive list
   * @param profile the profile for which to change the archive list
   * @param index index to insert before. 0 is the beginning of the list, INT_MAX can be used for the end of the list
   * @param archiveName the archive to add
   */
  virtual void addArchive(MOBase::IProfile *profile, int index, const QString &archiveName) = 0;

  virtual void removeArchive(MOBase::IProfile *profile, const QString &archiveName) = 0;

};

#endif // DATAARCHIVES

