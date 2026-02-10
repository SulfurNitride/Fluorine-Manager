#ifndef IARCHIVENAMESERVICE_H
#define IARCHIVENAMESERVICE_H

#include <QString>
#include <qlibbsarch/QLibbsarch.h>
#include <bsapacker/IModDto.h>

namespace BsaPacker
{
	class IArchiveNameService
	{
	public:
		virtual ~IArchiveNameService() = default;
		virtual QString GetFileExtension() const = 0;
		virtual QString GetArchiveFullPath(bsa_archive_type_e, const IModDto*) const = 0;
		virtual QString Infix(bsa_archive_type_e type) const = 0;
		virtual QString Suffix(const QString& pathNoExt) const = 0;
	};
}

#endif // !IARCHIVENAMESERVICE_H
