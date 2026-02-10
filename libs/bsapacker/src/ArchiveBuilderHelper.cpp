#include <bsapacker/ArchiveBuilderHelper.h>

#include <algorithm>
#include <ranges>

#include "SettingsService.h"

#include <QDebug>

using std::filesystem::path;
using std::filesystem::directory_entry;
using std::filesystem::directory_iterator;
using std::filesystem::recursive_directory_iterator;

namespace BsaPacker
{
	const std::set<std::string> ArchiveBuilderHelper::INCOMPRESSIBLE_TYPES = { ".wav", ".ogg", ".mp3" };

	ArchiveBuilderHelper::ArchiveBuilderHelper(const ISettingsService* settingsService)
		: m_SettingsService(settingsService)
	{
	}
	uint32_t ArchiveBuilderHelper::getFileCount(const path& rootDirectory) const
	{
		uint32_t count = 0;
		for(auto& p : recursive_directory_iterator(rootDirectory)) {
			if (p.is_regular_file()) {
				count++;
			}
		}
		return count;
	}

	std::vector<path::string_type> ArchiveBuilderHelper::getRootDirectoryFilenames(const path& rootDirectory) const
	{
		std::vector<path::string_type> filenames;
		for (const auto& entry : directory_iterator(rootDirectory)) {
			filenames.push_back(entry.path().filename().native());
		}
		return filenames;
	}

	bool ArchiveBuilderHelper::isFileIgnorable(const path& filepath, const std::vector<path::string_type>& rootDirFilenames) const
	{
		return this->doesPathContainFiles(filepath, rootDirFilenames) || // ignore files within mod directory
			this->isExtensionBlacklisted(filepath); // ignore user blacklisted file types
	}

	bool ArchiveBuilderHelper::isIncompressible(const path& filename) const
	{
		if (!this->m_SettingsService->GetPluginSetting(SettingsService::SETTING_COMPRESS_ARCHIVES).toBool()) {
			return true;
		}

		const auto& extension = filename.extension().string();
		const auto& count = ArchiveBuilderHelper::INCOMPRESSIBLE_TYPES.count(extension);
		const auto& result = count > 0;
		return result;
	}

	bool ArchiveBuilderHelper::isExtensionBlacklisted(const path& filepath) const
	{
		const auto& setting = this->m_SettingsService->GetPluginSetting(SettingsService::SETTING_BLACKLISTED_FILES).toString().toStdString();
		const auto &extension = filepath.extension().string();
		const auto count = std::ranges::count(
				setting | std::views::split(' '),
				extension,
				[](auto r)
				{ return std::string_view(r.data(), r.size()); });
		return count > 0;
	}

	bool ArchiveBuilderHelper::doesPathContainFiles(const path& filepath, const std::vector<path::string_type>& files) const
	{
		return std::find(files.begin(), files.end(), filepath.filename()) != files.end();
	}
} // namespace BsaPacker
