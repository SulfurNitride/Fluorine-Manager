#ifndef MODINSTALLATIONTRANSACTION_H
#define MODINSTALLATIONTRANSACTION_H

#include <functional>
#include <memory>

#include <QFile>
#include <QString>

class QLockFile;
class QTemporaryDir;

class ModInstallationTransaction {
public:
  struct Identity;

  enum class Mode { New, Replace, Merge };

  enum class PublishStatus { Success, Failure, PublicationUncertain };

  struct PublishResult {
    PublishStatus status{PublishStatus::Failure};
    QString error;
    QString residue;

    explicit operator bool() const { return status == PublishStatus::Success; }
    bool filesystemChanged() const {
      return status == PublishStatus::PublicationUncertain;
    }
  };

  struct Target {
    QString name;
    QString path;
    QString generation;
    bool exists{false};
  };

  static bool inspectTarget(const QString &modsRoot,
                            const QString &requestedName, Target &target,
                            QString &error);
  static bool prepareStagedFile(const QString &stageRoot,
                                const QString &relativePath, bool createParents,
                                QString &absolutePath, QString &error);
  static bool prepareStagedMetadata(const QString &stageRoot,
                                    QString &absolutePath, QString &error);

  static std::unique_ptr<ModInstallationTransaction>
  begin(const QString &modsRoot, const QString &targetName, Mode mode,
        QString &error, const QString &expectedGeneration = {},
        const std::function<void()> &afterSourceSnapshotForTesting = {});

  ~ModInstallationTransaction();

  QString stagePath() const;
  QString targetPath() const;
  QString targetName() const;
  Mode mode() const;

  PublishResult publish();

  ModInstallationTransaction(const ModInstallationTransaction &) = delete;
  ModInstallationTransaction &
  operator=(const ModInstallationTransaction &) = delete;

private:
  ModInstallationTransaction(QString root, Target target, Mode mode,
                             std::unique_ptr<QLockFile> lock,
                             std::unique_ptr<QTemporaryDir> stage,
                             QString stagePath, Identity rootIdentity,
                             Identity targetIdentity, Identity stageIdentity,
                             QFile::Permissions targetPermissions,
                             QString metadataGeneration);

  QString m_Root;
  Target m_Target;
  Mode m_Mode;
  std::unique_ptr<QLockFile> m_Lock;
  std::unique_ptr<QTemporaryDir> m_Stage;
  QString m_StagePath;
  std::unique_ptr<Identity> m_RootIdentity;
  std::unique_ptr<Identity> m_TargetIdentity;
  std::unique_ptr<Identity> m_StageIdentity;
  QFile::Permissions m_TargetPermissions;
  QString m_MetadataGeneration;
  bool m_Published{false};
};

#endif // MODINSTALLATIONTRANSACTION_H
