#include "ModDto.h"

#include <QDir>

namespace BsaPacker
{
	ModDto::ModDto(
		const int nexusId,
		const QString& modDir,
		const QString& archiveName,
		const QString& archiveExtension)
		: m_NexusId(nexusId),
		  m_Path(modDir),
		  m_ArchiveName(archiveName),
		  m_ArchiveExtension(archiveExtension)
	{
	}

	QString ModDto::ArchiveExtension() const
	{
		return this->m_ArchiveExtension;
	}

	QString ModDto::ArchiveName() const
	{
		return this->m_ArchiveName;
	}

	QString ModDto::Directory() const
	{
		return this->m_Path.path();
	}

	QString ModDto::ModForename() const
	{
		return this->m_Path.dirName();
	}

	int ModDto::NexusId() const
	{
		return this->m_NexusId;
	}
} // namespace BsaPacker
