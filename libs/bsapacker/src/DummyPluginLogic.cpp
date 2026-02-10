#include "DummyPluginLogic.h"

#include <QFileInfo>
#include <array>
#include "SettingsService.h"

namespace BsaPacker
{
	DummyPluginLogic::DummyPluginLogic(
		const ISettingsService* settingsService,
		const IArchiveNameService* archiveNameService)
		: m_SettingsService(settingsService),
		m_ArchiveNameService(archiveNameService)
	{
	}

	bool DummyPluginLogic::canCreateDummyESP(const QString& fileNameNoExtension, const bsa_archive_type_e type) const
	{
		const std::array<QString, 2>& fileList = {
			fileNameNoExtension + ".esm",
			fileNameNoExtension + ".esp"
		};
		return this->canCreateDummy(fileList, fileNameNoExtension, type);
	}

	bool DummyPluginLogic::canCreateDummyESL(const QString& fileNameNoExtension, const bsa_archive_type_e type) const
	{
		const std::array<QString, 3>& fileList = {
			fileNameNoExtension + ".esm",
			fileNameNoExtension + ".esp",
			fileNameNoExtension + ".esl"
		};
		return this->canCreateDummy(fileList, fileNameNoExtension, type);
	}

	template<std::size_t SIZE>
	bool DummyPluginLogic::canCreateDummy(const std::array<QString, SIZE>& fileList,
		const QString& fileNameNoExtension,
		const bsa_archive_type_e type) const
	{
		const QFileInfo archive(fileNameNoExtension + this->m_ArchiveNameService->Infix(type) + this->m_ArchiveNameService->GetFileExtension());
		if (!(archive.exists() && archive.isFile())) {
			return false;
		}

		if (!this->m_SettingsService->GetPluginSetting(SettingsService::SETTING_CREATE_PLUGINS).toBool()) {
			return false;
		}

		for (const QString& info : fileList) {
			const QFileInfo fileInfo(info);
			if (fileInfo.exists() && fileInfo.isFile()) {
				return false;
			}
		}
		return true;
	}
}
