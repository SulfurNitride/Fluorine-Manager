#pragma once

#include "BSArchive.h"
#include "BSArchiveEntries.h"
#include <QMap>

/*!
 * \brief A convenience class for BSArchive and BSArchiveEntries. Its performance is worse than using these
 * two classes separately, but it removes the need to manually handle the BSArchiveEntries.
 */
class BSArchiveAuto
{
public:
    /*!
     * \brief Constructor
     * \param rootDirectory The root directory of the BSA. This directory is the one containing folders such as textures and meshes.
     */
    BSArchiveAuto(const QString &_rootDirectory);
    /*!
     * \brief Opens an existing archive
     * \param archivePath The path of the archive
     */
    void open(const QString &archivePath);
    /*!
     * \brief Creates a BSA in memory
     * \param archiveName The BSA name
     * \param type The BSA type
     */
    void create(const QString& archiveName, const bsa_archive_type_e& type);
    /*!
     * \brief Adds a file from the disk to the BSA. Also adds it to the BSA entries.
     * \param rootDir The root directory of the file.
     * \param filename The complete path to the file.
     */
    void addFileFromDiskRoot(const QString& filename);
    /*!
     * \brief Adds files from the disk to the BSA. Also adds them to the BSA entries.
     * \param rootDir The root directory of the file. This directory is the one containing folders such as textures and meshes.
     * \param files A list containing the complete paths to the files.
     */
    void addFileFromDiskRoot(const QStringList& files);
    /*!
     * \brief Adds file from the memory to the BSA. Also adds it to the BSA entries.
     * \param filename The name of the file. It connot be a full path, and has to be a relative path.
     * \param data The file bytes data.
     */
    void addFileFromMemory(const QString& filename, const QByteArray& data);
    /*!
     * \brief Adds a file from the disk to the BSA. Also adds it to the BSA entries.
     * \param saveAs The path of the file in the BSA
     * \param diskPath The path of the file on disk
     */
    void addFileFromDisk(const QString& saveAs, const QString& diskPath);
    /*!
     * \brief Adds files from the disk to the BSA. Also adds it to the BSA entries.
     * \param map A map containing "save as" path as keys and disk path as values
     */
    void addFileFromDisk(const QMap<QString, QString>& map);
    /*!
     * \brief Extracts all files from the BSA to the destination.
     * \param destinationDirectory The directory where all files will be extracted.
     * \param overwriteExistingFiles Whether files in archive will overwrite existing loose files or not
     */
    void extractAll(const QString &destinationDirectory, const bool &overwriteExistingFiles);
    /*!
     * \brief Saves the archive to the disk
     */
    void save();

    void setShareData(const bool state);
    void setCompressed(const bool state);

    void reset();
    /*!
     * \brief Required in order to create FO4 DDS archive
     * \param fileDDSInfoProc The function that will process the DDS file
     */
    void setDDSCallback(bsa_file_dds_info_proc_t fileDDSInfoProc, void *context);

private:
    /*!
     * \brief The key will store the "save as" path, while the value will hold the disk path
     */
    QMap<QString, QString> _filesFromDisk;
    QStringList _filesFromDiskRoot;
    QMap<QString, QByteArray> _filesfromMemory;
    QDir _rootDirectory;

    BSArchive _archive;
    BSArchiveEntries _entries;
};
