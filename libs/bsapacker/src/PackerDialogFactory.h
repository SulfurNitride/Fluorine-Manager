#ifndef PACKERDIALOGFACTORY_H
#define PACKERDIALOGFACTORY_H

#include <bsapacker/IPackerDialogFactory.h>

namespace BsaPacker
{
	class PackerDialogFactory : public IPackerDialogFactory
	{
	public:
		~PackerDialogFactory() override = default;
		std::unique_ptr<IPackerDialog> Create() const override;
	};
}

#endif // PACKERDIALOGFACTORY_H
