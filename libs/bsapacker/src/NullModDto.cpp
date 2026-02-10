#include "NullModDto.h"

namespace BsaPacker
{
	QString NullModDto::ArchiveExtension() const
	{
		return QString();
	}

	QString NullModDto::ArchiveName() const
	{
		return QString();
	}

	QString NullModDto::Directory() const
	{
		return QString();
	}

	QString NullModDto::ModForename() const
	{
		return QString();
	}

	int NullModDto::NexusId() const
	{
		return 0;
	}
} // namespace BsaPacker
