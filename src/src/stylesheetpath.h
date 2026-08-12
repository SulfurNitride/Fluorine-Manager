#ifndef STYLESHEETPATH_H
#define STYLESHEETPATH_H

#include <QString>
#include <QStringList>

namespace StyleSheetPath
{

// Installed styles take precedence over portable-instance styles with the same
// filename. Empty, missing and escaping directories are ignored.
QStringList searchDirectories(const QString& applicationDirectory,
                              const QString& instanceDirectory);

// Resolve a stylesheet basename inside one of the supplied directories. Files
// reached through a symlink outside their containing stylesheet directory are
// rejected.
QString resolve(const QString& styleName, const QStringList& directories);

// Return unique stylesheet basenames in search precedence order.
QStringList available(const QStringList& directories);

// Resolve a relative QSS url inside stylesheetDirectory without modifying the
// theme on disk. Exact spelling wins; otherwise every path component must
// have one unambiguous case-insensitive match. URLs, resources and absolute
// paths are returned unchanged; paths which escape the directory are rejected.
QString resolveAsset(const QString& url, const QString& stylesheetDirectory);

// Rewrite relative url(...) references to quoted absolute paths. Rejected
// escaping references become empty url() values; unresolved safe paths retain
// the historical stylesheet-relative lookup behavior.
QString resolveAssets(const QString& stylesheet,
                      const QString& stylesheetDirectory);

}  // namespace StyleSheetPath

#endif  // STYLESHEETPATH_H
