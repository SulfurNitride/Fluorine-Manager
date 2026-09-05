#pragma once

#include "curatedguideinstaller.h"

#include <QDialog>
#include <QHash>
#include <QTimer>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QNetworkAccessManager;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QStackedWidget;

class CuratedGuideDialog : public QDialog
{
  Q_OBJECT

public:
  explicit CuratedGuideDialog(QWidget* parent = nullptr);
  QString createdInstanceDir() const { return m_createdInstanceDir; }
  bool shouldSwitchToInstance() const;

private:
  QVector<CuratedGuideRecipe> m_recipes;
  CuratedGuideInstaller m_installer;
  QString m_createdInstanceDir;
  QString m_manualArtifactId;
  QString m_manualFilename;
  QString m_manualCandidate;
  QString m_artworkDigest;
  QHash<QString, QString> m_detectedStores;
  qint64 m_manualCandidateSize{-1};
  QTimer m_manualWatcher;

  QStackedWidget* m_pages{};
  QComboBox* m_recipe{};
  QComboBox* m_store{};
  QComboBox* m_preset{};
  QLineEdit* m_instanceName{};
  QLineEdit* m_instancePath{};
  QLineEdit* m_downloadsPath{};
  QLineEdit* m_fnvPath{};
  QLineEdit* m_fo3Path{};
  QLineEdit* m_resolution{};
  QLineEdit* m_fov{};
  QCheckBox* m_isolated{};
  QCheckBox* m_cleanConfirm{};
  QCheckBox* m_switchInstance{};
  QLabel* m_recipeDetails{};
  QLabel* m_artwork{};
  QLabel* m_status{};
  QProgressBar* m_progress{};
  QPlainTextEdit* m_log{};
  QPushButton* m_install{};
  QPushButton* m_close{};
  QNetworkAccessManager* m_artworkNetwork{};

  void buildUi();
  void detectGames();
  void recipeChanged(int index);
  void loadArtwork(const CuratedGuideRecipe& recipe);
  void browsePath(QLineEdit* edit);
  QString detectedStore(const QString& path) const;
  bool validateConfiguration(QString* error) const;
  void beginInstall();
  void offerResume();
  void scanForManualArtifact();
  void showManualArtifact(const QString& id, const QString& name,
                          const QString& sourceUrl, const QString& filename);
  void showAssistedAction(const QString& actionId, const QString& title,
                          const QString& instructions, const QString& sourceUrl,
                          const QString& executable, const QString& outputPath,
                          const QString& gamePath);
};
