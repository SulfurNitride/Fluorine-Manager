#include "ModContext.h"

#include <array>
#include <functional>
#include "iplugingame.h"
#include "imodinterface.h"
#include "imodlist.h"
#include "NexusId.h"
#include <QtConcurrent/QtConcurrentFilter>
#include <QtConcurrent/QtConcurrentMap>

namespace BsaPacker
{
	const QStringList& ModContext::PLUGIN_TYPES = { "*.esm", "*.esp", "*.esl" };

	ModContext::ModContext(MOBase::IOrganizer *moInfo) : m_Organizer(moInfo)
	{
	}

	QString ModContext::GetAbsoluteModPath(const QString &modName) const
	{
		const MOBase::IModInterface* const mod = m_Organizer->modList()->getMod(modName);
		return mod->absolutePath();
	}

	int ModContext::GetNexusId() const
	{
		const MOBase::IPluginGame* managedGame = this->m_Organizer->managedGame();
		int nexusId = managedGame->nexusGameID();

		// Games with no Nexus page have an ID of 0. Use primarySources in that case
		if (nexusId != 0) {
			return nexusId;
		}
		if (!managedGame->primarySources().isEmpty()) {
			QString primarySource = managedGame->primarySources().first();
			if (primarySource == "FalloutNV") { // TTW
				return NexusId::NewVegas;
			}
		}
		return nexusId;
	}

	QStringList ModContext::GetPlugins(const QDir& modDirectory) const
	{
		const int nexusId = this->GetNexusId();
		const QString extension = (nexusId == NexusId::Fallout4 || nexusId == NexusId::Starfield) ? ".ba2" : ".bsa";
		const std::function<QString(QString)> replace_extension = [&](QString fname) {
			return fname.replace(fname.lastIndexOf('.'), 4, extension);
		};

		QStringList filenames = QtConcurrent::blockingMapped(modDirectory.entryList(PLUGIN_TYPES, QDir::Files), replace_extension);
		filenames.removeDuplicates();
		return filenames << QStringLiteral("<new filename>") + extension;
	}

	QStringList ModContext::GetValidMods() const
	{
		const MOBase::IModList* const list = m_Organizer->modList();
		const std::function<bool(const QString&)> modStateValid = [&](const QString& mod)
		{
			return !mod.endsWith("_separator", Qt::CaseInsensitive) && (list->state(mod) & MOBase::IModList::STATE_VALID);
		};
		return QtConcurrent::blockingFiltered(list->allMods(), modStateValid);
	}
} // namespace BsaPacker
