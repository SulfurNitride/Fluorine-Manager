#ifndef IDUMMYPLUGINSERVICEFACTORY_H
#define IDUMMYPLUGINSERVICEFACTORY_H

#include <bsapacker/IDummyPluginService.h>
#include <memory>

namespace BsaPacker
{
	class IDummyPluginServiceFactory
	{
	public:
		virtual ~IDummyPluginServiceFactory() = default;
		[[nodiscard]] virtual std::unique_ptr<IDummyPluginService> Create() const = 0;
	};
}

#endif // IDUMMYPLUGINSERVICEFACTORY_H
