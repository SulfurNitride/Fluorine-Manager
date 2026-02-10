#include "BsaPackerWorker.h"

#include <QMessageBox>

#include <bsapacker/ArchiveBuildDirector.h>
#include <bsapacker/ModDtoFactory.h>

namespace BsaPacker
{
	BsaPackerWorker::BsaPackerWorker(
		const ISettingsService* settingsService,
		const IModDtoFactory* modDtoFactory,
		const IArchiveBuilderFactory* archiveBuilderFactory,
		const IArchiveAutoService* archiveAutoService,
		const IDummyPluginServiceFactory* dummyPluginServiceFactory,
		const IHideLooseAssetService* hideLooseAssetService,
		const IArchiveNameService* archiveNameService,
		const IOverrideFileService* overrideFileService) :
		m_SettingsService(settingsService),
		m_ModDtoFactory(modDtoFactory),
		m_ArchiveBuilderFactory(archiveBuilderFactory),
		m_ArchiveAutoService(archiveAutoService),
		m_DummyPluginServiceFactory(dummyPluginServiceFactory),
		m_HideLooseAssetService(hideLooseAssetService),
		m_ArchiveNameService(archiveNameService),
		m_OverrideFileService(overrideFileService)
	{
	}

	void BsaPackerWorker::DoWork() const
	{
		QStringList createdArchives;
		const std::unique_ptr<IModDto> modDto = this->m_ModDtoFactory->Create(); // handles PackerDialog and validation, implements Null Object pattern
		const std::vector<bsa_archive_type_e> types = this->m_ArchiveBuilderFactory->GetArchiveTypes(modDto.get());
		for (auto&& type : types) {
			const std::unique_ptr<IArchiveBuilder> builder = this->m_ArchiveBuilderFactory->Create(type, modDto.get());
			ArchiveBuildDirector director(this->m_SettingsService, builder.get());
			director.Construct(); // must check if cancelled
			const std::vector<std::unique_ptr<libbsarch::bs_archive_auto>> archives = builder->getArchives();
			for (const auto& archive : archives) {
				if (archive) {
					const QFileInfo fileInfo(this->m_ArchiveNameService->GetArchiveFullPath(type, modDto.get()));
					bool res = this->m_ArchiveAutoService->CreateBSA(
						archive.get(), fileInfo.absoluteFilePath(), type,
						modDto->Directory(), modDto->NexusId());
					if (res) {
						createdArchives.append(fileInfo.completeBaseName());
					}
				}
			}
		}

		if (!createdArchives.isEmpty()) {
			QMessageBox::information(nullptr, "",
        QObject::tr("Created archive(s):") + "\n" + createdArchives.join(modDto->ArchiveExtension() +",\n") + modDto->ArchiveExtension());
			this->m_OverrideFileService->CreateOverrideFile(modDto->NexusId(), modDto->Directory(), createdArchives);
		}

		const std::unique_ptr<IDummyPluginService> pluginService = this->m_DummyPluginServiceFactory->Create();
		pluginService->CreatePlugin(modDto->Directory(), modDto->ArchiveName());

		if (!modDto->Directory().isEmpty()) {
			this->m_HideLooseAssetService->HideLooseAssets(modDto->Directory());
		}
	}
}
