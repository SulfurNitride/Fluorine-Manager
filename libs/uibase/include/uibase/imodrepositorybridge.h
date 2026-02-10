#ifndef INEXUSBRIDGE_H
#define INEXUSBRIDGE_H

#include "modrepositoryfileinfo.h"
#include <QNetworkReply>
#include <QObject>
#include <QVariant>

namespace MOBase
{

class QDLLEXPORT IModRepositoryBridge : public QObject
{
  Q_OBJECT
public:
  IModRepositoryBridge(QObject* parent = nullptr) : QObject(parent) {}

  /**
   * @brief request description for a mod
   *
   * @param modID id of the mod caller is interested in
   * @param userData user data to be returned with the result
   **/
  virtual void requestDescription(QString gameName, int modID, QVariant userData) = 0;

  /**
   * @brief request a list of the files belonging to a mod
   *
   * @param modID id of the mod caller is interested in
   * @param userData user data to be returned with the result
   **/
  virtual void requestFiles(QString gameName, int modID, QVariant userData) = 0;

  /**
   * @brief request info about a single file of a mod
   *
   * @param modID id of the mod caller is interested in
   * @param fileID id of the file the caller is interested in
   * @param userData user data to be returned with the result
   **/
  virtual void requestFileInfo(QString game, int modID, int fileID,
                               QVariant userData) = 0;

  /**
   * @brief request the download url of a file
   *
   * @param modID id of the mod caller is interested in
   * @param fileID id of the file the caller is interested in
   * @param userData user data to be returned with the result
   **/
  virtual void requestDownloadURL(QString gameName, int modID, int fileID,
                                  QVariant userData) = 0;

  /**
   * @brief requestToggleEndorsement
   * @param modID id of the mod caller is interested in
   * @param userData user data to be returned with the result
   */
  virtual void requestToggleEndorsement(QString gameName, int modID, QString modVersion,
                                        bool endorse, QVariant userData) = 0;

private:
  Q_DISABLE_COPY(IModRepositoryBridge)

Q_SIGNALS:

  /**
   * @brief sent when the description for a mod is reported by the repository
   * @param modID id of the mod for which the request was made
   * @param userData the data that was included in the request
   * @param resultData result data. this is a variant map. keys are strings. Value is
   * usually also a string, wrapped in a variant.
   * @note valid keys might change as the repository page is updated. For nexus, the
   * following keys are valid as of this writing: 'allow_view', 'ip',
   * 'one_week_ratings', 'date', 'pm_notify', 'OLD_mid', 'OLD_u_downloads', 'game_id',
   * 'OLD_perm_use', 'mod_page_uri', 'allow_topics', 'has_hot_image', 'id',
   * 'two_weeks_ratings', 'description', 'lastupdate', 'perm_convert', 'author',
   * 'OLD_image', 'translation_of', 'OLD_mname', 'version', 'allow_rating',
   * 'perm_useinstructions', 'featured_count', 'donate', 'type', 'perm_credits',
   *       'hidden_reason', 'OLD_views', 'perm_upload', 'has_back_image', 'adult',
   * 'allow_images', 'OLD_endorsements', 'OLD_size', 'name', 'commenting', 'moderate',
   * 'language', 'perm_others', 'lastcomment', 'OLD_readme', 'summary', 'perm_modify',
   * 'OLD_downloads', 'lock_comments', 'suggested_category', 'allow_tagging',
   * 'published', 'perm_notes', 'category_id', 'thread_id', 'perm_use', 'wizard_steps'
   * @note in the python interface use the onDescriptionAvailable call to register a
   * callback for this signal. The signature of your callback must match the signature
   * of this signal
   * @note this interface is going to be changed at some point to replace resultData
   * with a less "dynamic" data structure
   */
  void descriptionAvailable(QString gameName, int modID, QVariant userData,
                            QVariant resultData);

  /**
   * @brief sent when the list of files for a mod is reported by the repository
   * @param modID id of the mod for which the request was made
   * @param userData the data that was included in the request
   * @param resultData a list of (already decoded) file information objects
   * @note in the python interface use the onFilesAvailable call to register a callback
   * for this signal. The signature of your callback must match the signature of this
   * signal
   */
  void filesAvailable(QString gameName, int modID, QVariant userData,
                      const QList<ModRepositoryFileInfo*>& resultData);

  /**
   * @brief sent when information about a file is reported by the repository
   * @param modID id of the mod for which the request was made
   * @param fileID id of the the file for which information was requested
   * @param userData the data that was included in the request
   * @param resultData a variant map of information about the file.
   * @note valid keys might change as the repository page is updated. For nexus, the
   * following keys are valid as of this writing: 'count', 'requirements_alert',
   * 'u_count', 'description', 'uri', 'size', 'owner_id', 'primary', 'manager',
   * 'version', 'date', 'game_id', 'mod_id', 'category_id', 'id', 'name'
   * @note in the python interface use the onFileInfoAvailable call to register a
   * callback for this signal. The signature of your callback must match the signature
   * of this signal
   * @note if you intend to download this file you don't have to request this
   * information manually. call IDownloadManager::startDownloadNexusFile and let the
   * download manager figure things out
   * @note this interface is going to be changed at some point to replace resultData
   * with a less "dynamic" data structure
   */
  void fileInfoAvailable(QString gameName, int modID, int fileID, QVariant userData,
                         QVariant resultData);

  /**
   * @brief sent when the list of download urls for a file is returned by the repository
   * @param modID id of the mod for which the request was made
   * @param fileID id of the file for which the downloads
   * @param userData the data that was included in the request
   * @param resultData a variant map of information about the url
   * @note this function is not exposed to python. please use
   * IDownloadManager::startDownloadNexusFile to download a file, this lets the download
   * manager figure out the best server according to user preference and stuff
   * @note this interface is going to be changed at some point to replace resultData
   * with a less "dynamic" data structure
   */
  void downloadURLsAvailable(QString gameName, int modID, int fileID, QVariant userData,
                             QVariant resultData);

  /**
   * @brief sent when the endorsement data is returned from the API
   * @param userData the data that was included in the request
   * @param resultData new endorsement state as a boolean (wrapped in a qvariant)
   * @note in the python interface use the onEndorsementsAvailable call to register a
   * callback for this signal. The signature of your callback must match the signature
   * of this signal
   */
  void endorsementsAvailable(QVariant userData, QVariant resultData);

  /**
   * @brief sent when the endorsement state of a mod was changed (only sent as a result
   * of our request)
   * @param modID id of the mod for which the request was made
   * @param userData the data that was included in the request
   * @param resultData new endorsement state as a boolean (wrapped in a qvariant)
   * @note in the python interface use the onEndorsementToggled call to register a
   * callback for this signal. The signature of your callback must match the signature
   * of this signal
   * @note this interface is going to be changed at some point to replace resultData
   * with a boolean
   */
  void endorsementToggled(QString gameName, int modID, QVariant userData,
                          QVariant resultData);

  /**
   * @brief sent when the tracked mod data is returned from the API
   * @param userData the data that was included in the request
   * @param resultData new tracked state as a list of maps (keys: domain_name, mod_id)
   * @note in the python interface use the ontrackedModsAvailable call to register a
   * callback for this signal. The signature of your callback must match the signature
   * of this signal
   */
  void trackedModsAvailable(QVariant userData, QVariant resultData);

  /**
   * @brief sent when the tracking state of a mod was changed (only sent as a result of
   * our request)
   * @param modID id of the mod for which the request was made
   * @param userData the data that was included in the request
   * @param tracked new tracking state
   * @note in the python interface use the onTrackingToggled call to register a callback
   * for this signal. The signature of your callback must match the signature of this
   * signal
   */
  void trackingToggled(QString gameName, int modID, QVariant userData, bool tracked);

  /**
   * @brief sent when the tracking state of a mod was changed (only sent as a result of
   * our request)
   * @param modID id of the mod for which the request was made
   * @param userData the data that was included in the request
   * @param tracked new tracking state
   * @note in the python interface use the onTrackingToggled call to register a callback
   * for this signal. The signature of your callback must match the signature of this
   * signal
   */
  void gameInfoAvailable(QString gameName, QVariant userData, QVariant resultData);

  /**
   * @brief sent when a request to nexus failed
   * @param modID id of the mod for which the request was made
   * @param fileID id of the file for which the request was made (ignore if the request
   * was for the mod in general)
   * @param userData the data that was included in the request
   * @param errorMessage textual description of the problem
   * @note in the python interface use the onDescriptionAvailable call to register a
   * callback for this signal. The signature of your callback must match the signature
   * of this signal
   */
  void requestFailed(QString gameName, int modID, int fileID, QVariant userData,
                     int errorCode, const QString& errorMessage);
};

}  // namespace MOBase

#endif  // INEXUSBRIDGE_H
