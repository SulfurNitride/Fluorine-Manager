#include "../src/openmwsettingstab.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSignalSpy>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QToolButton>
#include <QTest>
#include <gtest/gtest.h>

namespace
{
QString slurp(const QString& path)
{
  QFile file(path);
  EXPECT_TRUE(file.open(QIODevice::ReadOnly));
  return QString::fromUtf8(file.readAll());
}

void spit(const QString& path, const QString& text)
{
  QFile file(path);
  ASSERT_TRUE(file.open(QIODevice::WriteOnly));
  file.write(text.toUtf8());
}

// The control on the same row as the label with this text.
template <class T>
T* controlFor(QWidget* root, const QString& label)
{
  for (auto* l : root->findChildren<QLabel*>()) {
    if (l->text() != label)
      continue;
    for (auto* c : l->parentWidget()->findChildren<T*>())
      return c;
  }
  return nullptr;
}

QPushButton* button(QWidget* root, const QString& text)
{
  for (auto* b : root->findChildren<QPushButton*>())
    if (b->text() == text)
      return b;
  return nullptr;
}

void selectPage(OpenMWSettingsTab& tab, const QString& title)
{
  auto* list = tab.findChild<QListWidget*>();
  for (int i = 0; i < list->count(); ++i)
    if (list->item(i)->text().startsWith(title))
      list->setCurrentRow(i);
  QCoreApplication::processEvents();
}

void shoot(QWidget& w, const QString& name)
{
#ifdef OPENMW_SCREENSHOT_DIR
  QDir().mkpath(QStringLiteral(OPENMW_SCREENSHOT_DIR));
  // Let the layout settle (deferred deletes, posted layout requests) first.
  QTest::qWait(100);
  QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  QTest::qWait(50);
  w.grab().save(QStringLiteral(OPENMW_SCREENSHOT_DIR) + "/" + name + ".png");
#else
  Q_UNUSED(w);
  Q_UNUSED(name);
#endif
}

class Env : public ::testing::Environment
{
public:
  void SetUp() override
  {
    static int argc = 1;
    static char name[] = "test";
    static char* argv[] = {name, nullptr};
    m_app = new QApplication(argc, argv);
  }
  QApplication* m_app{};
};
::testing::Environment* const env = ::testing::AddGlobalTestEnvironment(new Env);
}  // namespace

TEST(OpenMWSettingsTab, EditsOnlyWhatTheUserChangesAndKeepsTheRest)
{
  QTemporaryDir dir;
  const QString path = dir.filePath("settings.cfg");
  const QString original =
      "# set by the mod list\n[GUI]\nscaling factor = 1.5\n\n[Video]\nresolution x = 1920\n";
  spit(path, original);

  OpenMWSettingsTab tab;
  tab.resize(1100, 760);
  ASSERT_TRUE(tab.setSettingsFile(path, "This profile's own OpenMW settings"));
  tab.show();
  QCoreApplication::processEvents();
  selectPage(tab, "Display");
  shoot(tab, "display");

  // Nothing is written until the user changes something.
  EXPECT_FALSE(tab.hasUnsavedChanges());
  auto* mode = controlFor<QComboBox>(&tab, "Window mode");
  ASSERT_NE(mode, nullptr);
  EXPECT_EQ(mode->currentData().toString(), QString("2"));  // the default
  mode->setCurrentIndex(0);
  Q_EMIT mode->activated(0);
  EXPECT_TRUE(tab.hasUnsavedChanges());

  auto* resolution = controlFor<QSpinBox>(&tab, "Resolution y");
  ASSERT_NE(resolution, nullptr);
  resolution->setValue(1080);
  resolution->editingFinished();
  // An explicit value already in the file is shown, not the default.
  auto* x = controlFor<QSpinBox>(&tab, "Resolution x");
  ASSERT_NE(x, nullptr);
  EXPECT_EQ(x->value(), 1920);

  ASSERT_NE(button(&tab, "Save"), nullptr);
  QTest::mouseClick(button(&tab, "Save"), Qt::LeftButton);
  EXPECT_FALSE(tab.hasUnsavedChanges());

  const QString saved = slurp(path);
  EXPECT_TRUE(saved.contains("# set by the mod list\n[GUI]\nscaling factor = 1.5\n"));
  EXPECT_TRUE(saved.contains("window mode = 0"));
  EXPECT_TRUE(saved.contains("resolution y = 1080"));
  EXPECT_TRUE(saved.contains("resolution x = 1920"));
  // A setting the user never touched is not added.
  EXPECT_FALSE(saved.contains("antialiasing"));
}

TEST(OpenMWSettingsTab, ResettingASettingRemovesItFromTheFile)
{
  QTemporaryDir dir;
  const QString path = dir.filePath("settings.cfg");
  spit(path, "[Video]\nwindow mode = 0\n");
  OpenMWSettingsTab tab;
  tab.resize(1100, 760);
  ASSERT_TRUE(tab.setSettingsFile(path, "test"));
  tab.show();
  selectPage(tab, "Display");
  for (auto* l : tab.findChildren<QLabel*>()) {
    if (l->text() != "Window mode")
      continue;
    auto* reset = l->parentWidget()->findChild<QToolButton*>();
    ASSERT_NE(reset, nullptr);
    EXPECT_TRUE(reset->isEnabled());
    reset->click();
    break;
  }
  QCoreApplication::processEvents();
  ASSERT_TRUE(tab.hasUnsavedChanges());
  QTest::mouseClick(button(&tab, "Save"), Qt::LeftButton);
  EXPECT_FALSE(slurp(path).contains("window mode"));
}

TEST(OpenMWSettingsTab, SearchFindsSettingsAcrossEveryPage)
{
  QTemporaryDir dir;
  const QString path = dir.filePath("settings.cfg");
  spit(path, "[Shadows]\nenable shadows = true\n");
  OpenMWSettingsTab tab;
  tab.resize(1100, 760);
  ASSERT_TRUE(tab.setSettingsFile(path, "test"));
  tab.show();
  auto* search = tab.findChild<QLineEdit*>();
  ASSERT_NE(search, nullptr);
  search->setText("shadow");
  QCoreApplication::processEvents();
  shoot(tab, "search");
  int labels = 0;
  for (auto* l : tab.findChildren<QLabel*>())
    if (l->text().contains("shadow", Qt::CaseInsensitive))
      ++labels;
  EXPECT_GT(labels, 3);
  // Changed-only narrows it to what the file sets.
  for (auto* c : tab.findChildren<QCheckBox*>())
    if (c->text() == "Changed only")
      c->setChecked(true);
  QCoreApplication::processEvents();
  shoot(tab, "changed-only");
}

TEST(OpenMWSettingsTab, KeysOpenMWDoesNotDocumentStayEditable)
{
  QTemporaryDir dir;
  const QString path = dir.filePath("settings.cfg");
  spit(path, "[Brand New Mod Section]\nsome option = abc\n");
  OpenMWSettingsTab tab;
  tab.resize(1100, 760);
  ASSERT_TRUE(tab.setSettingsFile(path, "test"));
  tab.show();
  selectPage(tab, "Other");
  shoot(tab, "other");
  auto* edit = controlFor<QLineEdit>(&tab, "Some option");
  ASSERT_NE(edit, nullptr);
  EXPECT_EQ(edit->text(), QString("abc"));
}

TEST(OpenMWSettingsTab, ChoosesTheProfilesFileOrTheUsersOwn)
{
  EXPECT_EQ(OpenMWSettingsTab::settingsPathFor(true, "/inst/profiles/Default"),
            QString("/inst/profiles/Default/settings.cfg"));
  EXPECT_TRUE(OpenMWSettingsTab::settingsPathFor(false, "/inst/profiles/Default")
                  .endsWith("openmw/settings.cfg"));
}

TEST(OpenMWSettingsDialog, ShowsTheEditorInItsOwnWindowAndClosesWithoutAskingWhenClean)
{
  QTemporaryDir dir;
  const QString path = dir.filePath("settings.cfg");
  spit(path, "[Video]\nresolution x = 3840\nresolution y = 1970\n[Shadows]\nenable shadows = true\n");
  OpenMWSettingsDialog dialog;
  ASSERT_TRUE(dialog.setSettingsFile(path, "This profile's own OpenMW settings"));
  dialog.setWindowTitle("OpenMW Settings — Default");
  dialog.show();
  QCoreApplication::processEvents();
  shoot(dialog, "dialog");
  EXPECT_TRUE(dialog.isVisible());
  EXPECT_FALSE(dialog.hasUnsavedChanges());
  EXPECT_TRUE(dialog.close());
  EXPECT_FALSE(dialog.isVisible());
  // It can be opened again on another file.
  const QString other = dir.filePath("other.cfg");
  spit(other, "[Sound]\nmaster volume = 0.5\n");
  EXPECT_TRUE(dialog.setSettingsFile(other, "other"));
  dialog.show();
  EXPECT_TRUE(dialog.isVisible());
}

TEST(OpenMWSettingsTab, ThePickedResolutionFillsBothResolutionValues)
{
  QTemporaryDir dir;
  const QString path = dir.filePath("settings.cfg");
  spit(path, "[Video]\nresolution x = 1366\nresolution y = 768\n");
  OpenMWSettingsTab tab;
  tab.resize(1100, 760);
  ASSERT_TRUE(tab.setSettingsFile(path, "test"));
  tab.show();
  selectPage(tab, "Display");
  QCoreApplication::processEvents();

  auto* picker = tab.findChild<QComboBox*>("resolutionPicker");
  ASSERT_NE(picker, nullptr);
  // The current size is selected even if no screen offers it.
  EXPECT_EQ(picker->currentText(), QString("1366 × 768"));
  int target = -1;
  for (int i = 0; i < picker->count(); ++i)
    if (picker->itemText(i) == QString("1280 × 720"))
      target = i;
  if (target < 0) {  // a small test screen hides the larger sizes only
    GTEST_SKIP() << "1280x720 not offered on this display";
  }
  picker->setCurrentIndex(target);
  Q_EMIT picker->activated(target);
  EXPECT_TRUE(tab.hasUnsavedChanges());
  QTest::qWait(50);  // the number fields refresh after the pick
  EXPECT_EQ(controlFor<QSpinBox>(&tab, "Resolution x")->value(), 1280);
  EXPECT_EQ(controlFor<QSpinBox>(&tab, "Resolution y")->value(), 720);
  QTest::mouseClick(button(&tab, "Save"), Qt::LeftButton);
  const QString saved = slurp(path);
  EXPECT_TRUE(saved.contains("resolution x = 1280"));
  EXPECT_TRUE(saved.contains("resolution y = 720"));
}

TEST(OpenMWSettingsTab, TheAdvancedPageOffersToBuildNavigationMeshes)
{
  QTemporaryDir dir;
  const QString path = dir.filePath("settings.cfg");
  spit(path, "[Navigator]\nenable = true\n");
  OpenMWSettingsTab tab;
  tab.resize(1100, 760);
  ASSERT_TRUE(tab.setSettingsFile(path, "test"));
  tab.show();
  selectPage(tab, "Advanced");
  QCoreApplication::processEvents();

  // Without a bundled tool the button is there but disabled, with an explanation.
  auto* build = tab.findChild<QPushButton*>("buildNavmeshButton");
  ASSERT_NE(build, nullptr);
  EXPECT_FALSE(build->isEnabled());

  tab.setNavmeshAvailable(true);
  QCoreApplication::processEvents();
  QTest::qWait(50);
  build = tab.findChild<QPushButton*>("buildNavmeshButton");
  ASSERT_NE(build, nullptr);
  ASSERT_TRUE(build->isEnabled());
  shoot(tab, "advanced-navmesh");

  QSignalSpy asked(&tab, &OpenMWSettingsTab::navmeshRequested);
  QTest::mouseClick(build, Qt::LeftButton);
  EXPECT_EQ(asked.count(), 1);

  // The panel belongs to the Advanced page only.
  selectPage(tab, "Display");
  QCoreApplication::processEvents();
  EXPECT_EQ(tab.findChild<QPushButton*>("buildNavmeshButton"), nullptr);
}
