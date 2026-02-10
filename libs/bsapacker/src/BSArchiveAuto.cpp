#include <qlibbsarch/BSArchiveAuto.h>
#include <qlibbsarch/QLibbsarch.h>

BSArchiveAuto::BSArchiveAuto(const QString &rootDirectory)
	: _rootDirectory(QDir::toNativeSeparators(QDir::cleanPath(rootDirectory)))
{
}

void BSArchiveAuto::open(const QString &archivePath)
{
	_archive.open(archivePath);
	qDebug() << "Opening archive: " << archivePath;
}

void BSArchiveAuto::create(const QString &archiveName, const bsa_archive_type_e &type)
{
	_archive.create(archiveName, type, _entries);

	for (auto it = _filesfromMemory.constBegin(); it != _filesfromMemory.constEnd(); ++it)
		_archive.addFileFromMemory(it.key(), it.value());

	for (const auto &file : _filesFromDiskRoot)
		_archive.addFileFromDiskRoot(_rootDirectory.path(), file);

	for (auto it = _filesFromDisk.constBegin(); it != _filesFromDisk.constEnd(); ++it)
		_archive.addFileFromDisk(it.key(), it.value());
}


void BSArchiveAuto::addFileFromDiskRoot(const QString &filename)
{
	_entries.add(_rootDirectory.relativeFilePath(filename));
	_filesFromDiskRoot << filename;
}


void BSArchiveAuto::addFileFromDiskRoot(const QStringList& files)
{
	for (const auto &file : files)
		addFileFromDiskRoot(file);
}

void BSArchiveAuto::addFileFromDisk(const QString &saveAs, const QString &diskPath)
{
	_entries.add(saveAs);
	_filesFromDisk.insert(saveAs, diskPath);
}

void BSArchiveAuto::addFileFromDisk(const QMap<QString, QString> &map)
{
	for (auto it = map.cbegin(); it != map.cend(); ++it)
		_filesFromDisk.insert(it.key(), it.value());
}

void BSArchiveAuto::addFileFromMemory(const QString &filename, const QByteArray &data)
{
	_entries.add(filename);
	_filesfromMemory.insert(filename, data);
}

void BSArchiveAuto::extractAll(const QString& destinationDirectory, const bool &overwriteExistingFiles)
{
	for (const auto &file : _archive.listFiles())
	{
		QFile currentFile(destinationDirectory + "/" + file);
		_rootDirectory.mkpath(destinationDirectory + "/" + QFileInfo(file).path());
		if(currentFile.exists() && overwriteExistingFiles)
		{
			currentFile.remove();
			_archive.extract(file, currentFile.fileName());
		}
		else if (!currentFile.exists())
			_archive.extract(file, currentFile.fileName());
	}
}

void BSArchiveAuto::save()
{
	_archive.save();
}

void BSArchiveAuto::reset()
{
	_archive.reset();
	_entries.reset();
}

void BSArchiveAuto::setShareData(const bool state)
{
	_archive.setShareData(state);
}

void BSArchiveAuto::setCompressed(const bool state)
{
	_archive.setCompressed(state);
}

void BSArchiveAuto::setDDSCallback(bsa_file_dds_info_proc_t fileDDSInfoProc, void *context)
{
	_archive.setDDSCallback(fileDDSInfoProc, context);
}
