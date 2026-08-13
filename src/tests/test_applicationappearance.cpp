#include "applicationappearance.h"

#include <gtest/gtest.h>

#include <QApplication>
#include <QColor>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFontInfo>
#include <QLabel>
#include <QStyle>
#include <QStyleFactory>
#include <QTemporaryDir>

namespace
{
void writeFile(const QString& path, const QByteArray& contents)
{
  ASSERT_TRUE(QDir().mkpath(QFileInfo(path).absolutePath()));
  QFile file(path);
  ASSERT_TRUE(file.open(QIODevice::WriteOnly));
  ASSERT_EQ(file.write(contents), contents.size());
}

class ApplicationAppearanceTest : public ::testing::Test
{
protected:
  static QString stableStyle()
  {
    const QStringList styles = QStyleFactory::keys();
    const int fusion = styles.indexOf("Fusion", 0, Qt::CaseInsensitive);
    return fusion >= 0 ? styles[fusion] : styles.front();
  }

  void SetUp() override
  {
    ASSERT_NE(qApp, nullptr);
    ASSERT_FALSE(QStyleFactory::keys().isEmpty());
    qApp->setStyle(QStyleFactory::create(stableStyle()));
    m_Baseline = qApp->font();
  }

  void TearDown() override
  {
    qApp->setStyleSheet({});
    qApp->setFont(m_Baseline);
    qApp->setStyle(QStyleFactory::create(stableStyle()));
  }

  QFont m_Baseline;
};
}  // namespace

TEST_F(ApplicationAppearanceTest, AppliesThemeToExistingAndFutureWidgets)
{
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString app = temporary.filePath("app");
  const QString instance = temporary.filePath("instance");
  writeFile(instance + "/stylesheets/dark.qss",
            "QWidget { background: #191919; color: #dedede; font-size: 12px; }");

  ApplicationAppearance::Controller controller(
      *qApp, app, stableStyle(), m_Baseline);
  QLabel existing;
  existing.ensurePolished();
  const QColor baselineBackground = existing.palette().window().color();
  const QColor baselineText       = existing.palette().windowText().color();
  const QFont baselineWidgetFont  = existing.font();
  ApplicationAppearance::Spec spec;
  spec.styleName         = "dark.qss";
  spec.instanceDirectory = instance;
  ASSERT_TRUE(controller.apply(spec));
  EXPECT_TRUE(qApp->styleSheet().contains("#191919"));

  QLabel future;
  existing.ensurePolished();
  future.ensurePolished();
  EXPECT_EQ(existing.palette().window().color(), QColor("#191919"));
  EXPECT_EQ(existing.palette().windowText().color(), QColor("#dedede"));
  EXPECT_EQ(existing.font().pixelSize(), 12);
  EXPECT_EQ(future.palette().window().color(), QColor("#191919"));
  EXPECT_EQ(future.palette().windowText().color(), QColor("#dedede"));
  EXPECT_EQ(future.font().pixelSize(), 12);
  EXPECT_EQ(controller.activeStyleFile(),
            QFileInfo(instance + "/stylesheets/dark.qss").canonicalFilePath());

  ApplicationAppearance::Spec invalid = spec;
  invalid.styleName = "missing.qss";
  EXPECT_FALSE(controller.apply(invalid));
  existing.ensurePolished();
  EXPECT_EQ(existing.palette().window().color(), baselineBackground);
  EXPECT_EQ(existing.palette().windowText().color(), baselineText);
  EXPECT_EQ(existing.font(), baselineWidgetFont);
}

TEST_F(ApplicationAppearanceTest, InvalidReplacementClearsPriorAppearance)
{
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString app = temporary.filePath("app");
  const QString instance = temporary.filePath("instance");
  writeFile(instance + "/stylesheets/dark.qss",
            "QWidget { background: #191919; }");

  ApplicationAppearance::Controller controller(
      *qApp, app, stableStyle(), m_Baseline);
  ApplicationAppearance::Spec valid;
  valid.styleName         = "dark.qss";
  valid.instanceDirectory = instance;
  ASSERT_TRUE(controller.apply(valid));
  ASSERT_FALSE(qApp->styleSheet().isEmpty());

  ApplicationAppearance::Spec invalid = valid;
  invalid.styleName = "missing.qss";
  QString error;
  EXPECT_FALSE(controller.apply(invalid, &error));
  EXPECT_FALSE(error.isEmpty());
  EXPECT_TRUE(qApp->styleSheet().isEmpty());
  EXPECT_TRUE(controller.activeStyleFile().isEmpty());
  EXPECT_EQ(qApp->font(), m_Baseline);
}

TEST_F(ApplicationAppearanceTest, DefaultInstanceCannotInheritPreviousFont)
{
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString app = temporary.filePath("app");
  ApplicationAppearance::Controller controller(
      *qApp, app, stableStyle(), m_Baseline);

  ApplicationAppearance::Spec first;
  first.fontFamily = "monospace";
  first.fontSize   = 17;
  ASSERT_TRUE(controller.apply(first));
  EXPECT_EQ(qApp->font().pixelSize(), 17);

  ASSERT_TRUE(controller.apply(ApplicationAppearance::Spec{}));
  EXPECT_EQ(qApp->font(), m_Baseline);
}

TEST_F(ApplicationAppearanceTest, ExplicitFontSizeOverridesStylesheetTypography)
{
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString app      = temporary.filePath("app");
  const QString instance = temporary.filePath("instance");
  writeFile(instance + "/stylesheets/dark.qss",
            "QWidget { color: #dedede; font-size: 12px; }");

  ApplicationAppearance::Controller controller(
      *qApp, app, stableStyle(), m_Baseline);
  ApplicationAppearance::Spec spec;
  spec.styleName         = "dark.qss";
  spec.fontSize          = 15;
  spec.instanceDirectory = instance;
  ASSERT_TRUE(controller.apply(spec));

  QLabel existing;
  existing.show();
  existing.ensurePolished();
  qApp->processEvents();
  EXPECT_EQ(existing.font().pixelSize(), 15);
  EXPECT_TRUE(qApp->styleSheet().contains("font-size: 15px"));

  controller.reset();
  qApp->processEvents();
  existing.ensurePolished();
  EXPECT_EQ(qApp->font(), m_Baseline);
  const QFontInfo restored(existing.font());
  const QFontInfo baseline(m_Baseline);
  EXPECT_EQ(restored.family(), baseline.family());
  EXPECT_EQ(restored.pointSizeF(), baseline.pointSizeF());
  EXPECT_EQ(restored.pixelSize(), baseline.pixelSize());
  EXPECT_EQ(restored.weight(), baseline.weight());
}

TEST_F(ApplicationAppearanceTest, RejectsEscapingStylesheet)
{
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString app = temporary.filePath("app");
  const QString instance = temporary.filePath("instance");
  writeFile(temporary.filePath("outside.qss"), "QWidget { color: red; }");
  ASSERT_TRUE(QDir().mkpath(instance + "/stylesheets"));
  ASSERT_TRUE(QFile::link(temporary.filePath("outside.qss"),
                          instance + "/stylesheets/escape.qss"));

  ApplicationAppearance::Controller controller(
      *qApp, app, stableStyle(), m_Baseline);
  ApplicationAppearance::Spec spec;
  spec.styleName         = "escape.qss";
  spec.instanceDirectory = instance;
  EXPECT_FALSE(controller.apply(spec));
  EXPECT_TRUE(qApp->styleSheet().isEmpty());
}

TEST_F(ApplicationAppearanceTest, FactoryStyleClearsPriorStylesheet)
{
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString app = temporary.filePath("app");
  const QString instance = temporary.filePath("instance");
  writeFile(instance + "/stylesheets/dark.qss",
            "QWidget { background: #191919; }");

  ApplicationAppearance::Controller controller(
      *qApp, app, stableStyle(), m_Baseline);
  ApplicationAppearance::Spec dark;
  dark.styleName         = "dark.qss";
  dark.instanceDirectory = instance;
  ASSERT_TRUE(controller.apply(dark));
  ASSERT_FALSE(qApp->styleSheet().isEmpty());

  ApplicationAppearance::Spec factory;
  factory.styleName = stableStyle();
  ASSERT_TRUE(controller.apply(factory));
  EXPECT_TRUE(qApp->styleSheet().isEmpty());
  EXPECT_TRUE(controller.activeStyleFile().isEmpty());
}

int main(int argc, char** argv)
{
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QApplication application(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
