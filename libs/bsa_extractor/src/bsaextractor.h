#ifndef BSAEXTRACTOR_H
#define BSAEXTRACTOR_H

#include <QProgressDialog>

#include <uibase/iplugin.h>

class BsaExtractor : public QObject, public MOBase::IPlugin
{
  Q_OBJECT
  Q_INTERFACES(MOBase::IPlugin)
#if QT_VERSION >= QT_VERSION_CHECK(5,0,0)
  Q_PLUGIN_METADATA(IID "org.tannin.BSAExtractor")
#endif

public:
  BsaExtractor();

  virtual bool init(MOBase::IOrganizer *moInfo) override;
  virtual QString name() const override;
  virtual QString localizedName() const override;
  virtual QString author() const override;
  virtual QString description() const override;
  virtual MOBase::VersionInfo version() const override;
  virtual QList<MOBase::PluginSetting> settings() const override;

private:
  void modInstalledHandler(MOBase::IModInterface *mod);
  bool extractProgress(QProgressDialog &progress, int percentage, std::string fileName);
private:
  MOBase::IOrganizer *m_Organizer;
};

#endif // BSAEXTRACTOR_H
