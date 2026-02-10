#include "OverrideFileService.h"

#include <questionboxmemory.h>
#include <QApplication>
#include <QDialogButtonBox>

namespace BsaPacker {
	const uint16_t FALLOUT_3_NEXUS_ID = 120;
	const uint16_t NEW_VEGAS_NEXUS_ID = 130;

	OverrideFileService::OverrideFileService(
		const IFileWriterService* fileWriterService)
		: m_FileWriterService(fileWriterService)
	{
	}

	// TODO: Add detection for Command Extender and JIP LN NVSE and warn if missing
	bool OverrideFileService::CreateOverrideFile(const int nexusId,
		const QString& modPath,
		const QStringList& archiveNames) const {

		if (nexusId != FALLOUT_3_NEXUS_ID && nexusId != NEW_VEGAS_NEXUS_ID) {
			return false;
		}

		if (MOBase::QuestionBoxMemory::query(QApplication::activeModalWidget(), "BSAPacker", "Create .override file?",
			"Do you want to create an override file for the archive(s)?",
			QDialogButtonBox::No | QDialogButtonBox::Yes, QDialogButtonBox::No) & QDialogButtonBox::No) {
			return false;
		}

		bool res = true;
		for (const QString& baseName : archiveNames) {
			const QString& fileNameNoExtension = modPath + '/' + baseName;
			const std::string& absoluteFileName = fileNameNoExtension.toStdString() + ".override";
			if (!this->m_FileWriterService->Write(absoluteFileName, nullptr, 0)) {
				qWarning() << "Failed to create" << absoluteFileName;
				res = false;
			}
		}

		return res;
	}
} // namespace BsaPacker
