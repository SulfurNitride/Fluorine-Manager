#include "../src/openmwsettings.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <gtest/gtest.h>

using namespace OpenMWSettings;

namespace
{
QString defaultsText()
{
  QFile file(QStringLiteral(OPENMW_DEFAULTS_PATH));
  EXPECT_TRUE(file.open(QIODevice::ReadOnly)) << OPENMW_DEFAULTS_PATH;
  return QString::fromUtf8(file.readAll());
}

const Schema& schema()
{
  static const Schema s = Schema::parse(defaultsText());
  return s;
}

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
}  // namespace

TEST(OpenMWSettingsSchema, ReadsEverySettingOfTheBundledDefaults)
{
  EXPECT_GE(schema().settings().size(), 450);
  EXPECT_GE(schema().sections().size(), 20);
  EXPECT_TRUE(schema().sections().contains(QStringLiteral("Video")));
  // No setting is read twice or without a section.
  QSet<QString> ids;
  for (const auto& s : schema().settings()) {
    EXPECT_FALSE(s.section.isEmpty());
    EXPECT_FALSE(ids.contains(s.id())) << s.id().toStdString();
    ids.insert(s.id());
  }
}

TEST(OpenMWSettingsSchema, InfersTypesFromDefaults)
{
  EXPECT_EQ(schema().find("Game", "show projectile damage")->type, Type::Bool);
  EXPECT_EQ(schema().find("Video", "resolution x")->type, Type::Int);
  EXPECT_EQ(schema().find("Video", "contrast")->type, Type::Float);
  EXPECT_EQ(schema().find("Sound", "device")->type, Type::Text);
  EXPECT_EQ(schema().find("Sound", "device")->defaultValue, QString());
  EXPECT_EQ(schema().find("Camera", "field of view")->type, Type::Float);
}

TEST(OpenMWSettingsSchema, TakesTheExplanationFromTheCommentAbove)
{
  const auto* fov = schema().find("Camera", "field of view");
  ASSERT_NE(fov, nullptr);
  EXPECT_TRUE(fov->comment.contains("Camera field of view"));
  // Two settings under one comment share it.
  EXPECT_EQ(schema().find("Video", "resolution x")->comment,
            schema().find("Video", "resolution y")->comment);
  // A setting after a blank line does not inherit the previous comment.
  EXPECT_TRUE(schema().find("Fog", "distant land fog start")->comment.isEmpty());
  EXPECT_EQ(schema().find("Fog", "key"), nullptr);
}

TEST(OpenMWSettingsSchema, TurnsNumberedAndWordListsIntoChoices)
{
  const auto* mode = schema().find("Video", "window mode");
  ASSERT_NE(mode, nullptr);
  ASSERT_EQ(mode->choices.size(), 3);
  EXPECT_EQ(mode->choices[0].value, QString("0"));
  EXPECT_TRUE(mode->choices[2].label.contains("Windowed"));

  const auto* hrtf = schema().find("Sound", "hrtf enable");
  ASSERT_NE(hrtf, nullptr);
  QStringList values;
  for (const auto& c : hrtf->choices)
    values << c.value;
  EXPECT_EQ(values, (QStringList{"-1", "0", "1"}));

  const auto* mag = schema().find("General", "texture mag filter");
  ASSERT_NE(mag, nullptr);
  ASSERT_EQ(mag->choices.size(), 2);
  EXPECT_EQ(mag->choices[0].value, QString("nearest"));

  const auto* mip = schema().find("General", "texture mipmap");
  ASSERT_NE(mip, nullptr);
  EXPECT_EQ(mip->choices.size(), 3);

  // Free-form numbers and text stay free-form.
  EXPECT_TRUE(schema().find("Video", "resolution x")->choices.isEmpty());
  EXPECT_TRUE(schema().find("Game", "difficulty")->choices.isEmpty());
}

TEST(OpenMWSettingsSchema, LabelsStartWithACapital)
{
  EXPECT_EQ(schema().find("Video", "window mode")->label(), QString("Window mode"));
}

TEST(OpenMWSettingsFile, TreatsAMissingFileAsEmpty)
{
  QTemporaryDir dir;
  File file;
  ASSERT_TRUE(file.load(dir.filePath("settings.cfg")));
  EXPECT_FALSE(file.contains("Video", "window mode"));
  EXPECT_FALSE(file.isModified());
}

TEST(OpenMWSettingsFile, ChangesOnlyTheLinesItIsAskedTo)
{
  QTemporaryDir dir;
  const QString path = dir.filePath("settings.cfg");
  const QString original =
      "# kept comment\n[GUI]\nscaling factor = 1.5\n\n# another\n[Video]\n"
      "window mode = 2\nmod added = yes\n[Lua]\nsome mod setting = 7\n";
  spit(path, original);

  File file;
  ASSERT_TRUE(file.load(path));
  EXPECT_EQ(file.value("GUI", "scaling factor"), QString("1.5"));
  EXPECT_EQ(file.value("Video", "mod added"), QString("yes"));

  file.setValue("Video", "window mode", "0");  // existing key
  file.setValue("Video", "antialiasing", "4"); // new key, existing section
  file.setValue("Shadows", "enabled", "true"); // new section
  EXPECT_TRUE(file.isModified());
  ASSERT_TRUE(file.save());

  const QString saved = slurp(path);
  EXPECT_EQ(saved,
            "# kept comment\n[GUI]\nscaling factor = 1.5\n\n# another\n[Video]\n"
            "window mode = 0\nmod added = yes\nantialiasing = 4\n"
            "[Lua]\nsome mod setting = 7\n\n[Shadows]\nenabled = true\n");
  EXPECT_FALSE(file.isModified());
}

TEST(OpenMWSettingsFile, RemovesAKeyToReturnToTheDefault)
{
  QTemporaryDir dir;
  const QString path = dir.filePath("settings.cfg");
  spit(path, "[Video]\nwindow mode = 0\nscreen = 1\n");
  File file;
  ASSERT_TRUE(file.load(path));
  file.remove("Video", "window mode");
  file.remove("Video", "not there");
  ASSERT_TRUE(file.save());
  EXPECT_EQ(slurp(path), QString("[Video]\nscreen = 1\n"));
}

TEST(OpenMWSettingsFile, AnUnchangedValueDoesNotMarkTheFileModified)
{
  QTemporaryDir dir;
  const QString path = dir.filePath("settings.cfg");
  spit(path, "[Video]\nwindow mode=0\n");
  File file;
  ASSERT_TRUE(file.load(path));
  file.setValue("Video", "window mode", "0");
  EXPECT_FALSE(file.isModified());
}

TEST(OpenMWSettingsFile, ReadsWindowsLineEndingsAndListsEntriesInOrder)
{
  QTemporaryDir dir;
  const QString path = dir.filePath("settings.cfg");
  spit(path, "[A]\r\nx = 1\r\n[B]\r\ny = 2\r\n");
  File file;
  ASSERT_TRUE(file.load(path));
  EXPECT_EQ(file.value("A", "x"), QString("1"));
  const auto entries = file.entries();
  ASSERT_EQ(entries.size(), 2);
  EXPECT_EQ(entries[1], qMakePair(QString("B"), QString("y")));
}

TEST(OpenMWSettingsFile, LastDefinitionWinsLikeOpenMW)
{
  QTemporaryDir dir;
  const QString path = dir.filePath("settings.cfg");
  spit(path, "[A]\nx = 1\nx = 2\n");
  File file;
  ASSERT_TRUE(file.load(path));
  EXPECT_EQ(file.value("A", "x"), QString("2"));
}

TEST(OpenMWSettingsFile, ASavedFileCanBeEditedAgainAndStaysStable)
{
  QTemporaryDir dir;
  const QString path = dir.filePath("settings.cfg");
  File first;
  ASSERT_TRUE(first.load(path));
  first.setValue("Video", "window mode", "1");
  ASSERT_TRUE(first.save());
  File second;
  ASSERT_TRUE(second.load(path));
  second.setValue("Video", "window mode", "1");
  EXPECT_FALSE(second.isModified());
  EXPECT_EQ(second.value("Video", "window mode"), QString("1"));
}
