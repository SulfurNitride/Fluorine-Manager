#include "PackerDialog.h"

#include <QObject>
#include <QMessageBox>
#include <QPushButton>
#include <QStringList>

namespace BsaPacker
{
	constexpr uint32_t DIALOG_WIDTH = 240;
	constexpr uint32_t DIALOG_HEIGHT = 200;
	constexpr uint32_t COMBO_MIN_WIDTH = 100;
	constexpr uint32_t LIST_MIN_HEIGHT = 100;

	PackerDialog::PackerDialog(const IModContext* modContext) :
		m_ModContext(modContext),
		buttonsOkCancelMod(QDialogButtonBox::Ok | QDialogButtonBox::Cancel)
	{
		QPushButton* ok = this->buttonsOkCancelMod.button(QDialogButtonBox::Ok);
		ok->setEnabled(false);

		this->labelChooseMod.setText(QObject::tr("Choose a mod to pack:"));
		this->labelChooseName.setText(QObject::tr("Choose a name for the packed archive:"));

		this->resize(DIALOG_WIDTH, DIALOG_HEIGHT);
		this->setWindowTitle(QObject::tr("BSA Packer"));

		this->comboModList.setMinimumWidth(COMBO_MIN_WIDTH);

		this->listArchiveNames.setMinimumHeight(LIST_MIN_HEIGHT);

		this->layoutHorizontal.addStretch(1);
		this->layoutHorizontal.addWidget(&buttonsOkCancelMod);

		this->layoutVertical.addWidget(&labelChooseMod);
		this->layoutVertical.addWidget(&comboModList);
		this->layoutVertical.addWidget(&labelChooseName);
		this->layoutVertical.addWidget(&listArchiveNames);
		this->layoutVertical.addLayout(&layoutHorizontal);

		this->setLayout(&layoutVertical);

		QObject::connect(&listArchiveNames, &QListWidget::itemSelectionChanged, this, &PackerDialog::RefreshOkButton, Qt::QueuedConnection);
		QObject::connect(&listArchiveNames, qOverload<QListWidgetItem*>(&QListWidget::itemDoubleClicked), this, &QDialog::accept);
		QObject::connect(&buttonsOkCancelMod, &QDialogButtonBox::accepted, this, &QDialog::accept);
		QObject::connect(&buttonsOkCancelMod, &QDialogButtonBox::rejected, this, &QDialog::reject);
		QObject::connect(&comboModList, qOverload<const QString&>(&QComboBox::currentTextChanged), [this](auto&& text) { this->UpdateNameList(text); });
	}

	bool PackerDialog::IsNewFilename() const
	{
		const int currentIndex = this->listArchiveNames.currentIndex().row() + 1;
		const int count = this->listArchiveNames.count();
		return currentIndex == count;
	}

	QString PackerDialog::SelectedMod() const
	{
		return this->comboModList.currentText();
	}

	QString PackerDialog::SelectedPluginItem() const
	{
		const QListWidgetItem* const currentItem = this->listArchiveNames.currentItem();
		return currentItem != nullptr ? currentItem->text() : QString();
	}

	void PackerDialog::RefreshModList()
	{
		const QStringList validMods = this->m_ModContext->GetValidMods();
		this->comboModList.clear();
		this->comboModList.addItems(validMods);
	}

	void PackerDialog::UpdateNameList(const QString& modName)
	{
		this->listArchiveNames.clear();
		const QString& modPath = this->m_ModContext->GetAbsoluteModPath(modName);
		const QStringList& filenames = this->m_ModContext->GetPlugins(modPath);
		this->listArchiveNames.addItems(filenames);
	}

	int PackerDialog::Exec()
	{
	 	return QDialog::exec();
	}

	void PackerDialog::RefreshSelectedName()
	{
		Q_EMIT this->comboModList.currentTextChanged(this->comboModList.currentText());
	}

	void PackerDialog::RefreshOkButton()
	{
		QPushButton* ok = this->buttonsOkCancelMod.button(QDialogButtonBox::Ok);
		ok->setEnabled(this->listArchiveNames.currentItem() != nullptr);
	}
} // namespace BsaPacker
