#include <bsapacker/ArchiveBuildDirector.h>

#include <QProgressDialog>

namespace BsaPacker
{
	ArchiveBuildDirector::ArchiveBuildDirector(
			const ISettingsService* settingsService,
			IArchiveBuilder* archiveFileBuildService)
		: m_ArchiveFileBuildService(archiveFileBuildService),
		  m_SettingsService(settingsService)
	{
	}

	void ArchiveBuildDirector::Construct()
	{
		QProgressDialog dialog("Adding to archive",
							   "Abort",
							   0,
							   static_cast<int>(this->m_ArchiveFileBuildService->getFileCount()));
		QObject::connect(&dialog, &QProgressDialog::canceled, this->m_ArchiveFileBuildService, &IArchiveBuilder::cancel);
		QObject::connect(this->m_ArchiveFileBuildService, SIGNAL(valueChanged(int)), &dialog, SLOT(setValue(int)));
		dialog.show();
		this->m_ArchiveFileBuildService->setShareData(true);
		this->m_ArchiveFileBuildService->setFiles();
		dialog.close();
	}
} // namespace BsaPacker
