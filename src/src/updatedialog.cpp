#include "updatedialog.h"
#include "ui_updatedialog.h"

using namespace MOBase;

UpdateDialog::UpdateDialog(QWidget* parent)
    : QDialog(parent, Qt::WindowMaximizeButtonHint | Qt::WindowCloseButtonHint),
      ui(new Ui::UpdateDialog)
{
  // Basic UI stuff
  ui->setupUi(this);
  connect(ui->installButton, &QPushButton::pressed, this, [&] {
    done(QDialog::Accepted);
  });
  connect(ui->cancelButton, &QPushButton::pressed, this, [&] {
    done(QDialog::Rejected);
  });

  // Replace a label with an icon
  QIcon icon     = style()->standardIcon(QStyle::SP_MessageBoxQuestion);
  QPixmap pixmap = icon.pixmap(QSize(32, 32));
  ui->iconLabel->setPixmap(pixmap);
  ui->iconLabel->setScaledContents(true);

  // Setting up the expander
  m_expander.set(ui->detailsButton, ui->detailsWidget);
  connect(&m_expander, &ExpanderWidget::toggled, this, [&] {
    adjustSize();
  });

  // Adjust sizes after the expander hides stuff
  adjustSize();
}

UpdateDialog::~UpdateDialog() = default;

void UpdateDialog::setChangeLogs(const QString& text)
{
  m_changeLogs.setText(text);
  // The generated UI uses QTextBrowser. The dedicated Nexus authorization
  // window owns the WebEngine surface; update notes remain lightweight.
  ui->detailsWebView->setHtml(text);
}

void UpdateDialog::setVersions(const QString& oldVersion, const QString& newVersion)
{
  ui->updateLabel->setText(tr("Mod Organizer %1 is available.  The current version is "
                              "%2.  Updating will not affect your mods or profiles.")
                               .arg(newVersion)
                               .arg(oldVersion));
}
