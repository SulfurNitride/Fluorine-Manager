#ifndef ARCHIVEBUILDDIRECTOR_H
#define ARCHIVEBUILDDIRECTOR_H

#include <bsapacker/IArchiveBuilder.h>
#include <bsapacker/ISettingsService.h>

namespace BsaPacker
{
	class ArchiveBuildDirector
	{
	public:
		explicit ArchiveBuildDirector(const ISettingsService* settingsService, IArchiveBuilder* archiveFileBuildService);
		~ArchiveBuildDirector() = default;
		ArchiveBuildDirector(const ArchiveBuildDirector&) = default;
		ArchiveBuildDirector& operator=(const ArchiveBuildDirector&) = default;
		ArchiveBuildDirector(ArchiveBuildDirector&&) = default;
		ArchiveBuildDirector& operator=(ArchiveBuildDirector&&) = default;

		void Construct();

	private:
		IArchiveBuilder* m_ArchiveFileBuildService = nullptr;
		const ISettingsService* m_SettingsService = nullptr;
	};
} // namespace BsaPacker

#endif // ARCHIVEBUILDDIRECTOR_H
