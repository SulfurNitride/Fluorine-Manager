#include <bsapacker/ArchiveBuilderFactory.h>

#include <bsapacker/GeneralArchiveBuilder.h>
#include <bsapacker/NullArchiveBuilder.h>
#include <bsapacker/TextureArchiveBuilder.h>
#include <bsapacker/TexturelessArchiveBuilder.h>
#include "NexusId.h"

namespace BsaPacker
{
	ArchiveBuilderFactory::ArchiveBuilderFactory(const IArchiveBuilderHelper* archiveBuilderHelper)
		: m_ArchiveBuilderHelper(archiveBuilderHelper)
	{
	}

	std::vector<bsa_archive_type_e> ArchiveBuilderFactory::GetArchiveTypes(const IModDto* modDto) const
	{
		switch (modDto->NexusId()) {
			case NexusId::Morrowind:
				return std::vector<bsa_archive_type_e> { baTES3 };
			case NexusId::Oblivion:
				return std::vector<bsa_archive_type_e> { baTES4 };
			case NexusId::Fallout3:
			case NexusId::NewVegas:
			case NexusId::Skyrim:
			case NexusId::Enderal:
				return std::vector<bsa_archive_type_e> { baFO3 };
			case NexusId::SkyrimSE:
			case NexusId::EnderalSE:
				return std::vector<bsa_archive_type_e> { baSSE };
			case NexusId::Fallout4:
				return std::vector<bsa_archive_type_e> { baFO4, baFO4dds };
			case NexusId::Starfield:
				return std::vector<bsa_archive_type_e> { baSF, baSFdds };
			default:
				return std::vector<bsa_archive_type_e> { baNone };
		}
	}

	std::unique_ptr<IArchiveBuilder> ArchiveBuilderFactory::Create(const bsa_archive_type_e archiveType, const IModDto* modDto) const
	{
		switch (archiveType) {
			case baTES3:
			case baTES4:
			case baFO3:
			case baSSE:
				return std::make_unique<GeneralArchiveBuilder>(this->m_ArchiveBuilderHelper, modDto->Directory(), archiveType);
			case baFO4:
			case baSF:
				return std::make_unique<TexturelessArchiveBuilder>(this->m_ArchiveBuilderHelper, modDto->Directory(), archiveType);
			case baFO4dds:
			case baSFdds:
				return std::make_unique<TextureArchiveBuilder>(this->m_ArchiveBuilderHelper, modDto->Directory(), archiveType);
			case baNone:
			default:
				return std::make_unique<NullArchiveBuilder>();
		}
	}
} // namespace BsaPacker
