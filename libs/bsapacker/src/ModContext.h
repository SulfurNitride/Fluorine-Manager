#ifndef MOD_CONTEXT_H
#define MOD_CONTEXT_H

#include "bsapacker_global.h"
#include <bsapacker/IModContext.h>
#include <imoinfo.h>

namespace BsaPacker
{
	class BSAPACKER_EXPORT ModContext : public IModContext
	{
	public:
		explicit ModContext(MOBase::IOrganizer* moInfo);
		[[nodiscard]] QString GetAbsoluteModPath(const QString& modName) const override;
		[[nodiscard]] int GetNexusId() const override;
		[[nodiscard]] QStringList GetPlugins(const QDir& modDirectory) const override;
		[[nodiscard]] QStringList GetValidMods() const override;

	private:
		const MOBase::IOrganizer* m_Organizer = nullptr;
		const static QStringList& PLUGIN_TYPES;
	};
} // namespace BsaPacker

#endif // MOD_CONTEXT_H
