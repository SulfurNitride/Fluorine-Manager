#include "NewVegasDummyPluginService.h"

#include <QMessageBox>

namespace BsaPacker
{
	NewVegasDummyPluginService::NewVegasDummyPluginService(
		const IFileWriterService* fileWriterService,
		const IDummyPluginLogic* dummyPluginLogic)
		: m_FileWriterService(fileWriterService),
		m_DummyPluginLogic(dummyPluginLogic)
	{
	}

	bool NewVegasDummyPluginService::CreatePlugin(const QString& modPath,
		const QString& archiveNameBase) const
	{
		const QString& absoluteFileNameNoExtension = modPath + '/' + archiveNameBase;
		if (!this->m_DummyPluginLogic->canCreateDummyESP(absoluteFileNameNoExtension, bsa_archive_type_e::baFO3))
		{
			return false;
		}
		const std::string& absoluteFileName = absoluteFileNameNoExtension.toStdString() + ".esp";
		return this->m_FileWriterService->Write(absoluteFileName,
			reinterpret_cast<const char*>(NewVegasDummyPluginService::RAW_NEWVEGAS),
			sizeof(NewVegasDummyPluginService::RAW_NEWVEGAS));
	}
}
