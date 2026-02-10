#ifndef IPACKERDIALOG_H
#define IPACKERDIALOG_H

#include <QString>
#include <QDialog>
#include <QListWidgetItem>

namespace BsaPacker
{
	class IPackerDialog
	{
	public:
		virtual ~IPackerDialog()  = default;
		[[nodiscard]] virtual bool IsNewFilename() const = 0;
		[[nodiscard]] virtual QString SelectedMod() const = 0;
		virtual void RefreshModList() = 0;
		[[nodiscard]] virtual QString SelectedPluginItem() const = 0;
		virtual void UpdateNameList(const QString& modName) = 0;
		virtual void RefreshSelectedName() = 0;
		virtual int Exec() = 0;
		virtual void RefreshOkButton() = 0;
	};
} // namespace BsaPacker

#endif // IPACKERDIALOG_H
