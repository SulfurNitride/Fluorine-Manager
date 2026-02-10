#ifndef ARCHIVENAMESERVICE_H
#define ARCHIVENAMESERVICE_H

#include "bsapacker_global.h"
#include <bsapacker/IArchiveNameService.h>
#include <bsapacker/IModContext.h>

namespace BsaPacker
{
	class BSAPACKER_EXPORT ArchiveNameService : public IArchiveNameService
	{
	public:
		ArchiveNameService(const IModContext* modContext);
		~ArchiveNameService() override = default;
		QString GetArchiveFullPath(bsa_archive_type_e type, const IModDto* modDto) const override;
		QString GetFileExtension() const override;
		QString Infix(bsa_archive_type_e type) const override;
		QString Suffix(const QString& pathNoExt) const override;
	private:
		const IModContext* m_ModContext = nullptr;
	};
}

#endif // ARCHIVENAMESERVICE_H
