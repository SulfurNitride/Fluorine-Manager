#ifndef IPACKERDIALOGFACTORY_H
#define IPACKERDIALOGFACTORY_H

#include <bsapacker/IPackerDialog.h>

namespace BsaPacker
{
	class IPackerDialogFactory
	{
	public:
		virtual ~IPackerDialogFactory() = default;
		virtual std::unique_ptr<IPackerDialog> Create() const = 0;
	};
}

#endif // IPACKERDIALOGFACTORY_H
