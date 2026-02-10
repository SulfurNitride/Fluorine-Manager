#include "multiarchivedialog.h"
#include "ui_multiarchivedialog.h"

using namespace MOBase;

MultiArchiveDialog::MultiArchiveDialog(
    std::vector<std::shared_ptr<const MOBase::FileTreeEntry>> entries, QWidget* parent)
    : QDialog(parent), ui(new Ui::MultiArchiveDialog), m_Manual(false),
      m_SelectedEntry(nullptr)
{
  ui->setupUi(this);

  auto layout = ui->middleLayout;
  for (auto& entry : entries) {
    QPushButton* button = new QPushButton(entry->name(), this);
    button->setStyleSheet("text-align: left; padding: 10px; font-weight: bold;");
    layout->addWidget(button);

    connect(button, &QPushButton::clicked, [this, entry]() {
      m_SelectedEntry = entry;
      this->accept();
    });
  }
}

MultiArchiveDialog::~MultiArchiveDialog()
{
  delete ui;
}

void MultiArchiveDialog::on_cancelBtn_clicked()
{
  this->reject();
}

void MultiArchiveDialog::on_manualBtn_clicked()
{
  m_Manual = true;
  this->reject();
}
