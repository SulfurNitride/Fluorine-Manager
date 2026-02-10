#include "SettingsService.h"

namespace BsaPacker
{
	const QString& SettingsService::SETTING_HIDE_LOOSE_ASSETS = QStringLiteral("hide_loose_assets");
	const QString& SettingsService::SETTING_CREATE_PLUGINS = QStringLiteral("create_plugins");
	const QString& SettingsService::SETTING_BLACKLISTED_FILES = QStringLiteral("blacklisted_files");
	const QString& SettingsService::SETTING_COMPRESS_ARCHIVES = QStringLiteral("compress_archives");
	//const QString& SettingsService::SETTING_SPLIT_ARCHIVES = "split_archives";

	const QList<MOBase::PluginSetting>& SettingsService::PluginSettings = {
		MOBase::PluginSetting(SettingsService::SETTING_HIDE_LOOSE_ASSETS, QObject::tr("After creating the archive, set loose assets to hidden."), false),
		MOBase::PluginSetting(SettingsService::SETTING_CREATE_PLUGINS, QObject::tr("Create a dummy plugin to load the archive if one does not exist."), false),
		MOBase::PluginSetting(SettingsService::SETTING_BLACKLISTED_FILES, QObject::tr("Specify a semi-colon separated list of file extensions to ignore when packing."), ".txt;.hkx;.xml;.ini;.bk2"),
		MOBase::PluginSetting(SettingsService::SETTING_COMPRESS_ARCHIVES, QObject::tr("Compress archives if they do not contain incompressible files. Texture archives for Fallout 4 and Starfield will always be compressed. Morrowind archives will never be compressed."), true)
		//MOBase::PluginSetting(SettingsService::SETTING_SPLIT_ARCHIVES, QObject::tr("Automatically create multiple archives if they exceed the size limit."), false);
	};

	SettingsService::SettingsService(MOBase::IOrganizer* organizer)
		: m_Organizer(organizer)
	{
	}

	QVariant SettingsService::GetPluginSetting(const QString& setting) const
	{
		return this->m_Organizer->pluginSetting(QStringLiteral("BSA Packer"), setting);
	}
} // namespace BsaPacker
