#ifndef CHECKFNIS_H
#define CHECKFNIS_H

#include <uibase/imoinfo.h>
#include <uibase/iplugin.h>
#include <uibase/versioninfo.h>

#include <QList>
#include <QMap>
#include <QObject>
#include <QString>

#include <QtGlobal>  // for QT_VERSION, QT_VERSION_CHECK

#include <vector>

namespace MOBase
{
struct PluginSetting;
}

class CheckFNIS : public QObject, public MOBase::IPlugin
{

  Q_OBJECT
  Q_INTERFACES(MOBase::IPlugin)
#if QT_VERSION >= QT_VERSION_CHECK(5, 0, 0)
  Q_PLUGIN_METADATA(IID "org.tannin.CheckFNIS")
#endif

public:
  CheckFNIS();
  ~CheckFNIS();

public:  // IPlugin
  virtual bool init(MOBase::IOrganizer* moInfo) override;
  virtual QString name() const override;
  virtual QString localizedName() const override;
  virtual std::vector<std::shared_ptr<const MOBase::IPluginRequirement>>
  requirements() const override;
  virtual QString author() const override;
  virtual QString description() const override;
  virtual MOBase::VersionInfo version() const override;
  virtual QList<MOBase::PluginSetting> settings() const override;

private:
  bool fnisCheck(const QString& application);
  void fnisEndCheck(const QString& application, unsigned int code);

  QString getFnisPath() const;
  static bool appIsFNIS(QString const& application, const QString& fnisApp);

  bool testFileRelevant(const MOBase::IOrganizer::FileInfo& fileName) const;
  void findRelevantFilesRecursive(const QString& path,
                                  QMap<QString, QString>& fileList) const;
  // generates a map of file-names and hashes. Any change prompts a new fnis run
  QString generateIdentifier() const;

private:
  MOBase::IOrganizer* m_MOInfo;
  bool m_Active;

  std::vector<QRegularExpression> const m_MatchExpressions;
  std::vector<QRegularExpression> const m_SensitiveMatchExpressions;
};

#endif  // CHECKFNIS_H
