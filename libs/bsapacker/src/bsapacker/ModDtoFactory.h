#ifndef MODDTOFACTORY_H
#define MODDTOFACTORY_H

#include "bsapacker_global.h"
#include <bsapacker/IModDtoFactory.h>
#include <bsapacker/IModContext.h>
#include <bsapacker/IModDto.h>
#include <bsapacker/IPackerDialog.h>

namespace BsaPacker
{
	class BSAPACKER_EXPORT ModDtoFactory : public IModDtoFactory
	{
	public:
		ModDtoFactory(
			const IModContext* modContext,
			IPackerDialog* packerDialog);
		~ModDtoFactory() override = default;
		[[nodiscard]] std::unique_ptr<IModDto> Create() const override;
		[[nodiscard]] static QString ArchiveNameValidator(
			const QString& modName,
			const QString& pluginName,
			const bool needsNewName);
		[[nodiscard]] static bool CanOverwriteFile(
			const QString& filePath,
			const QString& fileName);

	private:
		const IModContext* m_ModContext = nullptr;
		IPackerDialog* m_PackerDialog = nullptr;
	};
} // namespace BsaPacker
#endif // MODDTOFACTORY_H
