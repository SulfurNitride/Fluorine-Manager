#include "ScriptFunctions.h"

#include <QApplication>
#include <QDir>
#include <QGridLayout>
#include <QInputDialog>
#include <QImageReader>
#include <QLabel>
#include <QMessageBox>
#include <QScreen>

#include <uibase/iplugingame.h>
#include <uibase/ipluginlist.h>
#include <uibase/log.h>

#include "../interop/QtDotNetConverters.h"
#include "../newstuff/rtfPopup.h"
#include "../oldstuff/DialogSelect.h"

ScriptFunctionsHelper::ScriptFunctionsHelper() : mMessageBoxHelper(make_unique<MessageBoxHelper>())
{
  moveToThread(QApplication::instance()->thread());

  connect(this, &ScriptFunctionsHelper::DialogSelectSignal, this, &ScriptFunctionsHelper::DialogSelectSlot, Qt::BlockingQueuedConnection);
  connect(this, &ScriptFunctionsHelper::InputStringSignal, this, &ScriptFunctionsHelper::InputStringSlot, Qt::BlockingQueuedConnection);
  connect(this, &ScriptFunctionsHelper::DisplayImageSignal, this, &ScriptFunctionsHelper::DisplayImageSlot, Qt::BlockingQueuedConnection);
  connect(this, &ScriptFunctionsHelper::DisplayTextSignal, this, &ScriptFunctionsHelper::DisplayTextSlot, Qt::BlockingQueuedConnection);
}

std::optional<QVector<int>> ScriptFunctionsHelper::DialogSelect(QWidget* parent, const QString& title, const QVector<QString>& items, const QVector<QString>& descriptions, const QVector<QString>& pixmaps, bool multiSelect)
{
  std::optional<QVector<int>> result;
  emit DialogSelectSignal(result, parent, title, items, descriptions, pixmaps, multiSelect);
  return result;
}

QString ScriptFunctionsHelper::InputString(QWidget* parentWidget, const QString& title, const QString& initialText)
{
  QString text;
  emit InputStringSignal(text, parentWidget, title, initialText);
  return text;
}

void ScriptFunctionsHelper::DisplayImage(QWidget* parentWidget, const QString& path, const QString& title)
{
  emit DisplayImageSignal(parentWidget, path, title);
}

void ScriptFunctionsHelper::DisplayText(QWidget* parentWidget, const QString& path, const QString& title)
{
  emit DisplayTextSignal(parentWidget, path, title);
}

void ScriptFunctionsHelper::InputStringSlot(QString& textOut, QWidget* parentWidget, const QString& title, const QString& initialText)
{
  textOut = QInputDialog::getText(parentWidget, title, title, QLineEdit::Normal, initialText);
}

void ScriptFunctionsHelper::DisplayImageSlot(QWidget* parentWidget, const QString& path, const QString& title)
{
  QImageReader reader(path);
  QImage image = reader.read();
  if (!image.isNull())
  {
    QPixmap pixmap = QPixmap::fromImage(image);
    MOBase::log::debug("image size {}, pixmap size {}", image.size(), pixmap.size());
    QDialog popup;
    QLayout* layout = new QGridLayout(&popup);
    popup.setLayout(layout);
    FixedAspectRatioImageLabel* label = new FixedAspectRatioImageLabel(&popup);
    label->setUnscaledPixmap(pixmap);

    QSize screenSize = parentWidget->screen()->availableSize();
    int maxHeight = static_cast<int>(screenSize.height() * 0.8f);
    if (pixmap.size().height() > maxHeight)
      // This is approximate due to borders, label can sort out details.
      popup.resize(label->widthForHeight(maxHeight), maxHeight);

    layout->addWidget(label);
    popup.setWindowTitle(title);
    popup.exec();
  }
  else
    MOBase::log::error("Unable to display {}. Error was {}: {}", path, reader.error(), reader.errorString());
}

void ScriptFunctionsHelper::DisplayTextSlot(QWidget* parentWidget, const QString& path, const QString& title)
{
  RtfPopup popup(toDotNetString(path), parentWidget);
  popup.setWindowTitle(title);
  // the size readmes are becoming automatically
  popup.resize(492, 366);
  popup.exec();
}

void ScriptFunctionsHelper::DialogSelectSlot(std::optional<QVector<int>>& resultOut, QWidget* parent, const QString& title, const QVector<QString>& items,
                                             const QVector<QString>& descriptions, const QVector<QString>& pixmaps, bool multiSelect)
{
  resultOut = ::DialogSelect(parent, title, items, descriptions, pixmaps, multiSelect);
}

ScriptFunctions::ScriptFunctions(QWidget* parentWidget, MOBase::IOrganizer* moInfo) : mParentWidget(parentWidget), mMoInfo(moInfo), mHelper(new ScriptFunctionsHelper) {}

ScriptFunctions::~ScriptFunctions()
{
  if (!mHelper)
    return;
  this->!ScriptFunctions();
}

ScriptFunctions::!ScriptFunctions()
{
  mHelper->deleteLater();
  mHelper = nullptr;
}

void ScriptFunctions::Warn(System::String^ msg)
{
  mHelper->warning(mParentWidget, "Warning", toQString(msg));
}

void ScriptFunctions::Message(System::String^ msg)
{
  mHelper->information(mParentWidget, "Message", toQString(msg));
}

void ScriptFunctions::Message(System::String^ msg, System::String^ title)
{
  mHelper->information(mParentWidget, toQString(title), toQString(msg));
}

System::Collections::Generic::List<int>^ ScriptFunctions::Select(System::Collections::Generic::List<System::String^>^ items,
                                                                 System::String^ title,
                                                                 bool isMultiSelect,
                                                                 System::Collections::Generic::List<System::String^>^ previews,
                                                                 System::Collections::Generic::List<System::String^>^ descriptions)
{
  QVector<QString> qItems;
  qItems.reserve(items ? items->Count : 0);
  QVector<QString> qPreviews;
  qPreviews.reserve(previews ? previews->Count : 0);
  QVector<QString> qDescriptions;
  qDescriptions.reserve(descriptions ? descriptions->Count : 0);

  // Expect red squiggles. No one told intellisense about this syntax, but it's the least ugly.
  if (items)
  {
    for each (System::String ^ item in items)
      qItems.push_back(toQString(item));
  }

  if (previews)
  {
    for each (System::String ^ preview in previews)
      qPreviews.push_back(toQString(preview));
  }

  if (descriptions)
  {
    for each (System::String ^ description in descriptions)
      qDescriptions.push_back(toQString(description));
  }

  std::optional<QVector<int>> qResponse = mHelper->DialogSelect(mParentWidget, toQString(title), qItems, qDescriptions, qPreviews, isMultiSelect);
  if (!qResponse.has_value())
    return nullptr;

  System::Collections::Generic::List<int>^ response = gcnew System::Collections::Generic::List<int>(qResponse.value().length());
  for (const auto selection : qResponse.value())
    response->Add(selection);
  return response;
}

System::String^ ScriptFunctions::InputString(System::String^ title, System::String^ initialText)
{
  return toDotNetString(mHelper->InputString(mParentWidget, toQString(title), initialText ? toQString(initialText) : ""));
}

int ScriptFunctions::DialogYesNo(System::String^ message)
{
  return mHelper->question(mParentWidget, "", toQString(message)) == QMessageBox::StandardButton::Yes;
}

int ScriptFunctions::DialogYesNo(System::String^ message, System::String^ title)
{
  return mHelper->question(mParentWidget, toQString(title), toQString(message)) == QMessageBox::StandardButton::Yes;
}

void ScriptFunctions::DisplayImage(System::String^ path, System::String^ title)
{
  mHelper->DisplayImage(mParentWidget, toQString(path), toQString(title));
}

void ScriptFunctions::DisplayText(System::String^ text, System::String^ title)
{
  mHelper->DisplayText(mParentWidget, toQString(text), toQString(title));
}

void ScriptFunctions::Patch(System::String^ from, System::String^ to)
{
  throw gcnew System::NotImplementedException();
}

System::String^ ScriptFunctions::ReadOblivionINI(System::String^ section, System::String^ name)
{
  throw gcnew System::NotImplementedException();
  // TODO: implement this if a user ever reports the exception. OMODFramework should be handling this for us.
}

System::String^ ScriptFunctions::ReadRendererInfo(System::String^ name)
{
  throw gcnew System::NotImplementedException();
  // TODO: implement this if a user ever reports the exception. OMODFramework should be handling this for us.
}

bool ScriptFunctions::DataFileExists(System::String^ path)
{
  return mMoInfo->resolvePath(toQString(path)) != "";
}

bool ScriptFunctions::HasScriptExtender()
{
  for (const auto& forcedLoad : mMoInfo->managedGame()->executableForcedLoads())
  {
    if (forcedLoad.library().toLower().startsWith("obse"))
    {
      if (mMoInfo->managedGame()->gameDirectory().exists(forcedLoad.library()))
        return true;
    }
  }

  return mMoInfo->managedGame()->gameDirectory().exists("obse_loader.exe");
}

bool ScriptFunctions::HasGraphicsExtender()
{
  return DataFileExists("obse\\plugins\\obge.dll");
}

System::Version^ ScriptFunctions::ScriptExtenderVersion()
{
  QString obsePath;
  for (const auto& forcedLoad : mMoInfo->managedGame()->executableForcedLoads())
  {
    if (forcedLoad.library().toLower().startsWith("obse"))
    {
      if (mMoInfo->managedGame()->gameDirectory().exists(forcedLoad.library()))
      {
        obsePath = mMoInfo->managedGame()->gameDirectory().filePath(forcedLoad.library());
        break;
      }
    }
  }

  if (obsePath.isEmpty())
    obsePath = mMoInfo->managedGame()->gameDirectory().filePath("obse_loader.exe");

  System::Diagnostics::FileVersionInfo^ info = System::Diagnostics::FileVersionInfo::GetVersionInfo(toDotNetString(obsePath));
  return gcnew System::Version(info->FileMajorPart, info->FileMinorPart, info->FileBuildPart, info->FilePrivatePart);
}

System::Version^ ScriptFunctions::GraphicsExtenderVersion()
{
  return gcnew System::Version(System::Diagnostics::FileVersionInfo::GetVersionInfo(toDotNetString(mMoInfo->resolvePath("obse\\plugins\\obge.dll")))->FileVersion);
}

System::Version^ ScriptFunctions::OblivionVersion()
{
  return gcnew System::Version(System::Diagnostics::FileVersionInfo::GetVersionInfo(toDotNetString(mMoInfo->managedGame()->gameDirectory().filePath("oblivion.exe")))->FileVersion);
}

System::Version^ ScriptFunctions::OBSEPluginVersion(System::String^ path)
{
  QString pluginPath = mMoInfo->resolvePath(toQString(System::IO::Path::Combine("obse", "plugins", System::IO::Path::ChangeExtension(path, ".dll"))));
  if (pluginPath.isEmpty())
    return nullptr;
  return gcnew System::Version(System::Diagnostics::FileVersionInfo::GetVersionInfo(toDotNetString(pluginPath))->FileVersion);
}

System::Collections::Generic::IEnumerable<OMODFramework::Scripting::ScriptESP>^ ScriptFunctions::GetESPs()
{
  QStringList plugins = mMoInfo->pluginList()->pluginNames();
  System::Collections::Generic::List<OMODFramework::Scripting::ScriptESP>^ pluginList = gcnew System::Collections::Generic::List<OMODFramework::Scripting::ScriptESP>(plugins.count());
  for (const auto& pluginName : plugins)
  {
    auto state = mMoInfo->pluginList()->state(pluginName);
    if (state != MOBase::IPluginList::PluginState::STATE_MISSING)
    {
      OMODFramework::Scripting::ScriptESP plugin;
      plugin.Name = toDotNetString(pluginName);
      plugin.Active = state == MOBase::IPluginList::PluginState::STATE_ACTIVE;
      pluginList->Add(plugin);
    }
  }
  return pluginList;
}

System::Collections::Generic::IEnumerable<System::String^>^ ScriptFunctions::GetActiveOMODNames()
{
  throw gcnew System::NotImplementedException();
  // TODO: implement this if a user ever reports the exception. No known OMODs seem to actually use this (which is irritating as this is one of OBMM's most powerful features).
}

cli::array<unsigned char, 1>^ ScriptFunctions::ReadExistingDataFile(System::String^ file)
{
  throw gcnew System::NotImplementedException();
  // TODO: implement this if a user ever reports the exception. OMODFramework should be handling this for us.
}

cli::array<unsigned char, 1>^ ScriptFunctions::GetDataFileFromBSA(System::String^ file)
{
  throw gcnew System::NotImplementedException();
  // TODO: implement this if a user ever reports the exception. OMODFramework should be handling this for us.
}

cli::array<unsigned char, 1>^ ScriptFunctions::GetDataFileFromBSA(System::String^ bsa, System::String^ file)
{
  throw gcnew System::NotImplementedException();
  // TODO: implement this if a user ever reports the exception. OMODFramework should be handling this for us.
}
