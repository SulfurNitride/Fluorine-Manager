#include "OMODFrameworkWrapper.h"

using namespace cli;

#include <algorithm>
#include <array>
#include <filesystem>

#include <QMessageBox>
#include <QTemporaryDir>
#include <QProgressDialog>

#include <uibase/imodinterface.h>
#include <uibase/imodlist.h>
#include <uibase/iplugingame.h>
#include <uibase/ipluginlist.h>
#include <uibase/game_features/igamefeatures.h>
#include <uibase/log.h>
#include <uibase/utility.h>
#include <uibase/registry.h>

#include <uibase/game_features/dataarchives.h>
#include <uibase/game_features/gameplugins.h>

#include "implementations/CodeProgress.h"
#include "implementations/Logger.h"
#include "implementations/ScriptFunctions.h"

#include "interop/QtDotNetConverters.h"
#include "interop/StdDotNetConverters.h"

#include "newstuff/namedialog.h"
#include "newstuff/rtfPopup.h"

#include "MessageBoxHelper.h"

namespace fs = std::filesystem;

// retrieve the path to the folder containing the proxy DLL
fs::path getPluginFolder()
{
  wchar_t path[MAX_PATH];
  HMODULE hm = NULL;

  if (GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                        (LPCWSTR)&getPluginFolder, &hm) == 0)
  {
    return {};
  }
  if (GetModuleFileName(hm, path, sizeof(path)) == 0)
  {
    return {};
  }

  return fs::path(path).parent_path();
}

// We want to search the plugin data directory for .NET DLLs
class AssemblyResolver
{
public:
  static bool getInitialised() { return sInitialised; }

  static void initialise(MOBase::IOrganizer* organizer)
  {
    if (sInitialised)
      return;
    sPluginDataPath = getPluginFolder();
    System::AppDomain::CurrentDomain->AssemblyResolve += gcnew System::ResolveEventHandler(&OnAssemblyResolve);
    sInitialised = true;
  }

private:
  static System::Reflection::Assembly^ OnAssemblyResolve(System::Object^ sender, System::ResolveEventArgs^ args);

  static bool sInitialised;
  static QDir sPluginDataPath;
};

bool AssemblyResolver::sInitialised = false;
QDir AssemblyResolver::sPluginDataPath;

System::Reflection::Assembly^ AssemblyResolver::OnAssemblyResolve(System::Object^ sender, System::ResolveEventArgs^ args)
{
  QString name = toQString(args->Name).section(',', 0, 0) + ".dll";
  if (sPluginDataPath.exists(name))
    return System::Reflection::Assembly::LoadFrom(toDotNetString(sPluginDataPath.absoluteFilePath(name)));
  return nullptr;
}

OMODFrameworkWrapper::OMODFrameworkWrapper(MOBase::IOrganizer* organizer, QWidget* parentWidget)
  : mMoInfo(organizer)
  , mParentWidget(parentWidget)
  , mWaitDialog(make_nullptr<QProgressDialog>())
{
  try
  {
    AssemblyResolver::initialise(mMoInfo);

    constructorHelper();

    connect(this, &OMODFrameworkWrapper::pickModName, this, &OMODFrameworkWrapper::pickModNameSlot, Qt::ConnectionType::BlockingQueuedConnection);
    connect(this, &OMODFrameworkWrapper::createMod, this, &OMODFrameworkWrapper::createModSlot, Qt::ConnectionType::BlockingQueuedConnection);
    connect(this, &OMODFrameworkWrapper::displayReadme, this, &OMODFrameworkWrapper::displayReadmeSlot, Qt::ConnectionType::BlockingQueuedConnection);
    connect(this, &OMODFrameworkWrapper::showWaitDialog, this, &OMODFrameworkWrapper::showWaitDialogSlot, Qt::ConnectionType::QueuedConnection);
    connect(this, &OMODFrameworkWrapper::hideWaitDialog, this, &OMODFrameworkWrapper::hideWaitDialogSlot, Qt::ConnectionType::QueuedConnection);

    initFrameworkSettings();
  }
  catch (const std::exception& e)
  {
    throw;
  }
  catch (System::Exception^ dotNetException)
  {
    throw toStdException(dotNetException);
  }
}

void OMODFrameworkWrapper::constructorHelper()
{
  // We can't call a function doing this before AssemblyResolver::initialise happens as the DLL needs to be available before its stack frame is created.
  mTempPathStack.push(toQString(OMODFramework::Framework::Settings->TempPath));
}

ref class InstallInAnotherThreadHelper
{
public:
  InstallInAnotherThreadHelper(OMODFrameworkWrapper* owner, MOBase::GuessedValue<QString>& modName, QString gameName, const QString& archiveName, const QString& version, int nexusID)
    : mModName(&modName)
    , mGameName(&gameName)
    , mArchiveName(&archiveName)
    , mVersion(&version)
    , mNexusID(nexusID)
    , mOwner(owner)
    , mExceptionPtr(new std::exception_ptr)
    , mHasResult(false)
  {}

  ~InstallInAnotherThreadHelper()
  {
    if (!mExceptionPtr)
      return;

    this->!InstallInAnotherThreadHelper();
  }

  !InstallInAnotherThreadHelper()
  {
    delete mExceptionPtr;
    mExceptionPtr = nullptr;
  }

  void Run()
  {
    try
    {
      mResult = mOwner->install(*mModName, *mGameName, *mArchiveName, *mVersion, mNexusID);
      mHasResult = true;
    }
    catch (...)
    {
      *mExceptionPtr = std::current_exception();
    }
  }

  bool HasResult()
  {
    return mHasResult;
  }

  OMODFrameworkWrapper::EInstallResult Result()
  {
    return mResult;
  }

  std::exception_ptr ExceptionPtr()
  {
    return *mExceptionPtr;
  }

private:
  // We own none of these pointers. They're pointers because managed objects can't have unmanaged members, but a pointer is just an integer of some form, which is the same.
  MOBase::GuessedValue<QString>* mModName;
  QString* mGameName;
  const QString* mArchiveName;
  const QString* mVersion;
  int mNexusID;

  OMODFrameworkWrapper* mOwner;

  std::exception_ptr* mExceptionPtr;
  OMODFrameworkWrapper::EInstallResult mResult;
  bool mHasResult;
};

OMODFrameworkWrapper::EInstallResult OMODFrameworkWrapper::installInAnotherThread(MOBase::GuessedValue<QString>& modName, QString gameName, const QString& archiveName, const QString& version, int nexusID)
{
  QEventLoop eventLoop;
  InstallInAnotherThreadHelper^ helper = gcnew InstallInAnotherThreadHelper(this, modName, gameName, archiveName, version, nexusID);
  System::Threading::Tasks::Task^ installationTask = System::Threading::Tasks::Task::Run(gcnew System::Action(helper, &InstallInAnotherThreadHelper::Run));

  // TODO: connect stuff to eventLoop.wakeUp.
  // Installation manager does this with futureWatcher.finished and progressUpdate
  while (!installationTask->IsCompleted)
    eventLoop.processEvents(QEventLoop::AllEvents | QEventLoop::WaitForMoreEvents);

  if (helper->HasResult())
    return helper->Result();
  else if (helper->ExceptionPtr())
    std::rethrow_exception(helper->ExceptionPtr());
  else
    throw std::runtime_error("Something went horribly wrong when asynchronously installing an OMOD. We don't even have the original exception.");
}

// TODO: replace with std::scope_exit when it leaves std::experimental
template <typename T>
class scope_guard
{
public:
  scope_guard(T onExit) : mOnExit(onExit) {}
  ~scope_guard() { mOnExit(); }
private:
  T mOnExit;
};

OMODFrameworkWrapper::EInstallResult OMODFrameworkWrapper::install(MOBase::GuessedValue<QString>& modName, QString gameName, const QString& archiveName, const QString& version, int nexusID)
{
  try
  {
    QObject_unique_ptr<MessageBoxHelper> messageBoxHelper = make_unique<MessageBoxHelper>();

    MOBase::log::debug("Installing {} as OMOD", archiveName);

    emit showWaitDialog("Initializing OMOD installer... ");
    refreshFrameworkSettings();
    // Stack allocating should dispose like a `using` statement in C#
    OMODFramework::OMOD omod(toDotNetString(archiveName));
    emit hideWaitDialog();

    if (!System::String::IsNullOrEmpty(omod.ModName))
      modName.update(toQString(omod.ModName), MOBase::EGuessQuality::GUESS_META);

    bool nameNotCancelled;
    emit pickModName(nameNotCancelled, modName);
    if (!nameNotCancelled)
      return EInstallResult::RESULT_CANCELED;

    MOBase::IModInterface* modInterface;
    emit createMod(modInterface, modName);
    if (!modInterface)
      return EInstallResult::RESULT_CANCELED;

    {
      QTemporaryDir tempPath(modInterface->absolutePath() + "/OMODInstallTempXXXXXX");
      pushTempPath(tempPath.path());
      scope_guard tempPathGuard([this]() { this->popTempPath(); });

      if (omod.HasReadme)
        emit displayReadme(toQString(omod.ModName), toQString(omod.GetReadme()));

      if (omod.HasScript)
      {
        MOBase::log::debug("Mod has script. Run it.");
        OMODFramework::Scripting::IScriptFunctions^ scriptFunctions = gcnew ScriptFunctions(mParentWidget, mMoInfo);

        System::Collections::Generic::List<System::String^>^ activeBSAs = gcnew System::Collections::Generic::List<System::String^>();
        {
          // Hopefully some other part of MO2 also knows this and this can be refactored
          auto dataArchives = mMoInfo->gameFeatures()->gameFeature<MOBase::DataArchives>();
          // force-enabled by engine
          auto bsas = dataArchives->vanillaArchives();
          // explicitly enabled in INI. For Oblivion, excludes the vanilla BSAs by default.
          bsas.append(dataArchives->archives(mMoInfo->profile().get()));
          for (const auto& bsa : bsas)
          {
            QString path = mMoInfo->resolvePath(bsa);
            if (!path.isEmpty())
              activeBSAs->Add(toDotNetString(path));
          }

          std::map<int, QString> loadOrder;
          for (const auto& plugin : mMoInfo->pluginList()->pluginNames())
          {
            int loadIndex = mMoInfo->pluginList()->loadOrder(plugin);
            if (loadIndex != -1)
              loadOrder[loadIndex] = plugin;
          }
          for (const auto& [loadIndex, plugin] : loadOrder)
          {
            QString bsaPath = QFileInfo(plugin).completeBaseName() + ".bsa";
            bsaPath = mMoInfo->resolvePath(bsaPath);
            if (!bsaPath.isEmpty())
              activeBSAs->Add(toDotNetString(bsaPath));
          }
        }
        OMODFramework::Framework::LoadBSAs(activeBSAs);
        scope_guard bsaGuard([]() { OMODFramework::Framework::ClearBSAs(); });

        System::String^ dataPath = omod.GetDataFiles();
        System::String^ pluginsPath = omod.GetPlugins();
        OMODFramework::ScriptReturnData^ scriptData = OMODFramework::Scripting::ScriptRunner::RunScript(%omod, scriptFunctions, dataPath, pluginsPath);
        if (!scriptData)
          throw std::runtime_error("OMOD script returned no result. This isn't supposed to happen.");
        if (scriptData->CancelInstall)
          return EInstallResult::RESULT_CANCELED;

        // inis first so that you don't need to wait for extraction before a second batch of questions appears
        if (scriptData->INIEdits && scriptData->INIEdits->Count)
        {
          QMap<QString, QMap<QString, QString>> iniEdits;
          for each (OMODFramework::INIEditInfo ^ edit in scriptData->INIEdits)
            iniEdits[toQString(edit->Section)][toQString(edit->Name)] = toQString(edit->NewValue);
          // This feels like something I shouldn't need to do manually
          QVariantMap iniEditsVariants;
          for (const auto& section : iniEdits.keys())
          {
            QVariantMap innerMap;
            for (const auto& setting : iniEdits[section])
              innerMap[setting] = iniEdits[section][setting];
            iniEditsVariants[section] = innerMap;
          }
          modInterface->setPluginSetting("Omod Installer", toQString(omod.ModName) + ".iniEdits", iniEditsVariants);

          QString oblivionIniPath = mMoInfo->profile()->absoluteIniFilePath("Oblivion.ini");
          bool yesToAll = false;
          for each (OMODFramework::INIEditInfo ^ edit in scriptData->INIEdits)
          {
            QString section = toQString(edit->Section);
            section = section.mid(1, section.size() - 2);
            QString name = toQString(edit->Name);
            QString newValue = toQString(edit->NewValue);
            QString oldValue;
            if (edit->OldValue)
              oldValue = toQString(edit->OldValue);
            else
            {
              // I'm pretty sure this is the maximum length for vanilla Oblivion.
              wchar_t buffer[256];
              if (GetPrivateProfileString(section.toStdWString().data(), name.toStdWString().data(), nullptr, buffer, sizeof(buffer) / sizeof(buffer[0]), oblivionIniPath.toStdWString().data()))
                oldValue = QString::fromWCharArray(buffer);
            }

            MOBase::log::debug("OMOD wants to set [{}] {} to \"{}\", was \"{}\"", section, name, newValue, oldValue);

            if (oldValue == newValue)
            {
              MOBase::log::debug("Value is unchanged, not nagging user.");
              continue;
            }

            QMessageBox::StandardButton response;
            if (!yesToAll)
            {
              QString message;
              if (!oldValue.isEmpty())
              {
                /*: %1 is the mod name
                    [%2] is the ini section name.
                    %3 is the ini setting name.
                    %4 is the value already in Oblivion.ini.
                    %5 is the value the mod wants to set.
                */
                message = tr("%1 wants to change [%2] %3 from \"%4\" to \"%5\"").arg(modName).arg(section).arg(name).arg(oldValue).arg(newValue);
              }
              else
              {
                /*: %1 is the mod name
                    [%2] is the ini section name.
                    %3 is the ini setting name.
                    %5 is the value the mod wants to set.
                */
                message = tr("%1 wants to set [%2] %3 to \"%4\"").arg(modName).arg(section).arg(name).arg(newValue);
              }

              response = messageBoxHelper->question(mParentWidget, tr("Update INI?"), message, QMessageBox::Yes | QMessageBox::No | QMessageBox::YesToAll | QMessageBox::NoToAll);
              if (response == QMessageBox::NoToAll)
              {
                MOBase::log::debug("User skipped all.");
                break;
              }

              yesToAll |= response == QMessageBox::YesToAll;
            }

            if (yesToAll || response == QMessageBox::StandardButton::Yes)
            {
              MOBase::log::debug("Doing edit.");
              MOBase::WriteRegistryValue(section.toStdWString().data(), name.toStdWString().data(), newValue.toStdWString().data(), oblivionIniPath.toStdWString().data());
            }
            else
              MOBase::log::debug("User skipped edit.");
          }
        }

        scriptData->Pretty(%omod, dataPath, pluginsPath);
        // no compatability between auto and var makes me :angery:
        System::Collections::Generic::HashSet<System::String^>^ installedPlugins = gcnew System::Collections::Generic::HashSet<System::String^>(System::StringComparer::InvariantCultureIgnoreCase);
        for each (OMODFramework::InstallFile file in scriptData->InstallFiles)
        {
          System::String^ destinationPath = System::IO::Path::Combine(toDotNetString(modInterface->absolutePath()), file.InstallTo);
          System::IO::Directory::CreateDirectory(System::IO::Path::GetDirectoryName(destinationPath));
          System::IO::File::Copy(file.InstallFrom, destinationPath, true);
          System::String^ extension = System::IO::Path::GetExtension(file.InstallTo);
          if (extension && (extension->Equals(".esm", System::StringComparison::InvariantCultureIgnoreCase) || extension->Equals(".esp", System::StringComparison::InvariantCultureIgnoreCase)))
            installedPlugins->Add(file.InstallTo);
        }

        if (scriptData->UncheckedPlugins)
          installedPlugins->ExceptWith(scriptData->UncheckedPlugins);

        QStringList defaultActivePlugins;
        for each (System::String ^ plugin in installedPlugins)
          defaultActivePlugins.append(toQString(plugin));
        modInterface->setPluginSetting("Omod Installer", toQString(omod.ModName) + ".defaultActivePlugins", defaultActivePlugins);
        QStringList defaultInactivePlugins;
        if (scriptData->UncheckedPlugins)
        {
          for each (System::String ^ plugin in scriptData->UncheckedPlugins)
            defaultInactivePlugins.append(toQString(plugin));
        }
        modInterface->setPluginSetting("Omod Installer", toQString(omod.ModName) + ".defaultInactivePlugins", defaultInactivePlugins);

        QStringList registeredBSAs;
        if (scriptData->RegisterBSASet)
        {
          for each (System::String ^ bsa in scriptData->RegisterBSASet)
            registeredBSAs.append(toQString(bsa));
        }
        modInterface->setPluginSetting("Omod Installer", toQString(omod.ModName) + ".registeredBSAs", registeredBSAs);

        if (scriptData->SDPEdits && scriptData->SDPEdits->Count)
        {
          for each (OMODFramework::SDPEditInfo ^ shaderEdit in scriptData->SDPEdits)
          {
            System::String^ destinationPath = System::IO::Path::Combine(toDotNetString(modInterface->absolutePath()), "Shaders", "OMOD", toDotNetString(QString::number(shaderEdit->Package)));
            System::IO::Directory::CreateDirectory(destinationPath);
            System::IO::File::Copy(shaderEdit->BinaryObject, System::IO::Path::Combine(destinationPath, shaderEdit->Shader), true);
          }
        }

        std::map<QString, int> unhandledScriptReturnDataCounts;
        // This is a mapping from plugin name to an enum saying whether OBMM should allow the user to deactivate an ESP from the OMOD, disallow it, or just warn. By default, it'd warn.
        unhandledScriptReturnDataCounts["ESPDeactivation"] = scriptData->ESPDeactivation ? scriptData->ESPDeactivation->Count : 0;
        // There's nothing in the OBMM documentation claiming the function that sets this exists.
        unhandledScriptReturnDataCounts["EarlyPlugins"] = scriptData->EarlyPlugins ? scriptData->EarlyPlugins->Count : 0;
        // Sets load order a.esp, b.esp, true and b.esp, a.esp, false both mean the same thing.
        unhandledScriptReturnDataCounts["LoadOrderSet"] = scriptData->LoadOrderSet ? scriptData->LoadOrderSet->Count : 0;
        // Says this OMOD conflicts with another, potentially with a description.
        unhandledScriptReturnDataCounts["ConflictsWith"] = scriptData->ConflictsWith ? scriptData->ConflictsWith->Count : 0;
        // Says this OMOD depends on another, potentially with a description.
        unhandledScriptReturnDataCounts["DependsOn"] = scriptData->DependsOn ? scriptData->DependsOn->Count : 0;

        // Contains a list of GMSTs and Globals to edit in the mod's ESPs. May a higher being have mercy on us if anyone ever used this.
        unhandledScriptReturnDataCounts["ESPEdits"] = scriptData->ESPEdits ? scriptData->ESPEdits->Count : 0;
        // OMODFramework is handling this for us, so don't sweat it.
        unhandledScriptReturnDataCounts["PatchFiles"] = scriptData->PatchFiles ? scriptData->PatchFiles->Count : 0;

        for (const auto& [name, usageCount] : unhandledScriptReturnDataCounts)
        {
          if (usageCount)
          {
            /*: %1 is the mod name
                %2 is the name of a field in the OMOD's return data
                Hopefully this message will never be seen by anyone, but if it is, they need to know to tell the Mod Organizer 2 dev team.
            */
            QString userMessage = tr("%1 has data for %2, but Mod Organizer 2 doesn't know what to do with it yet. Please report this to the Mod Organizer 2 development team (ideally by sending us your interface log) as we didn't find any OMODs that actually did this, and we need to know that they exist.");
            userMessage = userMessage.arg(toQString(omod.ModName));
            userMessage = userMessage.arg(name);
            messageBoxHelper->warning(mParentWidget, tr("Mod Organizer 2 can't completely install this OMOD."), userMessage);
            MOBase::log::warn("{} ({}) contains {} entries for {}", toUTF8String(omod.ModName), archiveName, usageCount, name);
          }
        }
      }
      else
      {
        MOBase::log::debug("Mod has no script. Install contents directly.");
        QString data = toQString(omod.GetDataFiles());
        QString plugins = toQString(omod.GetPlugins());
        if (!data.isNull())
        {
          if (MOBase::shellMove(data + "/*.*", modInterface->absolutePath(), true, mParentWidget))
            MOBase::log::debug("Installed mod files.");
          else
            MOBase::log::error("Error while installing mod files.");
          QFile::remove(data);
        }
        if (!plugins.isNull())
        {
          QStringList defaultActivePlugins = QDir(plugins).entryList({ "*.esm", "*.esp" }, QDir::Files);
          modInterface->setPluginSetting("Omod Installer", toQString(omod.ModName) + ".defaultActivePlugins", defaultActivePlugins);
          modInterface->setPluginSetting("Omod Installer", toQString(omod.ModName) + ".defaultInactivePlugins", QStringList());
          if (MOBase::shellMove(plugins + "/*.*", modInterface->absolutePath(), true, mParentWidget))
            MOBase::log::debug("Installed mod plugins.");
          else
            MOBase::log::error("Error while installing mod plugins.");
          QFile::remove(plugins);
        }
      }

      // on success, set mod info
      MOBase::VersionInfo modVersion(std::max(int(omod.MajorVersion), 0), std::max(int(omod.MinorVersion), 0), std::max(int(omod.BuildVersion), 0));
      modInterface->setVersion(modVersion);

      // TODO: parse omod.Website. If it's Nexus, set the ID, otherwise set custom URL in meta.ini. We can't set the URL with the installation manager.
      // TODO: maybe convert omod.Description to HTML and set it as nexusDescription
      // TODO: maybe Holt will finish the proposed mod metadata API and there'll be a better, tidier option.
      if (nexusID != -1)
        modInterface->setNexusID(nexusID);

      modInterface->setInstallationFile(archiveName);
    }

    OMODFramework::Framework::CleanTempDir(false);

    QStringList omodsPendingPostInstall = modInterface->pluginSetting("Omod Installer", "omodsPendingPostInstall", QStringList()).toStringList();
    if (!omodsPendingPostInstall.contains(toQString(omod.ModName)))
    {
      omodsPendingPostInstall.append(toQString(omod.ModName));
      modInterface->setPluginSetting("Omod Installer", "omodsPendingPostInstall", omodsPendingPostInstall);
    }
    return EInstallResult::RESULT_SUCCESS;
  }
  catch (const std::exception& e)
  {
    throw;
  }
  catch (System::Exception^ dotNetException)
  {
    throw toStdException(dotNetException);
  }
}

void OMODFrameworkWrapper::setParentWidget(QWidget* parentWidget)
{
  mParentWidget = parentWidget;
  if (OMODFramework::Framework::Settings->CodeProgress)
    static_cast<CodeProgress^>(OMODFramework::Framework::Settings->CodeProgress)->setParentWidget(mParentWidget);
}

const std::array<QString, 3> pluginStateNames = { "STATE_MISSING", "STATE_INACTIVE", "STATE_ACTIVE" };

void OMODFrameworkWrapper::onInstallationEnd(EInstallResult status, MOBase::IModInterface* mod)
{
  if (status != EInstallResult::RESULT_SUCCESS || !mod)
    return;

  QStringList omodsPendingPostInstall = mod->pluginSetting("Omod Installer", "omodsPendingPostInstall", QStringList()).toStringList();

  if (!omodsPendingPostInstall.empty() && !(mMoInfo->modList()->state(mod->name()) & MOBase::IModList::STATE_ACTIVE))
  {
    auto response = QMessageBox::question(mParentWidget, tr("Activate mod?"),
                                        /*: %1 is the left-pane mod name.
                                            %2 is the name from the metadata of an OMOD.
                                        */
                                        tr("%1 contains the OMOD %2. OMODs may have post-installation actions like activating ESPs. Would you like to enable the mod so this can happen now?").arg(mod->name()).arg(omodsPendingPostInstall[0]),
                                        QMessageBox::Yes | QMessageBox::No);

    if (response == QMessageBox::StandardButton::Yes)
      mMoInfo->modList()->setActive(mod->name(), true);
    else
      return;
  }

  for (const auto& omodName : omodsPendingPostInstall)
  {
    MOBase::log::debug("Running post-install for {}", omodName);
    QStringList defaultActivePlugins = mod->pluginSetting("Omod Installer", omodName + ".defaultActivePlugins", QStringList()).toStringList();
    QStringList defaultInactivePlugins = mod->pluginSetting("Omod Installer", omodName + ".defaultInactivePlugins", QStringList()).toStringList();

    bool yesToAll = false;
    for (const auto& plugin : defaultActivePlugins)
    {
      MOBase::IPluginList::PluginStates oldState = mMoInfo->pluginList()->state(plugin);
      MOBase::log::debug("OMOD wants to activate {}, was {}", plugin, pluginStateNames[oldState]);

      QMessageBox::StandardButton response;
      if (!yesToAll)
      {
        QString message;
        if (oldState == MOBase::IPluginList::STATE_INACTIVE)
        {
          /*: %1 is the mod name.
              %2 is the plugin name.
          */
          message = tr("%1 wants to activate %2. Do you want to do so?").arg(omodName).arg(plugin);

          response = QMessageBox::question(mParentWidget, tr("Activate plugin?"), message, QMessageBox::Yes | QMessageBox::No | QMessageBox::YesToAll | QMessageBox::NoToAll);
          if (response == QMessageBox::NoToAll)
          {
            MOBase::log::debug("User skipped all.");
            break;
          }

          yesToAll |= response == QMessageBox::YesToAll;
        }
        else
        {
          if (oldState == MOBase::IPluginList::STATE_MISSING)
            QMessageBox::warning(mParentWidget, tr("OMOD wants to activate missing plugin"), tr("An OMOD wants to activate a missing plugin. This shouldn't be possible. Please report this to a MO2 developer."));
          continue;
        }
      }

      if (yesToAll || response == QMessageBox::StandardButton::Yes)
      {
        MOBase::log::debug("Activating plugin.");
        mMoInfo->pluginList()->setState(plugin, MOBase::IPluginList::STATE_ACTIVE);
      }
      else
        MOBase::log::debug("User skipped plugin.");
    }

    yesToAll = false;
    for (const auto& plugin : defaultInactivePlugins)
    {
      MOBase::IPluginList::PluginStates oldState = mMoInfo->pluginList()->state(plugin);
      MOBase::log::debug("OMOD installed {}, but didn't try and activate it. State was {}", plugin, pluginStateNames[oldState]);

      QMessageBox::StandardButton response;
      if (!yesToAll)
      {
        QString message;
        if (oldState == MOBase::IPluginList::STATE_INACTIVE)
        {
          /*: %1 is the mod name.
              %2 is the plugin name.
          */
          message = tr("%1 installed %2, but doesn't activate it by default. Do you want to activate it anyway?").arg(omodName).arg(plugin);

          response = QMessageBox::question(mParentWidget, tr("Activate plugin?"), message, QMessageBox::Yes | QMessageBox::No | QMessageBox::YesToAll | QMessageBox::NoToAll);
          if (response == QMessageBox::NoToAll)
          {
            MOBase::log::debug("User skipped all.");
            break;
          }

          yesToAll |= response == QMessageBox::YesToAll;
        }
        else
        {
          if (oldState == MOBase::IPluginList::STATE_MISSING)
            QMessageBox::warning(mParentWidget, tr("OMOD claimed to install missing plugin"), tr("An OMOD has activation settings for a missing plugin. This shouldn't be possible. Please report this to a MO2 developer."));
          continue;
        }
      }

      if (yesToAll || response == QMessageBox::StandardButton::Yes)
      {
        MOBase::log::debug("Activating plugin.");
        mMoInfo->pluginList()->setState(plugin, MOBase::IPluginList::STATE_ACTIVE);
      }
      else
        MOBase::log::debug("User skipped plugin.");
    }

    // this is still ugly.
    mMoInfo->gameFeatures()->gameFeature<MOBase::GamePlugins>()->writePluginLists(mMoInfo->pluginList());


    QStringList registeredBSAs = mod->pluginSetting("Omod Installer", omodName + ".registeredBSAs", QStringList()).toStringList();
    if (!registeredBSAs.empty())
    {
      MOBase::log::debug("OMOD wants to register BSAs. We can't do that.");
      QMessageBox::warning(mParentWidget, tr("Register BSAs"),
                           /*: %1 is the OMOD name
                               <ul><li>%2</li></ul> becomes a list of BSA files
                           */
                           tr("%1 wants to register the following BSA archives, but Mod Organizer 2 can't do that yet due to technical limitations:<ul><li>%2</li></ul>For now, your options include adding the BSA names to <code>sResourceArchiveList</code> in the game INI, creating a dummy ESP with the same name, or extracting the BSA, all of which have drawbacks.")
                             .arg(omodName).arg(registeredBSAs.join("</li><li>"))
                           );
    }
  }

  mod->setPluginSetting("Omod Installer", "omodsPendingPostInstall", QStringList());
}

void OMODFrameworkWrapper::initFrameworkSettings()
{
  OMODFramework::Framework::Settings->CodeProgress = gcnew CodeProgress(mParentWidget);

  // This is a hack to fix an OMOD framework bug and should be removed once it's fixed.
  OMODFramework::Framework::Settings->DllPath = System::IO::Path::Combine(System::IO::Path::GetDirectoryName(OMODFramework::Framework::Settings->DllPath), "OMODFramework.Scripting.dll");

  OMODFramework::LoggingSettings^ loggingSettings = OMODFramework::Framework::Settings->LoggingSettings;
  loggingSettings->LogToFile = false;
  loggingSettings->LowestLoggingLevel = Logger::OMODLoggingLevel(MOBase::log::getDefault().level());
  loggingSettings->Logger = gcnew Logger();
  loggingSettings->UseLogger = true;

  OMODFramework::ScriptExecutionSettings^ scriptSettings = gcnew OMODFramework::ScriptExecutionSettings();
  scriptSettings->EnableWarnings = true;
  scriptSettings->ReadINIWithInterface = false;
  scriptSettings->ReadRendererInfoWithInterface = false;
  scriptSettings->HandleBSAsWithInterface = false;
  scriptSettings->PatchWithInterface = false;
  scriptSettings->UseSafePatching = true;

  OMODFramework::Framework::Settings->ScriptExecutionSettings = scriptSettings;
}

void OMODFrameworkWrapper::refreshFrameworkSettings()
{
  OMODFramework::ScriptExecutionSettings^ scriptSettings = OMODFramework::Framework::Settings->ScriptExecutionSettings;

  if (scriptSettings && mMoInfo->managedGame())
  {
    // the managed game isn't set during initFrameworkSettings, so only do this here
    scriptSettings->OblivionGamePath = toDotNetString(mMoInfo->managedGame()->gameDirectory().path());
    scriptSettings->OblivionINIPath = toDotNetString(mMoInfo->profile()->absoluteIniFilePath("Oblivion.ini"));
    scriptSettings->OblivionRendererInfoPath = System::IO::Path::Combine(toDotNetString(mMoInfo->managedGame()->documentsDirectory().path()), "RendererInfo.txt");
  }
}

void OMODFrameworkWrapper::pushTempPath(const QString& tempPath)
{
  if (!tempPath.isEmpty())
    OMODFramework::Framework::Settings->TempPath = toDotNetString(tempPath);
  mTempPathStack.push(tempPath);
}

void OMODFrameworkWrapper::popTempPath()
{
  if (mTempPathStack.count() >= 2)
    mTempPathStack.pop();
  OMODFramework::Framework::Settings->TempPath = toDotNetString(mTempPathStack.top());
}

void OMODFrameworkWrapper::pickModNameSlot(bool& successOut, MOBase::GuessedValue<QString>& modName)
{
  NameDialog nameDialog(modName, mParentWidget);
  successOut = nameDialog.exec() == QDialog::Accepted;
  if (successOut)
    modName.update(nameDialog.getName(), MOBase::EGuessQuality::GUESS_USER);
}

void OMODFrameworkWrapper::createModSlot(MOBase::IModInterface*& modInterfaceOut, MOBase::GuessedValue<QString>& modName)
{
  modInterfaceOut = mMoInfo->createMod(modName);
}

void OMODFrameworkWrapper::displayReadmeSlot(const QString& modName, const QString& readme)
{
  if (QMessageBox::question(mParentWidget, tr("Display Readme?"),
    //: <br> is a line break. Translators can remove it if it makes things clearer.
    tr("The Readme may explain installation options. Display it?<br>It will remain visible until you close it.")) == QMessageBox::StandardButton::Yes)
  {
    // TODO: ideally this wouldn't be part of the same window heirarchy so that modal popups in the installer don't prevent it being moved/resized etc.
    // DarNified UI's popups are modal for the whole process, so any fancy trick needs to be *here*.
    RtfPopup* readmePopup = new RtfPopup(toDotNetString(readme), mParentWidget);
    //: %1 is the mod name
    readmePopup->setWindowTitle(tr("%1 Readme").arg(modName));
    readmePopup->show();
    readmePopup->setAttribute(Qt::WA_DeleteOnClose);
  }
}

void OMODFrameworkWrapper::showWaitDialogSlot(QString message) {
  mWaitDialog.reset(new QProgressDialog(message, QString(), 0, 0, mParentWidget));
  mWaitDialog->setWindowFlags(mWaitDialog->windowFlags() & ~Qt::WindowContextHelpButtonHint & ~Qt::WindowCloseButtonHint);
  mWaitDialog->setWindowModality(Qt::WindowModal);
  mWaitDialog->show();
}

void OMODFrameworkWrapper::hideWaitDialogSlot() {
  if (mWaitDialog) {
    mWaitDialog->hide();
    mWaitDialog.reset();
  }
}
