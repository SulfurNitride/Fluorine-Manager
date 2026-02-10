#ifndef IDUMMYPLUGINLOGIC_H
#define IDUMMYPLUGINLOGIC_H

#include <QString>
#include <array>
#include <qlibbsarch/QLibbsarch.h>

namespace BsaPacker
{
	class IDummyPluginLogic
	{
	public:
		virtual ~IDummyPluginLogic() = default;
		[[nodiscard]] virtual bool canCreateDummyESP(const QString& fileNameNoExtension, const bsa_archive_type_e type) const = 0;
		[[nodiscard]] virtual bool canCreateDummyESL(const QString& fileNameNoExtension, const bsa_archive_type_e type) const = 0;
	};
}

#endif // IDUMMYPLUGINLOGIC_H
