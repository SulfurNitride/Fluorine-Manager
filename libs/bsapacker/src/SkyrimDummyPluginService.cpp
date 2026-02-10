#include "SkyrimDummyPluginService.h"

namespace BsaPacker
{
	SkyrimDummyPluginService::SkyrimDummyPluginService(
		const IFileWriterService* fileWriterService,
		const IDummyPluginLogic* dummyPluginLogic)
		: m_FileWriterService(fileWriterService),
		m_DummyPluginLogic(dummyPluginLogic)
	{
	}

	bool SkyrimDummyPluginService::CreatePlugin(const QString& modPath,
		const QString& archiveNameBase) const
	{
		const QString& fileNameNoExtension = modPath + '/' + archiveNameBase;
		if (!this->m_DummyPluginLogic->canCreateDummyESP(fileNameNoExtension, bsa_archive_type_e::baFO3))
		{
			return false;
		}
		const std::string& absoluteFileName = fileNameNoExtension.toStdString() + ".esp";
		return this->m_FileWriterService->Write(absoluteFileName,
			reinterpret_cast<const char*>(SkyrimDummyPluginService::RAW_SKYRIM),
			sizeof(SkyrimDummyPluginService::RAW_SKYRIM));
	}
}
