/*
Mod Organizer shared UI functionality

Copyright (C) 2012 Sebastian Herbord. All rights reserved.

This library is free software; you can redistribute it and/or
modify it under the terms of the GNU Lesser General Public
License as published by the Free Software Foundation; either
version 3 of the License, or (at your option) any later version.

This library is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public
License along with this library; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
*/

#include <uibase/textviewer.h>
#include <uibase/finddialog.h>
#include <uibase/log.h>
#include <uibase/report.h>
#include <uibase/transactionalwritefile.h>
#include "ui_textviewer.h"
#include <uibase/utility.h>
#include <QAction>
#include <QFile>
#include <QFileInfo>
#include <QMessageBox>
#include <QPushButton>
#include <QShortcutEvent>
#include <QTextEdit>
#include <QVBoxLayout>

namespace MOBase
{

namespace
{

bool trySaveEditor(QTextEdit* editor)
{
  const QString path     = editor->documentTitle();
  const QString encoding = editor->property("mo2TextEncoding").toString();
  const bool needsBOM    = editor->property("mo2TextNeedsBOM").toBool();
  QByteArray contents;
  QString encodingError;
  if (!encodeTextData(editor->toPlainText().replace('\n', "\r\n"), encoding,
                      needsBOM, contents, &encodingError)) {
    reportError(QObject::tr("failed to encode %1: %2").arg(path, encodingError));
    return false;
  }

  TransactionalWriteFile transaction(path);
  QByteArray original;
  bool present = false;
  if (!transaction.readOriginal(original, present)) {
    reportError(QObject::tr("failed to write to %1: %2")
                    .arg(path, transaction.errorString()));
    return false;
  }

  QFileDevice::Permissions originalPermissions;
  if (present && !transaction.readPermissions(originalPermissions)) {
    reportError(QObject::tr("failed to read permissions for %1: %2")
                    .arg(path, transaction.errorString()));
    return false;
  }

  QFileInfo fileInfo(path);
  if (present && !fileInfo.isWritable()) {
    const QMessageBox::StandardButton buttonPressed =
        MOBase::TaskDialog(qApp->activeModalWidget(),
                           QObject::tr("INI file is read-only"))
            .main(QObject::tr("INI file is read-only"))
            .content(QObject::tr("Mod Organizer is attempting to write to \"%1\" "
                                 "which is currently set to read-only.")
                         .arg(fileInfo.fileName()))
            .icon(QMessageBox::Warning)
            .button({QObject::tr("Clear the read-only flag"), QMessageBox::Yes})
            .button({QObject::tr("Allow the write once"),
                     QObject::tr("The file will be set to read-only again."),
                     QMessageBox::Ignore})
            .button({QObject::tr("Skip this file"), QMessageBox::No})
            .remember("clearReadOnly", fileInfo.fileName())
            .exec();

    if (!(buttonPressed & (QMessageBox::Yes | QMessageBox::Ignore))) {
      return false;
    }
    const QFileDevice::Permissions publishedPermissions =
        buttonPressed == QMessageBox::Yes
            ? originalPermissions | QFileDevice::WriteUser | QFileDevice::WriteOwner
            : originalPermissions;
    if (!transaction.setPermissions(publishedPermissions)) {
      reportError(QObject::tr("failed to prepare permissions for %1: %2")
                      .arg(path, transaction.errorString()));
      return false;
    }
  }

  if (!transaction.replaceWith(contents)) {
    reportError(QObject::tr("failed to write to %1: %2")
                    .arg(path, transaction.errorString()));
    return false;
  }

  editor->document()->setModified(false);
  return true;
}

}  // namespace

TextViewer::TextViewer(const QString& title, QWidget* parent)
    : QDialog(parent), ui(new Ui::TextViewer), m_FindDialog(nullptr)
{
  ui->setupUi(this);
  setWindowTitle(title);
  m_EditorTabs = findChild<QTabWidget*>("editorTabs");
  connect(ui->showWhitespace, SIGNAL(stateChanged(int)), this,
          SLOT(showWhitespaceChanged(int)));
}

TextViewer::~TextViewer()
{
  delete ui;
}

void TextViewer::closeEvent(QCloseEvent* event)
{
  while (!m_Modified.empty()) {
    QTextEdit* editor = *m_Modified.begin();
    QMessageBox::StandardButton res = QMessageBox::question(
        this, tr("Save changes?"),
        tr("Do you want to save changes to %1?").arg(editor->documentTitle()),
        QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);
    if (res == QMessageBox::Yes) {
      if (!trySaveEditor(editor)) {
        event->ignore();
        return;
      }
    } else if (res == QMessageBox::Cancel) {
      event->ignore();
      return;
    }
    m_Modified.erase(editor);
  }
  event->accept();
}

void TextViewer::find()
{
  if (!m_FindDialog) {
    m_FindDialog = new FindDialog(this);
    connect(m_FindDialog, SIGNAL(findNext()), this, SLOT(findNext()));
    connect(m_FindDialog, SIGNAL(patternChanged(QString)), this,
            SLOT(patternChanged(QString)));
  }

  m_FindDialog->show();
  m_FindDialog->raise();
  m_FindDialog->activateWindow();
}

void TextViewer::patternChanged(QString newPattern)
{
  m_FindPattern = newPattern;
}

void TextViewer::findNext()
{
  if (m_FindPattern.length() == 0) {
    return;
  }

  QWidget* currentPage = m_EditorTabs->currentWidget();
  QTextEdit* editor    = currentPage->findChild<QTextEdit*>("editorView");

  if (editor->find(m_FindPattern)) {
    // found text
    return;
  } else {
    // reached the bottom and no text found,
    // we wrap around once.

    // save current cursor
    auto oldCursor = editor->textCursor();

    editor->moveCursor(QTextCursor::Start);

    // search again from the top
    if (editor->find(m_FindPattern)) {
      // found something, keep new cursor position.
      return;
    } else {
      // there are no matches in the document,
      // restore previous cursor.
      editor->setTextCursor(oldCursor);
    }
  }
}

void TextViewer::showWhitespaceChanged(int state)
{
  for (int i = 0; i < m_EditorTabs->count(); ++i) {
    QTextEdit* editor = m_EditorTabs->widget(i)->findChild<QTextEdit*>();
    if (editor != nullptr) {
      auto document   = editor->document();
      auto textOption = document->defaultTextOption();
      auto flags      = textOption.flags();
      if (state == Qt::Unchecked)
        flags = flags & (~QTextOption::ShowTabsAndSpaces);
      else
        flags = flags | QTextOption::ShowTabsAndSpaces;
      textOption.setFlags(flags);
      document->setDefaultTextOption(textOption);
      editor->setDocument(document);
    }
  }
}

bool TextViewer::eventFilter(QObject* object, QEvent* event)
{
  if (event->type() == QEvent::ShortcutOverride) {
    QKeyEvent* keyEvent = static_cast<QKeyEvent*>(event);
    if (keyEvent->matches(QKeySequence::Find)) {
      find();
    } else if (keyEvent->matches(QKeySequence::FindNext)) {
      findNext();
    }
  }
  return QDialog::eventFilter(object, event);
}

void TextViewer::setDescription(const QString& description)
{
  QLabel* descriptionLabel = findChild<QLabel*>("descriptionLabel");
  descriptionLabel->setText(description);
}

void TextViewer::saveFile(const QTextEdit* editor)
{
  trySaveEditor(const_cast<QTextEdit*>(editor));
}

void TextViewer::saveFile()
{
  QWidget* currentPage = m_EditorTabs->currentWidget();
  QTextEdit* editor    = currentPage->findChild<QTextEdit*>("editorView");
  if (trySaveEditor(editor)) {
    m_Modified.erase(editor);
  }
}

void TextViewer::modified()
{
  QWidget* currentPage = m_EditorTabs->currentWidget();
  QTextEdit* editor    = currentPage->findChild<QTextEdit*>("editorView");

  m_Modified.insert(editor);
}

void TextViewer::addFile(const QString& fileName, bool writable)
{
  QFile file(fileName);
  if (!file.open(QIODevice::ReadOnly)) {
    throw Exception(tr("file not found: %1").arg(fileName));
  }
  QByteArray temp = file.readAll();
  if (file.error() != QFileDevice::NoError) {
    throw Exception(tr("failed to read: %1").arg(fileName));
  }

  QString encoding;
  bool needsBOM = false;
  const QString text = decodeTextData(temp, &encoding, &needsBOM);

  QWidget* page           = new QWidget();
  QVBoxLayout* layout     = new QVBoxLayout(page);
  QTextEdit* editor       = new QTextEdit(page);
  QTextDocument* document = new QTextDocument(page);
  if (ui->showWhitespace->isChecked()) {
    QTextOption option;
    option.setFlags(QTextOption::ShowTabsAndSpaces);
    document->setDefaultTextOption(option);
  }
  editor->setDocument(document);
  editor->setAcceptRichText(false);
  editor->setPlainText(text);
  editor->setLineWrapMode(QTextEdit::NoWrap);
  editor->setObjectName("editorView");
  editor->setDocumentTitle(fileName);
  editor->setProperty("mo2TextEncoding", encoding);
  editor->setProperty("mo2TextNeedsBOM", needsBOM);
  editor->installEventFilter(this);
  editor->setReadOnly(!writable);

  // set text highlighting color in inactive window equal to text hightlighting color in
  // active window
  QPalette palette = editor->palette();
  palette.setColor(QPalette::Inactive, QPalette::Highlight,
                   palette.color(QPalette::Active, QPalette::Highlight));
  palette.setColor(QPalette::Inactive, QPalette::HighlightedText,
                   palette.color(QPalette::Active, QPalette::HighlightedText));
  editor->setPalette(palette);

  // add hotkeys for searching through the document
  QAction* findAction = new QAction(QString("&Find"), editor);
  findAction->setShortcut(QKeySequence::Find);
  editor->addAction(findAction);
  QAction* findNextAction = new QAction(QString("Find &Next"), editor);
  findAction->setShortcut(QKeySequence::FindNext);
  editor->addAction(findNextAction);

  layout->addWidget(editor);
  if (writable) {
    QPushButton* saveBtn = new QPushButton(tr("Save"), page);
    layout->addWidget(saveBtn);
    connect(saveBtn, SIGNAL(clicked()), this, SLOT(saveFile()));
    connect(document, &QTextDocument::modificationChanged, this,
            [this, editor](bool changed) {
              if (changed) {
                m_Modified.insert(editor);
              } else {
                m_Modified.erase(editor);
              }
            });
  }
  page->setLayout(layout);
  m_EditorTabs->addTab(page, QFileInfo(fileName).fileName());
}
}  // namespace MOBase
