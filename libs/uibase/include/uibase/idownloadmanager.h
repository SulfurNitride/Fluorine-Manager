#ifndef IDOWNLOADMANAGER_H
#define IDOWNLOADMANAGER_H

#include "dllimport.h"
#include <QList>
#include <QObject>
#include <QString>
#include <functional>

namespace MOBase
{

class QDLLEXPORT IDownloadManager : public QObject
{
  Q_OBJECT

public:
  IDownloadManager(QObject* parent = nullptr) : QObject(parent) {}

  /**
   * @brief download a file by url. The list can contain alternative URLs to allow the
   *        download manager to switch in case of download problems
   * @param urls list of urls to download from
   * @return an id by which the download will be identified
   */
  virtual int startDownloadURLs(const QStringList& urls) = 0;

  /**
   * @brief download a file from www.nexusmods.com/<game>. <game> is always the game
   * currently being managed
   * @param modID id of the mod for which to download a file
   * @param fileID id of the file to download
   * @return an id by which the download will be identified
   */
  virtual int startDownloadNexusFile(int modID, int fileID) = 0;

  /**
   * @brief download a file from www.nexusmods.com/<gameName>.
   * @param gameName 'short' name of the game the mod is for
   * @param modID id of the mod for which to download a file
   * @param fileID id of the file to download
   * @return an id by which the download will be identified
   */
  virtual int startDownloadNexusFileForGame(const QString& gameName, int modID,
                                            int fileID) = 0;

  /**
   * @brief get the (absolute) file path of the specified download.
   * @param id id of the download as returned by the download... functions
   * @return absoute path to the downloaded file. This file may not yet exist if the
   * download is incomplete
   */
  virtual QString downloadPath(int id) = 0;

  /**
   * @brief Installs a handler to be called when a download complete.
   *
   * @param callback The function to be called when a download complete. The argument is
   *     the download ID.
   *
   * @return true if the handler was successfully installed (there is as of now no known
   * reason this should fail).
   */
  virtual bool onDownloadComplete(const std::function<void(int)>& callback) = 0;

  /**
   * @brief Installs a handler to be called when a download is paused.
   *
   * @param callback The function to be called when a download is paused. The argument
   * is the download ID.
   *
   * @return true if the handler was successfully installed (there is as of now no known
   * reason this should fail).
   */
  virtual bool onDownloadPaused(const std::function<void(int)>& callback) = 0;

  /**
   * @brief Installs a handler to be called when a download fails.
   *
   * @param callback The function to be called when a download fails. The argument is
   *     the download ID.
   *
   * @return true if the handler was successfully installed (there is as of now no known
   * reason this should fail).
   */
  virtual bool onDownloadFailed(const std::function<void(int)>& callback) = 0;

  /**
   * @brief Installs a handler to be called when a download is removed.
   *
   * @param callback The function to be called when a download is removed. The argument
   * is the download ID (which is no longer valid).
   *
   * @return true if the handler was successfully installed (there is as of now no known
   * reason this should fail).
   */
  virtual bool onDownloadRemoved(const std::function<void(int)>& callback) = 0;
};

}  // namespace MOBase

#endif  // IDOWNLOADMANAGER_H
