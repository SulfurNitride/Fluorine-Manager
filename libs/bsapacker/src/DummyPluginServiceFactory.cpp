#include "DummyPluginServiceFactory.h"

#include "NexusId.h"

#include "Fallout3DummyPluginService.h"
#include "Fallout4DummyPluginService.h"
#include "NewVegasDummyPluginService.h"
#include "NullDummyPluginService.h"
#include "OblivionDummyPluginService.h"
#include "SkyrimDummyPluginService.h"
#include "SkyrimSEDummyPluginService.h"
#include "StarfieldDummyPluginService.h"

#include <QMessageBox>

namespace BsaPacker
{
	DummyPluginServiceFactory::DummyPluginServiceFactory(
			const IModContext* modContext,
			const IFileWriterService* fileWriterService,
			const IDummyPluginLogic* dummyPluginLogic)
		: m_ModContext(modContext),
		  m_FileWriterService(fileWriterService),
		  m_DummyPluginLogic(dummyPluginLogic)
	{
	}

	std::unique_ptr<IDummyPluginService> DummyPluginServiceFactory::Create() const
	{
		// violates SRP! Must seperate choosing logic from construction. This class is not a factory, but a strategy!
		switch (this->m_ModContext->GetNexusId()) {
			case NexusId::Oblivion:
				return std::make_unique<OblivionDummyPluginService>(m_FileWriterService, m_DummyPluginLogic);
			case NexusId::Fallout3:
				return std::make_unique<Fallout3DummyPluginService>(m_FileWriterService, m_DummyPluginLogic);
			case NexusId::NewVegas:
				return std::make_unique<NewVegasDummyPluginService>(m_FileWriterService, m_DummyPluginLogic);
			case NexusId::Skyrim:
			case NexusId::Enderal:
				return std::make_unique<SkyrimDummyPluginService>(m_FileWriterService, m_DummyPluginLogic);
			case NexusId::SkyrimSE:
			case NexusId::EnderalSE:
				return std::make_unique<SkyrimSEDummyPluginService>(m_FileWriterService, m_DummyPluginLogic);
			case NexusId::Fallout4:
				return std::make_unique<Fallout4DummyPluginService>(m_FileWriterService, m_DummyPluginLogic);
			case NexusId::Starfield:
				return std::make_unique<StarfieldDummyPluginService>(m_FileWriterService, m_DummyPluginLogic);
			case NexusId::Morrowind:
			default:
				break;
		}
		return std::make_unique<NullDummyPluginService>();
	}
} // namespace BsaPacker
