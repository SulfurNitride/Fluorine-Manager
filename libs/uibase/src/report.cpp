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

#include <uibase/report.h>
#include <uibase/expanderwidget.h>
#include <uibase/log.h>
#include <uibase/questionboxmemory.h>
#include "ui_taskdialog.h"
#include <uibase/utility.h>
#include <QComboBox>
#include <QMainWindow>
#include <QRadioButton>

namespace MOBase
{

QWidget* topLevelWindow()
{
  if (!qApp) {
    return nullptr;
  }

  for (QWidget* w : qApp->topLevelWidgets()) {
    if (dynamic_cast<QMainWindow*>(w)) {
      return w;
    }
  }

  return nullptr;
}

void criticalOnTop(const QString& message)
{
  QMessageBox mb(QMessageBox::Critical, "Mod Organizer", message);

  mb.show();
  mb.activateWindow();
  mb.raise();
  mb.exec();
}

void reportError(const QString& message)
{
  log::error("{}", message);

  if (QApplication::topLevelWidgets().count() != 0) {
    if (auto* mw = topLevelWindow()) {
      QMessageBox::warning(mw, QObject::tr("Error"), message, QMessageBox::Ok);
    } else {
      criticalOnTop(message);
    }
  } else {
    // No top-level widget available, show a standalone critical dialog
    criticalOnTop(message);
  }
}

TaskDialogButton::TaskDialogButton(QString t, QString d, QMessageBox::StandardButton b)
    : text(std::move(t)), description(std::move(d)), button(b)
{}

TaskDialogButton::TaskDialogButton(QString t, QMessageBox::StandardButton b)
    : TaskDialogButton(std::move(t), {}, b)
{}

TaskDialog::TaskDialog(QWidget* parent, QString title)
    : m_dialog(new QDialog(parent)), ui(new Ui::TaskDialog), m_title(std::move(title)),
      m_icon(QMessageBox::NoIcon), m_result(QMessageBox::Cancel), m_width(-1),
      m_rememberCheck(nullptr), m_rememberCombo(nullptr)
{
  ui->setupUi(m_dialog.get());
}

TaskDialog::~TaskDialog() = default;

TaskDialog& TaskDialog::title(const QString& s)
{
  m_title = s;
  return *this;
}

TaskDialog& TaskDialog::main(const QString& s)
{
  m_main = s;
  return *this;
}

TaskDialog& TaskDialog::content(const QString& s)
{
  m_content = s;
  return *this;
}

TaskDialog& TaskDialog::details(const QString& s)
{
  m_details = s;
  return *this;
}

TaskDialog& TaskDialog::icon(QMessageBox::Icon i)
{
  m_icon = i;
  return *this;
}

TaskDialog& TaskDialog::button(TaskDialogButton b)
{
  m_buttons.emplace_back(std::move(b));
  return *this;
}

TaskDialog& TaskDialog::remember(const QString& action, const QString& file)
{
  m_rememberAction = action;
  m_rememberFile   = file;
  return *this;
}

void TaskDialog::addContent(QWidget* w)
{
  auto* ly = static_cast<QVBoxLayout*>(ui->contentPanel->layout());

  // add before spacer
  ly->insertWidget(ly->count() - 1, w);
}

void TaskDialog::setWidth(int w)
{
  m_width = w;
}

QMessageBox::StandardButton TaskDialog::exec()
{
  const auto b = checkMemory();
  if (b != QMessageBox::NoButton) {
    return b;
  }

  setDialog();
  setWidgets();
  setChoices();
  setButtons();
  setDetails();

  if (m_width >= 0) {
    ui->topPanel->setMinimumWidth(m_width);
  } else {
    ui->topPanel->setMinimumWidth(400);
  }

  m_dialog->adjustSize();

  if (!m_dialog->parent()) {
    // no parent, make sure it's shown on top
    m_dialog->show();
    m_dialog->activateWindow();
    m_dialog->raise();
  }

  if (m_dialog->exec() != QDialog::Accepted) {
    return QMessageBox::Cancel;
  }

  rememberChoice();
  return m_result;
}

QMessageBox::StandardButton TaskDialog::checkMemory() const
{
  if (m_rememberAction.isEmpty() && m_rememberFile.isEmpty()) {
    return QMessageBox::NoButton;
  }

  const auto b = QuestionBoxMemory::getMemory(m_rememberAction, m_rememberFile);

  const auto logName = m_rememberAction +
                       (m_rememberFile.isEmpty() ? "" : QString("/") + m_rememberFile);

  if (b == QDialogButtonBox::NoButton) {
    log::debug("{}: asking because the user has not set a choice before", logName);
  } else {
    log::debug("{}: not asking because user always wants response {}", logName,
               QuestionBoxMemory::buttonToString(b));
  }

  return static_cast<QMessageBox::StandardButton>(b);
}

void TaskDialog::rememberChoice()
{
  if (m_rememberAction.isEmpty() && m_rememberFile.isEmpty()) {
    // nothing
    return;
  }

  const auto b = static_cast<QuestionBoxMemory::Button>(m_result);

  if (m_rememberCheck) {
    // action only
    if (m_rememberCheck->isChecked()) {
      QuestionBoxMemory::setWindowMemory(m_rememberAction, b);
    }
  } else if (m_rememberCombo) {
    Q_ASSERT(m_rememberCombo->count() == 3);

    if (m_rememberCombo->currentIndex() == 1) {
      // remember action
      QuestionBoxMemory::setWindowMemory(m_rememberAction, b);
    } else if (m_rememberCombo->currentIndex() == 2) {
      // remember file
      QuestionBoxMemory::setFileMemory(m_rememberAction, m_rememberFile, b);
    }
  }
}

void TaskDialog::setButtons()
{
  if (m_buttons.empty()) {
    setStandardButtons();
  } else {
    setCommandButtons();
  }

  m_expander.reset(new ExpanderWidget(ui->detailsExpander, ui->detailsWidget));
}

void TaskDialog::setStandardButtons()
{
  ui->standardButtonsPanel->show();
  ui->commandButtonsPanel->hide();

  for (auto* b : ui->standardButtons->buttons()) {
    ui->standardButtons->removeButton(b);
  }

  ui->standardButtons->addButton(QDialogButtonBox::Ok);

  QObject::connect(ui->standardButtons, &QDialogButtonBox::clicked, [&](auto* b) {
    m_result = static_cast<QMessageBox::StandardButton>(
        ui->standardButtons->standardButton(b));

    m_dialog->accept();
  });
}

void TaskDialog::setCommandButtons()
{
  ui->standardButtonsPanel->hide();
  ui->commandButtonsPanel->show();

  deleteChildWidgets(ui->commandButtons);

  for (auto&& b : m_buttons) {
    auto* cb = new QCommandLinkButton(b.text, b.description);

    QObject::connect(cb, &QAbstractButton::clicked, [&] {
      m_result = b.button;
      m_dialog->accept();
    });

    ui->commandButtons->layout()->addWidget(cb);
  }
}

void TaskDialog::setDetails()
{
  if (m_details.isEmpty()) {
    ui->detailsExpander->setEnabled(false);
    return;
  }

  ui->details->setPlainText(m_details);

  setVisibleLines(ui->details, 10);
  setFontPercent(ui->details, 0.9);

  const QColor bg = detailsColor();

  ui->detailsPanel->setStyleSheet(QString("background-color: rgb(%1, %2, %3)")
                                      .arg(bg.redF() * 255)
                                      .arg(bg.greenF() * 255)
                                      .arg(bg.blueF() * 255));
}

void TaskDialog::setDialog()
{
#ifdef _WIN32
  m_dialog->setWindowFlags(Qt::Dialog | Qt::MSWindowsFixedSizeDialogHint);
#else
  m_dialog->setWindowFlags(Qt::Dialog);
#endif
  m_dialog->layout()->setSizeConstraint(QLayout::SetFixedSize);

  m_dialog->setWindowTitle(m_title);
}

void TaskDialog::setWidgets()
{
  ui->main->setText(m_main);
  setFontPercent(ui->main, 1.5);

  ui->content->setText(m_content);
  ui->content->setVisible(!m_content.isEmpty());

  auto icon = standardIcon(m_icon);

  if (icon.isNull()) {
    ui->iconPanel->hide();
  } else {
    ui->iconPanel->show();
    ui->icon->setPixmap(std::move(icon));
  }
}

void TaskDialog::setChoices()
{
  if (m_rememberAction.isEmpty() && m_rememberFile.isEmpty()) {
    ui->rememberPanel->hide();
    return;
  }

  ui->rememberPanel->show();
  deleteChildWidgets(ui->rememberPanel);

  const auto tooltip = QObject::tr(
      "You can reset these choices by clicking \"Reset Dialog Choices\" in the "
      "General tab of the Settings");

  if (!m_rememberAction.isEmpty() && !m_rememberFile.isEmpty()) {
    // both
    m_rememberCombo = new QComboBox;
    m_rememberCombo->setToolTip(tooltip);

    m_rememberCombo->addItem(QObject::tr("Always ask"));
    m_rememberCombo->addItem(QObject::tr("Remember my choice"));
    m_rememberCombo->addItem(
        QObject::tr("Remember my choice for %1").arg(m_rememberFile));

    ui->rememberPanel->layout()->setAlignment(Qt::AlignLeft);
    ui->rememberPanel->layout()->addWidget(m_rememberCombo);
  } else if (!m_rememberAction.isEmpty() || !m_rememberFile.isEmpty()) {
    // either
    m_rememberCheck = new QCheckBox(QObject::tr("Remember my choice"));
    m_rememberCheck->setToolTip(tooltip);
    ui->rememberPanel->layout()->addWidget(m_rememberCheck);
  }
}

QColor TaskDialog::detailsColor() const
{
  auto b = std::make_unique<QPushButton>();
  m_dialog->style()->polish(b.get());
  return b->palette().color(b->backgroundRole());
}

QPixmap TaskDialog::standardIcon(QMessageBox::Icon icon) const
{
  QStyle* s = m_dialog->style();
  QIcon i;

  switch (icon) {
  case QMessageBox::Information:
    i = s->standardIcon(QStyle::SP_MessageBoxInformation, 0, m_dialog.get());
    break;
  case QMessageBox::Warning:
    i = s->standardIcon(QStyle::SP_MessageBoxWarning, 0, m_dialog.get());
    break;
  case QMessageBox::Critical:
    i = s->standardIcon(QStyle::SP_MessageBoxCritical, 0, m_dialog.get());
    break;
  case QMessageBox::Question:
    i = s->standardIcon(QStyle::SP_MessageBoxQuestion, 0, m_dialog.get());
    break;
  case QMessageBox::NoIcon:
    [[fallthrough]];
  default:
    break;
  }

  if (i.isNull()) {
    return {};
  }

  QWindow* window = m_dialog->windowHandle();
  if (!window) {
    if (const QWidget* nativeParent = m_dialog->nativeParentWidget())
      window = nativeParent->windowHandle();
  }

  const int iconSize = s->pixelMetric(QStyle::PM_MessageBoxIconSize, 0, m_dialog.get());

  return i.pixmap(QSize(iconSize, iconSize));
}

void TaskDialog::setVisibleLines(QPlainTextEdit* w, int lines)
{
  QTextDocument* d = w->document();
  QFontMetrics fm(d->defaultFont());

  const QMargins margins = ui->details->contentsMargins();

  double height = 0;

  // lines
  height += fm.lineSpacing() * lines;

  // top and bottom margins for document and frame
  height += (d->documentMargin() + ui->details->frameWidth()) * 2;

  // widget margins
  height += margins.top() + margins.bottom();

  w->setMaximumHeight(static_cast<int>(std::round(height)));
}

void TaskDialog::setFontPercent(QWidget* w, double p)
{
  auto f = w->font();

  if (f.pointSizeF() > 0) {
    f.setPointSizeF(f.pointSizeF() * p);
  } else if (f.pixelSize() > 0) {
    f.setPixelSize(static_cast<int>(std::round(f.pixelSize() * p)));
  }

  w->setFont(f);
}

}  // namespace MOBase
