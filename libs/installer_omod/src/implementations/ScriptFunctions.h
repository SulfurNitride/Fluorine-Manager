#pragma once

using namespace cli;

#include <optional>

#include <QWidget>

#include <uibase/imoinfo.h>

#include "../MessageBoxHelper.h"
#include "../QObject_unique_ptr.h"

class ScriptFunctionsHelper : public QObject
{
  Q_OBJECT

public:
  ScriptFunctionsHelper();

  // don't bother with std::forward and decltype(auto) as we know everything is fine being copied
  // in fact, as some stuff lives on the managed heap, we can't take a reference anyway
  template<typename... Args> auto critical(Args... args) { return mMessageBoxHelper->critical(args...); }
  template<typename... Args> auto information(Args... args) { return mMessageBoxHelper->information(args...); }
  template<typename... Args> auto question(Args... args) { return mMessageBoxHelper->question(args...); }
  template<typename... Args> auto warning(Args... args) { return mMessageBoxHelper->warning(args...); }

  std::optional<QVector<int>> DialogSelect(QWidget* parent, const QString& title, const QVector<QString>& items,
                                           const QVector<QString>& descriptions, const QVector<QString>& pixmaps,
                                           bool multiSelect);

  QString InputString(QWidget* parentWidget, const QString& title, const QString& initialText);

  void DisplayImage(QWidget* parentWidget, const QString& path, const QString& title);

  void DisplayText(QWidget* parentWidget, const QString& path, const QString& title);

signals:
  void DialogSelectSignal(std::optional<QVector<int>>& resultOut, QWidget* parent, const QString& title, const QVector<QString>& items,
                          const QVector<QString>& descriptions, const QVector<QString>& pixmaps, bool multiSelect);

  void InputStringSignal(QString& textOut, QWidget* parentWidget, const QString& title, const QString& initialText);

  void DisplayImageSignal(QWidget* parentWidget, const QString& path, const QString& title);

  void DisplayTextSignal(QWidget* parentWidget, const QString& path, const QString& title);

public slots:
  void DialogSelectSlot(std::optional<QVector<int>>& resultOut, QWidget* parent, const QString& title, const QVector<QString>& items,
                        const QVector<QString>& descriptions, const QVector<QString>& pixmaps, bool multiSelect);

  void InputStringSlot(QString& textOut, QWidget* parentWidget, const QString& title, const QString& initialText);

  void DisplayImageSlot(QWidget* parentWidget, const QString& path, const QString& title);

  void DisplayTextSlot(QWidget* parentWidget, const QString& path, const QString& title);

private:
  QObject_unique_ptr<MessageBoxHelper> mMessageBoxHelper;
};

ref class ScriptFunctions : OMODFramework::Scripting::IScriptFunctions
{
public:
  ScriptFunctions(QWidget* parentWidget, MOBase::IOrganizer* moInfo);
  ~ScriptFunctions();
  !ScriptFunctions();

  // note: C++/CLI wants virtual for interface implementations, not override
  virtual void Warn(System::String^ msg);

  virtual void Message(System::String^ msg);

  virtual void Message(System::String^ msg, System::String^ title);

  virtual System::Collections::Generic::List<int>^ Select(System::Collections::Generic::List<System::String^>^ items, System::String^ title, bool isMultiSelect, System::Collections::Generic::List<System::String^>^ previews, System::Collections::Generic::List<System::String^>^ descriptions);

  virtual System::String^ InputString(System::String^ title, System::String^ initialText);

  virtual int DialogYesNo(System::String^ message);

  virtual int DialogYesNo(System::String^ message, System::String^ title);

  virtual void DisplayImage(System::String^ path, System::String^ title);

  virtual void DisplayText(System::String^ text, System::String^ title);

  virtual void Patch(System::String^ from, System::String^ to);

  virtual System::String^ ReadOblivionINI(System::String^ section, System::String^ name);

  virtual System::String^ ReadRendererInfo(System::String^ name);

  virtual bool DataFileExists(System::String^ path);

  virtual bool HasScriptExtender();

  virtual bool HasGraphicsExtender();

  virtual System::Version^ ScriptExtenderVersion();

  virtual System::Version^ GraphicsExtenderVersion();

  virtual System::Version^ OblivionVersion();

  virtual System::Version^ OBSEPluginVersion(System::String^ path);

  virtual System::Collections::Generic::IEnumerable<OMODFramework::Scripting::ScriptESP>^ GetESPs();

  virtual System::Collections::Generic::IEnumerable<System::String^>^ GetActiveOMODNames();

  virtual cli::array<unsigned char, 1>^ ReadExistingDataFile(System::String^ file);

  virtual cli::array<unsigned char, 1>^ GetDataFileFromBSA(System::String^ file);

  virtual cli::array<unsigned char, 1>^ GetDataFileFromBSA(System::String^ bsa, System::String^ file);

private:
  QWidget* mParentWidget;
  MOBase::IOrganizer* mMoInfo;
  ScriptFunctionsHelper* mHelper;
};
