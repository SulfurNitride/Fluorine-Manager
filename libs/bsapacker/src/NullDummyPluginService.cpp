#include "NullDummyPluginService.h"

namespace BsaPacker
{
	bool NullDummyPluginService::CreatePlugin([[maybe_unused]] const QString& modPath, [[maybe_unused]] const QString& archiveNameBase) const
	{
		return false;
	}
}
