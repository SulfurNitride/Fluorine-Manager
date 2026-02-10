#include <BsaPacker.h>

#include <bsapacker/ArchiveAutoService.h>
#include <bsapacker/ArchiveBuildDirector.h>
#include <bsapacker/ArchiveBuilderHelper.h>
#include <bsapacker/ArchiveBuilderFactory.h>
#include <ArchiveNameService.h>
#include "BsaPackerWorker.h"
#include "DummyPluginLogic.h"
#include "DummyPluginServiceFactory.h"
#include "FileWriterService.h"
#include "HideLooseAssetService.h"
#include "ModContext.h"
#include "ModDto.h"
#include "OverrideFileService.h"
#include "PackerDialog.h"
#include "SettingsService.h"
#include <bsapacker/ModDtoFactory.h>
#include <QMessageBox>
#include <iplugingame.h>

namespace BsaPacker
{
	bool Bsa_Packer::init(MOBase::IOrganizer* moInfo)
	{
		this->m_Organizer = moInfo;
		this->m_ModContext = std::make_unique<ModContext>(this->m_Organizer);
		this->m_SettingsService = std::make_unique<SettingsService>(this->m_Organizer);
		return true;
	}

	QString Bsa_Packer::name() const
	{
		return QStringLiteral("BSA Packer");
	}

	QString Bsa_Packer::author() const
	{
		return QStringLiteral("MattyFez & MO2 Team");
	}

	QString Bsa_Packer::description() const
	{
		return tr("Transform loose files into a Bethesda Softworks Archive file (.bsa/.ba2).");
	}

	MOBase::VersionInfo Bsa_Packer::version() const
	{
		return MOBase::VersionInfo(1, 1, 0, MOBase::VersionInfo::RELEASE_FINAL);
	}

	QList<MOBase::PluginSetting> Bsa_Packer::settings() const
	{
		return SettingsService::PluginSettings;
	}

	QString Bsa_Packer::tooltip() const
	{
		return tr("Transform loose files into a Bethesda Softworks Archive file (.bsa/.ba2).");
	}

	QIcon Bsa_Packer::icon() const
	{
		return QIcon();
	}

	QString Bsa_Packer::displayName() const
	{
		return tr("BSA Packer");
	}

	void Bsa_Packer::display() const
	{
		ArchiveBuilderHelper archiveBuilderHelper(this->m_SettingsService.get());
		ArchiveBuilderFactory archiveBuilderFactory(&archiveBuilderHelper);
		ArchiveAutoService archiveAutoService;
		FileWriterService fileWriterService;
		ArchiveNameService archiveNameService(this->m_ModContext.get());
		DummyPluginLogic dummyPluginLogic(this->m_SettingsService.get(), &archiveNameService);
		DummyPluginServiceFactory dummyPluginServiceFactory(
			this->m_ModContext.get(), &fileWriterService, &dummyPluginLogic);
		HideLooseAssetService hideLooseAssetService(this->m_SettingsService.get());
		PackerDialog packerDialog(this->m_ModContext.get());
		ModDtoFactory modDtoFactory(this->m_ModContext.get(), &packerDialog);
		OverrideFileService overrideFileService(&fileWriterService);

		BsaPackerWorker worker(
			this->m_SettingsService.get(),
			&modDtoFactory,
			&archiveBuilderFactory,
			&archiveAutoService,
			&dummyPluginServiceFactory,
			&hideLooseAssetService,
			&archiveNameService,
			&overrideFileService);
		worker.DoWork();
	}

} // namespace BsaPacker
