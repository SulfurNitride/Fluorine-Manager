#ifndef SETTINGSSERVICE_H
#define SETTINGSSERVICE_H

#include <bsapacker/ISettingsService.h>

#include <imoinfo.h>
#include <pluginsetting.h>

namespace BsaPacker
{
	class SettingsService : public ISettingsService
	{
	public:
		explicit SettingsService(MOBase::IOrganizer* organizer);
		~SettingsService() override = default;
		[[nodiscard]] QVariant GetPluginSetting(const QString& setting) const override;

		static const QString& SETTING_ENABLED;
		static const QString& SETTING_HIDE_LOOSE_ASSETS;
		static const QString& SETTING_CREATE_PLUGINS;
		static const QString& SETTING_BLACKLISTED_FILES;
		static const QString& SETTING_COMPRESS_ARCHIVES;
		//static const QString& SETTING_SPLIT_ARCHIVES;

		static const QList<MOBase::PluginSetting>& PluginSettings;

	private:
		MOBase::IOrganizer* m_Organizer = nullptr;
	};
} // namespace BsaPacker

#endif // SETTINGSSERVICE_H
