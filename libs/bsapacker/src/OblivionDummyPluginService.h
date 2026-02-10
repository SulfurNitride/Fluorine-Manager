#ifndef OBLIVIONDUMMYPLUGINSERVICE_H
#define OBLIVIONDUMMYPLUGINSERVICE_H

#include "bsapacker_global.h"
#include <bsapacker/IDummyPluginLogic.h>
#include <bsapacker/IDummyPluginService.h>
#include <bsapacker/IFileWriterService.h>

namespace BsaPacker
{
	class BSAPACKER_EXPORT OblivionDummyPluginService : public IDummyPluginService
	{
	public:
		OblivionDummyPluginService(const IFileWriterService* fileWriterService,
								   const IDummyPluginLogic* dummyPluginLogic);
		~OblivionDummyPluginService() override = default;
		bool CreatePlugin(const QString& modPath,
						  const QString& archiveNameBase) const override;

		static constexpr unsigned char RAW_OBLIVION[] = {
			0x54, 0x45, 0x53, 0x34, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x48, 0x45, 0x44, 0x52,
			0x0C, 0x00, 0x00, 0x00, 0x80, 0x3F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08,
			0x00, 0x00, 0x43, 0x4E, 0x41, 0x4D, 0x08, 0x00, 0x44, 0x45, 0x46, 0x41,
			0x55, 0x4C, 0x54, 0x00
		};

	private:
		const IFileWriterService* m_FileWriterService = nullptr;
		const IDummyPluginLogic* m_DummyPluginLogic = nullptr;
	};
}

#endif // OBLIVIONDUMMYPLUGINSERVICE_H
