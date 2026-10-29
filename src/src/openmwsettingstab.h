#pragma once

#include "openmwsettings.h"

#include <QList>
#include <QDialog>
#include <QString>
#include <QWidget>
#include <memory>

class QCheckBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QScrollArea;
class QVBoxLayout;

// The OpenMW Launcher's settings, inside Fluorine: every setting OpenMW
// documents in its defaults file, grouped like the Launcher, searchable, with
// the explanation OpenMW gives for each. It edits the profile's settings.cfg
// and leaves everything else in that file (comments, values set by a mod list,
// keys it does not know) untouched.
class OpenMWSettingsTab : public QWidget
{
  Q_OBJECT

public:
  explicit OpenMWSettingsTab(QWidget* parent = nullptr);
  ~OpenMWSettingsTab() override;

  // The settings.cfg OpenMW reads for a profile: the profile's own when it
  // uses local settings, otherwise the user's OpenMW config.
  static QString settingsPathFor(bool localSettings, const QString& profileDir);

  // Edit this file. If there are unsaved edits to another file the user is
  // asked first; returns false if they cancelled.
  bool setSettingsFile(const QString& path, const QString& description);

  bool hasUnsavedChanges() const;
  // Ask whether to save unsaved edits. Returns false if the user cancelled.
  bool promptToSave();

  // Whether this instance has a navigation mesh tool to start. Without one the
  // Advanced page explains how to get it instead of offering the button.
  void setNavmeshAvailable(bool available);

Q_SIGNALS:
  // The user asked to rebuild the navigation meshes for the current load order.
  void navmeshRequested();

protected:
  void resizeEvent(QResizeEvent* event) override;
  void showEvent(QShowEvent* event) override;

private:
  struct PageDefinition
  {
    QString title;
    QStringList sections;
  };
  static const QList<PageDefinition>& pages();

  void buildUi();
  void reload(const QString& path);
  void showPage();
  void rebuildContent();
  QList<const OpenMWSettings::Setting*> visibleSettings() const;
  QWidget* makeRow(const OpenMWSettings::Setting& setting, QWidget* parent);
  // A size picker above the two resolution fields: this machine's screens and
  // common sizes, written to "resolution x" and "resolution y".
  QWidget* makeResolutionRow(QWidget* parent);
  QWidget* makeNavmeshPanel();
  void updatePageCounts();
  // Qt's scroll area underestimates the height of wrapped labels, so each
  // explanation is given the height its text needs at the current width.
  void fitNotes();
  void updateButtons();
  bool save();

  OpenMWSettings::Schema m_schema;
  OpenMWSettings::File m_file;
  QList<OpenMWSettings::Setting> m_unknown;
  QString m_description;

  QLabel* m_location{};
  QLabel* m_state{};
  QLineEdit* m_search{};
  QCheckBox* m_changedOnly{};
  QListWidget* m_pageList{};
  QScrollArea* m_scroll{};
  QWidget* m_content{};
  QVBoxLayout* m_contentLayout{};
  QPushButton* m_save{};
  QPushButton* m_revert{};
  QLabel* m_message{};
  bool m_rebuilding{false};
  bool m_navmeshAvailable{false};
};

// The same editor in its own window, opened from a button next to Run. One
// instance lives as long as the main window; it is hidden, not destroyed, when
// closed (after offering to save unsaved edits).
class OpenMWSettingsDialog : public QDialog
{
  Q_OBJECT

public:
  explicit OpenMWSettingsDialog(QWidget* parent = nullptr);

  // Point the editor at a profile's settings. Returns false if the user
  // cancelled a prompt about unsaved edits to the previous file.
  bool setSettingsFile(const QString& path, const QString& description);
  bool promptToSave() { return m_tab->promptToSave(); }
  void setNavmeshAvailable(bool available) { m_tab->setNavmeshAvailable(available); }
  bool hasUnsavedChanges() const { return m_tab->hasUnsavedChanges(); }
  OpenMWSettingsTab* editor() const { return m_tab; }

Q_SIGNALS:
  void navmeshRequested();

protected:
  void closeEvent(QCloseEvent* event) override;

private:
  OpenMWSettingsTab* m_tab{};
};
