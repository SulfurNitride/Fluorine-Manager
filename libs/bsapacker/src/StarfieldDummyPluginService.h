#ifndef STARFIELDDUMMYPLUGINSERVICE_H
#define STARFIELDDUMMYPLUGINSERVICE_H

#include "bsapacker_global.h"
#include <bsapacker/IDummyPluginLogic.h>
#include <bsapacker/IDummyPluginService.h>
#include <bsapacker/IFileWriterService.h>

namespace BsaPacker
{
	class BSAPACKER_EXPORT StarfieldDummyPluginService : public IDummyPluginService
	{
	public:
		StarfieldDummyPluginService(const IFileWriterService* fileWriterService,
			const IDummyPluginLogic* dummyPluginLogic);
		~StarfieldDummyPluginService() override = default;
		bool CreatePlugin(const QString& modPath,
			const QString& archiveNameBase) const override;

		static constexpr unsigned char RAW_STARFIELD[] = {
			0x54, 0x45, 0x53, 0x34, 0x19, 0x00, 0x00, 0x00, 0x01, 0x01, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x2B, 0x02, 0x00, 0x00,
			0x48, 0x45, 0x44, 0x52, 0x0C, 0x00, 0x8F, 0xC2, 0x75, 0x3F, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x43, 0x4E, 0x41, 0x4D, 0x01, 0x00,
			0x00
		};

	private:
		const IFileWriterService* m_FileWriterService = nullptr;
		const IDummyPluginLogic* m_DummyPluginLogic = nullptr;
	};
}

#endif // STARFIELDDUMMYPLUGINSERVICE_H
