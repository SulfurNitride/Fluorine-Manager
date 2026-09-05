#ifndef MODINFODIALOGNEXUS_H
#define MODINFODIALOGNEXUS_H

#include "modinfodialogtab.h"

class NexusTab : public ModInfoDialogTab
{
  Q_OBJECT;

public:
  NexusTab(ModInfoDialogTabContext cx);

  ~NexusTab() override;

  void clear() override;
  void update() override;
  void firstActivation() override;
  void setMod(ModInfoPtr mod, MOShared::FilesOrigin* origin) override;
  bool usesOriginFiles() const override;

private:
  QMetaObject::Connection m_modConnection;
  bool m_requestStarted{false};
  bool m_loading{false};

  void cleanup();
  void updateVersionColor();
  void updateWebpage();
  void updateTracking();

  void refreshData(int modID);
  bool tryRefreshData(int modID);
  void onModChanged();

  void onModIDChanged();
  void onSourceGameChanged();
  void onVersionChanged();
  void onCategoryChanged();

  void onRefreshBrowser();
  void onVisitNexus();
  void onEndorse();
  void onTrack();

  void onCustomURLToggled();
  void onCustomURLChanged();
  void onVisitCustomURL();
};

#endif  // MODINFODIALOGNEXUS_H
