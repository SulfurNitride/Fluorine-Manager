#ifndef FLUORINE_UPDATE_INSTALLER_H
#define FLUORINE_UPDATE_INSTALLER_H

#include "fluorineupdater.h"

#include <QObject>
#include <QString>

// Downloads a release into a private update attempt directory, validates that
// it contains a launcher, and hands it to the launcher's serialized publisher
// after Fluorine exits.
class FluorineUpdateInstaller : public QObject
{
  Q_OBJECT

public:
  explicit FluorineUpdateInstaller(QObject* parent = nullptr);
  ~FluorineUpdateInstaller() override;

  bool isBusy() const { return m_busy; }
  void install(const FluorineUpdater::ReleaseInfo& info);

signals:
  void statusChanged(const QString& status);
  void downloadProgress(qint64 received, qint64 total);
  void failed(const QString& reason);

private:
  void fail(const QString& reason);
  void cleanFailedAttempt();

  bool m_busy = false;
  bool m_attemptHandedOff = false;
  QString m_attemptDirectory;
};

#endif  // FLUORINE_UPDATE_INSTALLER_H
