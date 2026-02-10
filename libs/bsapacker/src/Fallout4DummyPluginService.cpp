#include "Fallout4DummyPluginService.h"

namespace BsaPacker
{
	Fallout4DummyPluginService::Fallout4DummyPluginService(
		const IFileWriterService* fileWriterService,
		const IDummyPluginLogic* dummyPluginLogic)
		: m_FileWriterService(fileWriterService),
		m_DummyPluginLogic(dummyPluginLogic)
	{
	}

	bool Fallout4DummyPluginService::CreatePlugin(const QString& modPath,
		const QString& archiveNameBase) const
	{
		const QString& fileNameNoExtension = modPath + '/' + archiveNameBase;
		if (!this->m_DummyPluginLogic->canCreateDummyESL(fileNameNoExtension, bsa_archive_type_e::baFO4) && !this->m_DummyPluginLogic->canCreateDummyESL(fileNameNoExtension, bsa_archive_type_e::baFO4dds))
		{
			return false;
		}
		const std::string& absoluteFileName = fileNameNoExtension.toStdString() + ".esp";
		return this->m_FileWriterService->Write(absoluteFileName,
			reinterpret_cast<const char*>(Fallout4DummyPluginService::RAW_FALLOUT4),
			sizeof(Fallout4DummyPluginService::RAW_FALLOUT4));
	}
}
