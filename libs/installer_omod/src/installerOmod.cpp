#include "installerOmod.h"

#include <QRegularExpression>
#include <QTemporaryFile>

#include <uibase/iplugingame.h>
#include <uibase/log.h>

#include "OMODFrameworkWrapper.h"

InstallerOMOD::InstallerOMOD() : mMoInfo(nullptr), mOmodFrameworkWrapper(nullptr)
{
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
// IPlugin

bool InstallerOMOD::init(MOBase::IOrganizer* moInfo)
{
  mMoInfo = moInfo;
  mOmodFrameworkWrapper = std::make_unique<OMODFrameworkWrapper>(mMoInfo);
  mMoInfo->onAboutToRun([this](const QString&) { buildSDPs(); return true; });
  mMoInfo->onFinishedRun([this](const QString&, unsigned int) { clearSDPs(); });
  return true;
}

QString InstallerOMOD::name() const
{
  return "Omod Installer";
}

QString InstallerOMOD::localizedName() const
{
  return tr("Omod Installer");
}

QString InstallerOMOD::author() const
{
  return "AnyOldName3 & erril120";
}

QString InstallerOMOD::description() const
{
  return tr("Installer for Omod files (including scripted ones)");
}

MOBase::VersionInfo InstallerOMOD::version() const
{
  return MOBase::VersionInfo(1, 1, 0, MOBase::VersionInfo::RELEASE_FINAL);
}

std::vector<std::shared_ptr<const MOBase::IPluginRequirement>> InstallerOMOD::requirements() const
{
  return { Requirements::gameDependency("Oblivion") };
}

QList<MOBase::PluginSetting> InstallerOMOD::settings() const
{
  return {};
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
// IPluginInstaller

unsigned int InstallerOMOD::priority() const
{
  // Some other installers have a use_any_file setting and then they'll try and claim OMODs as their own, so we want higher priority than them.
  return 500;
}

bool InstallerOMOD::isManualInstaller() const
{
  return false;
}

void InstallerOMOD::onInstallationEnd(EInstallResult result, MOBase::IModInterface* newMod)
{
  mOmodFrameworkWrapper->onInstallationEnd(result, newMod);
}

bool InstallerOMOD::isArchiveSupported(std::shared_ptr<const MOBase::IFileTree> tree) const
{
  for (const auto file : *tree) {
    // config is the only file guaranteed to be there
    if (file->isFile() && file->name() == "config")
      return true;
  }
  return false;
}

void InstallerOMOD::setParentWidget(QWidget* parent)
{
  IPluginInstaller::setParentWidget(parent);
  mOmodFrameworkWrapper->setParentWidget(parent);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
// IPluginInstallerCustom

bool InstallerOMOD::isArchiveSupported(const QString& archiveName) const
{
  return archiveName.endsWith(".omod", Qt::CaseInsensitive);
}

std::set<QString> InstallerOMOD::supportedExtensions() const
{
  return { "omod" };
}

InstallerOMOD::EInstallResult InstallerOMOD::install(MOBase::GuessedValue<QString>& modName, QString gameName, const QString& archiveName, const QString& version, int nexusID)
{
  return mOmodFrameworkWrapper->installInAnotherThread(modName, gameName, archiveName, version, nexusID);
}

MappingType InstallerOMOD::mappings() const
{
  MOBase::log::debug("Mapping virtual SDPs");

  MappingType mappings;

  for (const auto& [realSDPPath, virtualSDP] : mVirtualSDPs)
    mappings.push_back({ virtualSDP->fileName(), realSDPPath, false, false });

  return mappings;
}

void InstallerOMOD::buildSDPs()
{
  QStringList directories = mMoInfo->listDirectories("Shaders/OMOD");
  directories = directories.filter(QRegularExpression("\\d+"));

  for (const auto& directory : directories)
  {
    QString packagePath = mMoInfo->resolvePath(QString("Shaders/shaderpackage%1.sdp").arg(directory.toInt(), 3, 10, QChar('0')));
    if (packagePath.isEmpty())
      continue;

    QMap<QByteArray, QByteArray> shaders;
    {
      QFile packageFile(packagePath);
      packageFile.open(QIODevice::ReadOnly);
      QDataStream basePackageStream(&packageFile);
      basePackageStream.setByteOrder(QDataStream::LittleEndian);
      quint32 magic;
      basePackageStream >> magic;
      if (magic != 100)
      {
        MOBase::log::debug("SDP magic number mismatch in {}, got {}, but expected 100", packagePath, magic);
        continue;
      }
      quint32 shaderCount, totalSize;
      basePackageStream >> shaderCount >> totalSize;
      for (quint32 i = 0; i < shaderCount && !basePackageStream.atEnd(); ++i)
      {
        QByteArray shaderName(256, '\0');
        basePackageStream.readRawData(shaderName.data(), 256);
        quint32 shaderSize;
        basePackageStream >> shaderSize;
        QByteArray shaderBytecode(shaderSize, '\0');
        basePackageStream.readRawData(shaderBytecode.data(), shaderSize);
        shaders[shaderName] = shaderBytecode;
      }
    }

    QStringList shaderFiles = mMoInfo->findFiles("Shaders/OMOD/" + directory, { "*.vso", "*.pso" });
    for (const auto& shaderFile : shaderFiles)
    {
      QFileInfo info(shaderFile);
      QByteArray shaderName = (info.baseName().toUpper() + "." + info.suffix().toLower()).toLatin1();
      QFile file(shaderFile);
      file.open(QIODevice::ReadOnly);
      shaderName = shaderName.leftJustified(256, '\0', true);
      shaders[shaderName] = file.readAll();
      MOBase::log::debug("Replacement for {} was {} bytes long", shaderFile, shaders[shaderName].size());
    }

    std::unique_ptr<QTemporaryFile> outputFile = std::make_unique<QTemporaryFile>();
    outputFile->open();
    QDataStream packageStream(outputFile.get());
    packageStream.setByteOrder(QDataStream::LittleEndian);
    packageStream << quint32(100) << quint32(shaders.count());

    quint32 totalSize = 0;
    for (const auto& shader : shaders)
      totalSize += 256 + 4 + shader.size();
    packageStream << totalSize;

    for (auto itr = shaders.cbegin(); itr != shaders.cend(); ++itr)
    {
      packageStream.writeRawData(itr.key().constData(), 256);
      packageStream << quint32(itr.value().size());
      packageStream.writeRawData(itr.value().constData(), itr.value().size());
    }

    outputFile->close();

    mVirtualSDPs.emplace(mMoInfo->managedGame()->dataDirectory().absoluteFilePath(QString("Shaders/shaderpackage%1.sdp").arg(directory.toInt(), 3, 10, QChar('0'))), std::move(outputFile));
  }
}

void InstallerOMOD::clearSDPs()
{
  mVirtualSDPs.clear();
}
