#ifndef BSA_PACKER_H
#define BSA_PACKER_H

#include "bsapacker_global.h"
#include <iplugintool.h>
#include <bsapacker/IModContext.h>
#include <bsapacker/ISettingsService.h>

namespace BsaPacker
{
	class BSAPACKER_EXPORT Bsa_Packer : public MOBase::IPluginTool
	{
		Q_OBJECT
			Q_INTERFACES(MOBase::IPlugin MOBase::IPluginTool)
#if QT_VERSION >= QT_VERSION_CHECK(5, 0, 0)
			Q_PLUGIN_METADATA(IID "org.MattyFez.BSA_Packer" FILE "bsa_packer.json")
#endif

	public:
		// IPlugin interface
		bool init(MOBase::IOrganizer* moInfo) override;
		[[nodiscard]] QString name() const override;
		[[nodiscard]] QString author() const override;
		[[nodiscard]] QString description() const override;
		[[nodiscard]] MOBase::VersionInfo version() const override;
		[[nodiscard]] QList<MOBase::PluginSetting> settings() const override;

		// IPluginTool interface
		[[nodiscard]] QString displayName() const override;
		[[nodiscard]] QString tooltip() const override;
		[[nodiscard]] QIcon icon() const override;

	public Q_SLOTS:
		void display() const override;

	private:
		MOBase::IOrganizer* m_Organizer = nullptr;
		std::unique_ptr<IModContext> m_ModContext = nullptr;
		std::unique_ptr<ISettingsService> m_SettingsService = nullptr;
	};
} // namespace BsaPacker
#endif //BSA_PACKER_H
