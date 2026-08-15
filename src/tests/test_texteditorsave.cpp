#include "texteditor.h"

#include <uibase/log.h>
#include <uibase/textviewer.h>
#include <uibase/utility.h>

#include <gtest/gtest.h>

#include <QApplication>
#include <QFile>
#include <QTemporaryDir>
#include <QTextEdit>

namespace {

bool writeBytes(const QString &path, const QByteArray &bytes) {
  QFile file(path);
  return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() &&
         file.flush();
}

QByteArray readBytes(const QString &path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    return {};
  }
  return file.readAll();
}

TEST(TextEncoding, InvalidUtf8FallsBackWithoutLosingBytes) {
  const QByteArray original("caf\xe9", 4);
  QString encoding;
  bool needsBOM = true;
  const QString decoded =
      MOBase::decodeTextData(original, &encoding, &needsBOM);

  EXPECT_FALSE(needsBOM);
  EXPECT_EQ(decoded, QString::fromLatin1(original));

  QByteArray encoded;
  QString error;
  ASSERT_TRUE(
      MOBase::encodeTextData(decoded, encoding, needsBOM, encoded, &error))
      << error.toStdString();
  EXPECT_EQ(encoded, original);

  EXPECT_FALSE(MOBase::encodeTextData(QString::fromUtf8("\xe2\x82\xac"),
                                      QStringLiteral("ISO-8859-1"), false,
                                      encoded, &error));
  EXPECT_TRUE(encoded.isEmpty());
  EXPECT_FALSE(error.isEmpty());
}

TEST(TextEncoding, RoundTripsSupportedBomEncodings) {
  const QString sample = QString::fromUtf8("R\xc3\xa9sum\xc3\xa9");
  const QStringList encodings = {
      QStringLiteral("UTF-8"), QStringLiteral("UTF-16LE"),
      QStringLiteral("UTF-16BE"), QStringLiteral("UTF-32LE"),
      QStringLiteral("UTF-32BE")};

  for (const QString &requested : encodings) {
    SCOPED_TRACE(requested.toStdString());
    QByteArray original;
    QString error;
    ASSERT_TRUE(
        MOBase::encodeTextData(sample, requested, true, original, &error))
        << error.toStdString();

    QString detected;
    bool needsBOM = false;
    EXPECT_EQ(MOBase::decodeTextData(original, &detected, &needsBOM), sample);
    EXPECT_TRUE(needsBOM);

    QByteArray roundTrip;
    ASSERT_TRUE(
        MOBase::encodeTextData(sample, detected, needsBOM, roundTrip, &error))
        << error.toStdString();
    EXPECT_EQ(roundTrip, original);
  }

  QByteArray empty;
  QString error;
  ASSERT_TRUE(MOBase::encodeTextData({}, QStringLiteral("UTF-8"), false, empty,
                                     &error));
  EXPECT_TRUE(empty.isEmpty());
}

TEST(TextEncoding, RejectsAnIncompleteFinalCodeUnit) {
  QString incomplete;
  incomplete.append(QChar(0xd800));
  QByteArray encoded;
  QString error;
  EXPECT_FALSE(MOBase::encodeTextData(incomplete, QStringLiteral("UTF-8"),
                                      false, encoded, &error));
  EXPECT_TRUE(encoded.isEmpty());
  EXPECT_FALSE(error.isEmpty());

  const QString supplementary = QString::fromUtf8("\xf0\x9f\x99\x82");
  ASSERT_TRUE(MOBase::encodeTextData(supplementary, QStringLiteral("UTF-8"),
                                     false, encoded, &error));
  EXPECT_EQ(encoded, QByteArray::fromHex("f09f9982"));
}

TEST(TextEncoding, MalformedWideInputFallsBackWithoutLosingBytes) {
  const QList<QByteArray> malformed = {QByteArray::fromHex("fffe41"),
                                       QByteArray::fromHex("fffe00004100")};
  for (const QByteArray &original : malformed) {
    SCOPED_TRACE(original.toHex().toStdString());
    QString encoding;
    bool needsBOM = true;
    const QString decoded =
        MOBase::decodeTextData(original, &encoding, &needsBOM);
    EXPECT_EQ(decoded, QString::fromLatin1(original));
    EXPECT_FALSE(needsBOM);

    QByteArray roundTrip;
    QString error;
    ASSERT_TRUE(
        MOBase::encodeTextData(decoded, encoding, needsBOM, roundTrip, &error))
        << error.toStdString();
    EXPECT_EQ(roundTrip, original);
  }
}

TEST(TextEditorSave, PublishesAtomicallyAndPreservesUtf16) {
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString path = temporary.filePath("settings.ini");

  QByteArray original;
  QString error;
  ASSERT_TRUE(MOBase::encodeTextData(QStringLiteral("old"),
                                     QStringLiteral("UTF-16LE"), true, original,
                                     &error));
  ASSERT_TRUE(writeBytes(path, original));

  TextEditor editor;
  ASSERT_TRUE(editor.load(path));
  editor.selectAll();
  editor.insertPlainText(QStringLiteral("new\nvalue"));
  ASSERT_TRUE(editor.dirty());
  ASSERT_TRUE(editor.save());
  EXPECT_FALSE(editor.dirty());

  const QByteArray published = readBytes(path);
  EXPECT_TRUE(published.startsWith(QByteArray::fromHex("fffe")));
  QString encoding;
  bool needsBOM = false;
  EXPECT_EQ(MOBase::decodeTextData(published, &encoding, &needsBOM),
            QStringLiteral("new\r\nvalue"));
  EXPECT_TRUE(needsBOM);
}

TEST(TextEditorSave, FailedLoadAndUnsafePublicationPreserveState) {
  QTemporaryDir temporary;
  QTemporaryDir external;
  ASSERT_TRUE(temporary.isValid());
  ASSERT_TRUE(external.isValid());

  const QString ordinary = temporary.filePath("ordinary.txt");
  const QString outside = external.filePath("outside.txt");
  const QString alias = temporary.filePath("alias.txt");
  ASSERT_TRUE(writeBytes(ordinary, "ordinary"));
  ASSERT_TRUE(writeBytes(outside, "outside"));

  TextEditor editor;
  ASSERT_TRUE(editor.load(ordinary));
  editor.selectAll();
  editor.insertPlainText(QStringLiteral("unsaved"));
  ASSERT_TRUE(editor.dirty());
  EXPECT_FALSE(editor.load(temporary.filePath("missing.txt")));
  EXPECT_EQ(editor.filename(), ordinary);
  EXPECT_EQ(editor.toPlainText(), QStringLiteral("unsaved"));
  EXPECT_TRUE(editor.dirty());

  const QString second = temporary.filePath("second.txt");
  ASSERT_TRUE(writeBytes(second, "second"));
  ASSERT_TRUE(editor.load(second));
  EXPECT_EQ(editor.filename(), second);
  EXPECT_EQ(editor.toPlainText(), QStringLiteral("second"));
  EXPECT_FALSE(editor.dirty());

#ifdef Q_OS_UNIX
  ASSERT_TRUE(QFile::link(outside, alias));
  ASSERT_TRUE(editor.load(alias));
  editor.selectAll();
  editor.insertPlainText(QStringLiteral("replacement"));
  EXPECT_FALSE(editor.save());
  EXPECT_EQ(readBytes(outside), QByteArray("outside"));
  EXPECT_TRUE(editor.dirty());
#endif
}

TEST(TextViewerSave, PreservesLoadedEncodingAndClearsModifiedOnCommit) {
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString path = temporary.filePath("profile.ini");

  QByteArray original;
  QString error;
  ASSERT_TRUE(MOBase::encodeTextData(QStringLiteral("before"),
                                     QStringLiteral("UTF-16LE"), true, original,
                                     &error));
  ASSERT_TRUE(writeBytes(path, original));

  MOBase::TextViewer viewer(QStringLiteral("Editor"));
  viewer.addFile(path, true);
  auto *editor = viewer.findChild<QTextEdit *>(QStringLiteral("editorView"));
  ASSERT_NE(editor, nullptr);
  editor->selectAll();
  editor->insertPlainText(QStringLiteral("after"));
  ASSERT_TRUE(editor->document()->isModified());
  ASSERT_TRUE(
      QMetaObject::invokeMethod(&viewer, "saveFile", Qt::DirectConnection));
  EXPECT_FALSE(editor->document()->isModified());

  const QByteArray published = readBytes(path);
  EXPECT_TRUE(published.startsWith(QByteArray::fromHex("fffe")));
  QString encoding;
  bool needsBOM = false;
  EXPECT_EQ(MOBase::decodeTextData(published, &encoding, &needsBOM),
            QStringLiteral("after"));
  EXPECT_TRUE(needsBOM);
}

} // namespace

int main(int argc, char **argv) {
  qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("offscreen"));
  QApplication application(argc, argv);
  MOBase::log::LoggerConfiguration logging;
  logging.name = "test_texteditorsave";
  logging.maxLevel = MOBase::log::Warning;
  MOBase::log::createDefault(std::move(logging));
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
