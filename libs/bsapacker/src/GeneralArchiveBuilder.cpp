#include <bsapacker/GeneralArchiveBuilder.h>

#include <bsapacker/ArchiveBuilderHelper.h>
#include <QDirIterator>
#include <QApplication>
#include <QDebug>

using namespace libbsarch;

namespace BsaPacker
{
	// 2 GiB limit. This does not consider size after compression or share data
	const qint64 GeneralArchiveBuilder::SIZE_LIMIT = (qint64)1024 * 1024 * 1024 * 2;

	GeneralArchiveBuilder::GeneralArchiveBuilder(const IArchiveBuilderHelper* archiveBuilderHelper, const QDir& rootDir, const bsa_archive_type_t& type)
		: m_ArchiveBuilderHelper(archiveBuilderHelper), m_RootDirectory(rootDir), m_ArchiveType(type)
	{
		this->m_Cancelled = false;
		this->m_Archives.emplace_back(std::make_unique<libbsarch::bs_archive_auto>(this->m_ArchiveType));
	}

	uint32_t GeneralArchiveBuilder::setFiles()
	{
		uint32_t incompressibleFiles = 0;
		uint32_t compressibleFiles = 0;
		int count = 0;
		qint64 currentSize = 0;
		const auto& dirString = (this->m_RootDirectory.path() + '/').toStdWString();
		const auto& rootDirFiles = this->m_ArchiveBuilderHelper->getRootDirectoryFilenames(dirString);
		qDebug() << "root is: " << m_RootDirectory.path() + '/';

		QDirIterator iterator(this->m_RootDirectory.absolutePath(), QDir::Files, QDirIterator::Subdirectories);
		while (iterator.hasNext()) {
			QApplication::processEvents();

			if (this->m_Cancelled) {
				for (auto& archive : this->m_Archives) {
					archive.reset();
				}
				return 0;
			}

			const QFileInfo& fileInfo = iterator.nextFileInfo();
			const QString& filepath = fileInfo.absoluteFilePath();
			const bool ignored = this->m_ArchiveBuilderHelper->isFileIgnorable(filepath.toStdWString(), rootDirFiles);

			Q_EMIT this->valueChanged(++count);
			if (ignored) {
				continue;
			}

			currentSize += fileInfo.size();
			if (currentSize > SIZE_LIMIT) {
				currentSize = fileInfo.size();
				this->m_Archives.back()->set_compressed(!static_cast<bool>(incompressibleFiles));
				incompressibleFiles = 0;
				compressibleFiles = 0;
				this->m_Archives.emplace_back(std::make_unique<libbsarch::bs_archive_auto>(this->m_ArchiveType));
				this->setShareData(true);
			}

			this->m_ArchiveBuilderHelper->isIncompressible(filepath.toStdWString()) ? ++incompressibleFiles : ++compressibleFiles;
			auto fileBlob = disk_blob(
				 dirString,
				 filepath.toStdWString());
			this->m_Archives.back()->add_file_from_disk(fileBlob);
			qDebug() << "file is: " << filepath;
		}
		this->m_Archives.back()->set_compressed(!static_cast<bool>(incompressibleFiles));
		return incompressibleFiles + compressibleFiles;
	}

	void GeneralArchiveBuilder::setShareData(const bool value)
	{
		this->m_Archives.back()->set_share_data(value);
	}

	std::vector<std::unique_ptr<libbsarch::bs_archive_auto>> GeneralArchiveBuilder::getArchives()
	{
		return std::move(this->m_Archives);
	}

	uint32_t GeneralArchiveBuilder::getFileCount() const
	{
		return this->m_ArchiveBuilderHelper->getFileCount(this->m_RootDirectory.path().toStdWString());
	}

	QString GeneralArchiveBuilder::getRootPath() const
	{
		return this->m_RootDirectory.path();
	}

	void GeneralArchiveBuilder::cancel()
	{
		this->m_Cancelled = true;
	}
} // namespace BsaPacker
