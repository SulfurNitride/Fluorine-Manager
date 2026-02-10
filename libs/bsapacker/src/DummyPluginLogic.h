#ifndef DUMMYPLUGINLOGIC_H
#define DUMMYPLUGINLOGIC_H

#include <bsapacker/IDummyPluginLogic.h>
#include <bsapacker/ISettingsService.h>
#include <bsapacker/IArchiveNameService.h>

namespace BsaPacker
{
	class DummyPluginLogic : public IDummyPluginLogic
	{
	public:
		DummyPluginLogic(const ISettingsService* settingsService,
			const IArchiveNameService* archiveNameService);
		~DummyPluginLogic() override = default;
		[[nodiscard]] bool canCreateDummyESP(const QString& fileNameNoExtension, const bsa_archive_type_e type) const override;
		[[nodiscard]] bool canCreateDummyESL(const QString& fileNameNoExtension, const bsa_archive_type_e type) const override;

	private:
		template<std::size_t SIZE>
		bool canCreateDummy(const std::array<QString, SIZE>& fileList,
			const QString& fileNameNoExtension,
			const bsa_archive_type_e type) const;

		const ISettingsService* m_SettingsService = nullptr;
		const IArchiveNameService* m_ArchiveNameService = nullptr;
	};
}

#endif // DUMMYPLUGINLOGIC_H
