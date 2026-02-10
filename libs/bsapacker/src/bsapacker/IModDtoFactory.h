#ifndef IMODDTOFACTORY_H
#define IMODDTOFACTORY_H

#include "IModDto.h"
#include <memory>

namespace BsaPacker
{
	class IModDtoFactory
	{
	public:
		virtual ~IModDtoFactory() = default;
		[[nodiscard]] virtual std::unique_ptr<IModDto> Create() const = 0;
	};
} // namespace BsaPacker

#endif // IMODDTOFACTORY_H
