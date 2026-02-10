#ifndef IDUMMYPLUGINSERVICE_H
#define IDUMMYPLUGINSERVICE_H

#include <QString>

namespace BsaPacker
{
	class IDummyPluginService
	{
	public:
		virtual ~IDummyPluginService() = default;
		virtual bool CreatePlugin(const QString& modPath,
			const QString& archiveNameBase) const = 0;
	};
}

#endif // IDUMMYPLUGINSERVICE_H
