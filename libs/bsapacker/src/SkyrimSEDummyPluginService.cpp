#include "SkyrimSEDummyPluginService.h"

namespace BsaPacker
{
	SkyrimSEDummyPluginService::SkyrimSEDummyPluginService(
		const IFileWriterService* fileWriterService,
		const IDummyPluginLogic* dummyPluginLogic)
		: m_FileWriterService(fileWriterService),
		m_DummyPluginLogic(dummyPluginLogic)
	{
	}

	bool SkyrimSEDummyPluginService::CreatePlugin(const QString& modPath,
		const QString& archiveNameBase) const
	{
		const QString& fileNameNoExtension = modPath + '/' + archiveNameBase;
		if (!this->m_DummyPluginLogic->canCreateDummyESP(fileNameNoExtension, bsa_archive_type_e::baSSE))
		{
			return false;
		}
		const std::string& absoluteFileName = fileNameNoExtension.toStdString() + ".esp";
		return this->m_FileWriterService->Write(absoluteFileName,
			reinterpret_cast<const char*>(SkyrimSEDummyPluginService::RAW_SKYRIMSE),
			sizeof(SkyrimSEDummyPluginService::RAW_SKYRIMSE));
	}
}
