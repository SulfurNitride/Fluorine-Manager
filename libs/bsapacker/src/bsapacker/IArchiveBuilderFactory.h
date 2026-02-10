#ifndef IARCHIVEBUILDERFACTORY_H
#define IARCHIVEBUILDERFACTORY_H

#include <bsapacker/IModDto.h>
#include <bsapacker/IArchiveBuilder.h>

namespace BsaPacker
{
	class IArchiveBuilderFactory
	{
	public:
		virtual ~IArchiveBuilderFactory() = default;
		virtual std::vector<bsa_archive_type_e> GetArchiveTypes(const IModDto* modDto) const = 0;
		virtual std::unique_ptr<IArchiveBuilder> Create(bsa_archive_type_e archiveType, const IModDto* modDto) const = 0;
	};
}

#endif // IARCHIVEBUILDERFACTORY_H
