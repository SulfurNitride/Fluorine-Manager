#ifndef IARCHIVEAUTOSERVICE_H
#define IARCHIVEAUTOSERVICE_H

#include <libbsarch/bs_archive_auto.hpp>
#include <bsapacker/IModDto.h>

namespace BsaPacker
{
	class IArchiveAutoService
	{
	public:
		virtual ~IArchiveAutoService() = default;
		virtual bool CreateBSA(libbsarch::bs_archive_auto*, const QString&, bsa_archive_type_e,
							   const QString& sourceDir, int nexusId) const = 0;
	};
}

#endif // IARCHIVEAUTOSERVICE_H
