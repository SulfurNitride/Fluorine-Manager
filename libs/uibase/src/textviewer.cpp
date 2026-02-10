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
  if (!m_Modified.empty()) {
    for (std::set<QTextEdit*>::iterator iter = m_Modified.begin();
         iter != m_Modified.end(); ++iter) {
      QMessageBox::StandardButton res = QMessageBox::question(
          this, tr("Save changes?"),
          tr("Do you want to save changes to %1?").arg((*iter)->documentTitle()),
          QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);
      if (res == QMessageBox::Yes) {
        saveFile(*iter);
      } else if (res == QMessageBox::Cancel) {
        event->ignore();
        break;
      }
    }
  }
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
  bool write                                = true;
  QMessageBox::StandardButton buttonPressed = QMessageBox::Ignore;
  QFile file(editor->documentTitle());
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    write = false;
    QFileInfo fileInfo(file.fileName());
    buttonPressed =
        MOBase::TaskDialog(qApp->activeModalWidget(),
                           QObject::tr("INI file is read-only"))
            .main(QObject::tr("INI file is read-only"))
            .content(QObject::tr("Mod Organizer is attempting to write to \"%1\" which "
                                 "is currently set to read-only.")
                         .arg(fileInfo.fileName()))
            .icon(QMessageBox::Warning)
            .button({QObject::tr("Clear the read-only flag"), QMessageBox::Yes})
            .button({QObject::tr("Allow the write once"),
                     QObject::tr("The file will be set to read-only again."),
                     QMessageBox::Ignore})
            .button({QObject::tr("Skip this file"), QMessageBox::No})
            .remember("clearReadOnly", fileInfo.fileName())
            .exec();

    if (buttonPressed & (QMessageBox::Yes | QMessageBox::Ignore)) {
      file.setPermissions(file.permissions() | QFile::WriteUser);
    }

    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
      reportError(tr("failed to write to %1").arg(editor->documentTitle()));
    } else {
      write = true;
    }
  }

  if (write) {
    file.write(editor->toPlainText().toUtf8().replace('\n', "\r\n"));
    file.close();
  }

  if (buttonPressed == QMessageBox::Ignore) {
    file.setPermissions(file.permissions() & ~(QFile::WriteUser));
  }
}

void TextViewer::saveFile()
{
  QWidget* currentPage = m_EditorTabs->currentWidget();
  QTextEdit* editor    = currentPage->findChild<QTextEdit*>("editorView");
  saveFile(editor);

  m_Modified.erase(editor);
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
  editor->setPlainText(QString(temp));
  editor->setLineWrapMode(QTextEdit::NoWrap);
  editor->setObjectName("editorView");
  editor->setDocumentTitle(fileName);
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
    connect(editor, SIGNAL(textChanged()), this, SLOT(modified()));
  }
  page->setLayout(layout);
  m_EditorTabs->addTab(page, QFileInfo(fileName).fileName());
}
}  // namespace MOBase
