#include "HideLooseAssetService.h"

#include <QDir>
#include <QDirIterator>
#include <QString>
#include <QtConcurrent/QtConcurrentMap>

#include "SettingsService.h"

namespace BsaPacker
{
	QString HideLooseAssetService::s_HiddenExt(".mohidden");

	HideLooseAssetService::HideLooseAssetService(const ISettingsService* settingsService)
		: m_SettingsService(settingsService)
	{
	}

	bool HideLooseAssetService::HideLooseAssets(const QDir& modDirectory) const
	{
		if (!this->m_SettingsService->GetPluginSetting(SettingsService::SETTING_HIDE_LOOSE_ASSETS).toBool()) {
			return false;
		}

		// Only hide loose files that were not excluded when creating the archive (blacklisted), so might still be needed by the mod
		QStringList blacklistExtensions = this->m_SettingsService->GetPluginSetting(SettingsService::SETTING_BLACKLISTED_FILES).toString().split(';');
		for (auto& ext : blacklistExtensions) {
			ext.prepend("*");
		}

		const QString& absModDir = modDirectory.absolutePath();
		for (const QString& subDir : modDirectory.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
			// Hide subdirectories
			const QString& absPath = absModDir + '/' + subDir;
			QDir originalDir(absPath);
			if (originalDir.dirName().endsWith(s_HiddenExt) || originalDir.isEmpty()) {
				continue;
			}
			if (!originalDir.rename(originalDir.absolutePath(), originalDir.absolutePath() + s_HiddenExt)) {
				qWarning() << "Failed to hide " << originalDir.absolutePath();
				continue;
			}

			// Restore files with blacklisted extension to their original directories
			QDir hiddenDir(originalDir.absolutePath() + s_HiddenExt);
			QDirIterator iterator(hiddenDir.absolutePath(), blacklistExtensions, QDir::Files, QDirIterator::Subdirectories);
			while (iterator.hasNext()) {
				QString hiddenFilePath = iterator.next();
				QString originalFilePath = originalDir.absoluteFilePath(hiddenDir.relativeFilePath(hiddenFilePath));
				originalDir.mkpath(originalFilePath.left(originalFilePath.lastIndexOf("/")));
				if (!originalDir.rename(hiddenFilePath, originalFilePath)) {
					qWarning() << "Failed to unhide " << hiddenFilePath;
				}
			}
		}
		return true;

		/*
		const std::function<void(const QString&)> hideFolder = [&](const QString& subDir)
		{
			const QString& absPath = absModDir + '/' + subDir;
			QDir dir(absPath);
			if (!dir.dirName().endsWith(".mohidden"))
				dir.rename(absPath, absPath + ".mohidden");
		};
		*/
		//QtConcurrent::blockingMap(modDirectory.entryList(QDir::Dirs | QDir::NoDotAndDotDot), hideFolder);
	}
}
