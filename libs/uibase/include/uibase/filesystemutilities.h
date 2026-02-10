#ifndef FILESYSTEMUTILIITES_H
#define FILESYSTEMUTILITIES_H

#include <QString>

#include "dllimport.h"

namespace MOBase
{
/**
 * @brief fix a directory name so it can be dealt with by windows explorer
 * @return false if there was no way to convert the name into a valid one
 **/
QDLLEXPORT bool fixDirectoryName(QString& name);

/**
 * @brief ensures a file name is valid
 *
 * @param name the file name being sanitized
 * @param replacement invalid characters are replaced with this string
 * @return the sanitized file name
 **/
QDLLEXPORT QString sanitizeFileName(const QString& name,
                                    const QString& replacement = QString(""));

/**
 * @brief checks file name validity per sanitizeFileName
 *
 * @param name the file name being checked
 * @return true if the given file name is valid
 **/
QDLLEXPORT bool validFileName(const QString& name);

}  // namespace MOBase

#endif  // FILESYSTEM_H
