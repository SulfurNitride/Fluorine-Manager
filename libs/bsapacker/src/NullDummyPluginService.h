#ifndef NULLDUMMYPLUGINSERVICE_H
#define NULLDUMMYPLUGINSERVICE_H

#include <bsapacker/IDummyPluginService.h>

namespace BsaPacker
{
	class NullDummyPluginService : public IDummyPluginService
	{
	public:
		NullDummyPluginService() = default;
		~NullDummyPluginService() override = default;
		bool CreatePlugin(const QString& modPath, const QString& archiveNameBase) const override;
	};
}

#endif // NULLDUMMYPLUGINSERVICE_H
