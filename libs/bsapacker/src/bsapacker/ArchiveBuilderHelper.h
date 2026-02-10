#ifndef ARCHIVEBUILDERHELPER_H
#define ARCHIVEBUILDERHELPER_H

#include "bsapacker_global.h"
#include <bsapacker/IArchiveBuilderHelper.h>
#include <bsapacker/ISettingsService.h>
#include <set>
#include <string>

namespace BsaPacker
{
	class BSAPACKER_EXPORT ArchiveBuilderHelper : public IArchiveBuilderHelper
	{
	public:
		ArchiveBuilderHelper(const ISettingsService* settingsService);
		[[nodiscard]] bool isFileIgnorable(const std::filesystem::path&, const std::vector<std::filesystem::path::string_type>&) const override;
		[[nodiscard]] bool isIncompressible(const std::filesystem::path&) const override;
		[[nodiscard]] bool isExtensionBlacklisted(const std::filesystem::path&) const override;
		[[nodiscard]] uint32_t getFileCount(const std::filesystem::path&) const override;
		[[nodiscard]] std::vector<std::filesystem::path::string_type> getRootDirectoryFilenames(const std::filesystem::path&) const override;
		[[nodiscard]] bool doesPathContainFiles(const std::filesystem::path&, const std::vector<std::filesystem::path::string_type>&) const override;

	private:
		const static std::set<std::string> INCOMPRESSIBLE_TYPES;
		const ISettingsService* m_SettingsService = nullptr;
	};
} // namespace BsaPacker

#endif // ARCHIVEBUILDERHELPER_H
