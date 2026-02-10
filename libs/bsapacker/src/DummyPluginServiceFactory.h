#ifndef DUMMYPLUGINSERVICEFACTORY_H
#define DUMMYPLUGINSERVICEFACTORY_H

#include "bsapacker_global.h"
#include <bsapacker/IDummyPluginLogic.h>
#include <bsapacker/IDummyPluginServiceFactory.h>
#include <bsapacker/IFileWriterService.h>
#include <bsapacker/IModContext.h>

namespace BsaPacker
{
	class BSAPACKER_EXPORT DummyPluginServiceFactory : public IDummyPluginServiceFactory
	{
	public:
		explicit DummyPluginServiceFactory(
				const IModContext* modContext,
				const IFileWriterService* fileWriterService, // bad! only passing this to another constructor!
				const IDummyPluginLogic* dummyPluginLogic);  // TODO: perhaps remedy with strategy pattern?
		~DummyPluginServiceFactory() override = default;
		[[nodiscard]] std::unique_ptr<IDummyPluginService> Create() const override;

	private:
		const IModContext* m_ModContext = nullptr;
		const IFileWriterService* m_FileWriterService = nullptr;
		const IDummyPluginLogic* m_DummyPluginLogic = nullptr;
	};
} // namespace BsaPacker

#endif // DUMMYPLUGINSERVICEFACTORY_H
