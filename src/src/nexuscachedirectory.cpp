#include "nexuscachedirectory.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkDiskCache>
#include <QObject>
#include <QSaveFile>
#include <QTemporaryDir>

#include <memory>

namespace NexusCacheDirectory
{
namespace
{
constexpr auto NamespaceDirectory = ".fluorine-manager";
constexpr auto CacheDirectory     = "nexus-network-v1";
constexpr auto OwnershipMarker    = ".fluorine-cache-owner";
constexpr auto OwnershipBytes     = "Fluorine Manager Nexus network cache v1\n";
constexpr auto ExpectedPathProperty = "fluorineNexusCacheDirectory";
constexpr auto QuarantinePathProperty = "fluorineNexusCacheQuarantine";
constexpr auto QuarantineObjectName = "fluorineNexusCacheQuarantineOwner";

class CacheQuarantine final : public QObject
{
public:
  explicit CacheQuarantine(const QString& fileTemplate)
      : directory(fileTemplate.isEmpty()
                      ? QDir::temp().filePath("fluorine-nexus-cache-XXXXXX")
                      : fileTemplate)
  {
    setObjectName(QuarantineObjectName);
  }

  QTemporaryDir directory;
};

Result ensureConfiguredRoot(const QString& path)
{
  QFileInfo info(path);
  if (info.exists()) {
    if (!info.isDir()) {
      return {Status::InvalidRoot, {}, path};
    }
    return {Status::Ready, path, {}};
  }

  // A dangling link is an occupied pathname, not an absent cache root that we
  // may replace. Existing links to directories remain supported because the
  // configured cache root has historically allowed user-selected indirection.
  if (info.isSymLink()) {
    return {Status::Collision, {}, path};
  }

  const QString parentPath = info.absolutePath();
  const QFileInfo parent(parentPath);
  if (!parent.exists() || !parent.isDir() || info.fileName().isEmpty()) {
    return {Status::InvalidRoot, {}, path};
  }

  if (!QDir(parentPath).mkdir(info.fileName())) {
    return {Status::IoError, {}, path};
  }

  info.refresh();
  if (info.isSymLink() || !info.exists() || !info.isDir()) {
    return {Status::Collision, {}, path};
  }

  return {Status::Ready, path, {}};
}

Result ensureChildDirectory(const QString& parent, const QString& name)
{
  const QString path = QDir(parent).filePath(name);
  QFileInfo info(path);

  if (info.isSymLink() || (info.exists() && !info.isDir())) {
    return {Status::Collision, {}, path};
  }

  if (!info.exists() && !QDir(parent).mkdir(name)) {
    return {Status::IoError, {}, path};
  }

  // Revalidate the created/resolved leaf. This is a fail-closed ownership
  // check, not a same-user directory-race defence.
  info.refresh();
  if (info.isSymLink() || !info.exists() || !info.isDir()) {
    return {Status::Collision, {}, path};
  }

  return {Status::Ready, path, {}};
}

Result validateOwnedDirectory(const QString& path)
{
  const QFileInfo root(path);
  if (root.isSymLink() || !root.exists() || !root.isDir()) {
    return {Status::Collision, {}, path};
  }

  const QString markerPath = QDir(path).filePath(OwnershipMarker);
  const QFileInfo markerInfo(markerPath);
  if (markerInfo.isSymLink() || !markerInfo.exists() || !markerInfo.isFile() ||
      markerInfo.size() != qstrlen(OwnershipBytes)) {
    return {Status::Collision, {}, markerPath};
  }

  QFile marker(markerPath);
  if (!marker.open(QIODevice::ReadOnly) || marker.readAll() != OwnershipBytes) {
    return {Status::Collision, {}, markerPath};
  }

  QDirIterator entries(path,
                       QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden |
                           QDir::System,
                       QDirIterator::Subdirectories);
  while (entries.hasNext()) {
    entries.next();
    const QFileInfo info = entries.fileInfo();
    if (info.isSymLink() || (!info.isDir() && !info.isFile())) {
      return {Status::Collision, {}, info.filePath()};
    }
  }

  return {Status::Ready, QDir::cleanPath(path), {}};
}

Result claimCacheDirectory(const QString& path)
{
  const QString markerPath = QDir(path).filePath(OwnershipMarker);
  QFileInfo markerInfo(markerPath);
  if (markerInfo.exists() || markerInfo.isSymLink()) {
    return validateOwnedDirectory(path);
  }

  const QDir directory(path);
  if (!directory.entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden |
                           QDir::System)
           .isEmpty()) {
    return {Status::Collision, {}, path};
  }

  QSaveFile marker(markerPath);
  marker.setDirectWriteFallback(false);
  if (!marker.open(QIODevice::WriteOnly) ||
      marker.write(OwnershipBytes) != qstrlen(OwnershipBytes) || !marker.commit()) {
    return {Status::IoError, {}, markerPath};
  }

  return validateOwnedDirectory(path);
}
}

Result prepare(const QString& configuredRoot)
{
  if (configuredRoot.isEmpty() || !QDir::isAbsolutePath(configuredRoot)) {
    return {Status::InvalidRoot, {}, configuredRoot};
  }

  const QString rootPath = QDir::cleanPath(configuredRoot);
  const Result root = ensureConfiguredRoot(rootPath);
  if (!root) {
    return root;
  }

  const Result nameSpace = ensureChildDirectory(rootPath, NamespaceDirectory);
  if (!nameSpace) {
    return nameSpace;
  }

  const Result cache = ensureChildDirectory(nameSpace.path, CacheDirectory);
  if (!cache) {
    return cache;
  }

  return claimCacheDirectory(cache.path);
}

Result configure(QNetworkAccessManager& manager, const QString& configuredRoot,
                 const QString& quarantineTemplate)
{
  const Result prepared = prepare(configuredRoot);
  if (!prepared) {
    return prepared;
  }

  auto* diskCache = qobject_cast<QNetworkDiskCache*>(manager.cache());
  if (manager.cache() != nullptr && diskCache == nullptr) {
    return {Status::UnexpectedCache, {}, {}};
  }

  QObject* oldQuarantine = nullptr;
  if (diskCache != nullptr) {
    oldQuarantine = diskCache->findChild<QObject*>(
        QuarantineObjectName, Qt::FindDirectChildrenOnly);
    if (diskCache->property(ExpectedPathProperty).toString().isEmpty() ||
        oldQuarantine == nullptr) {
      return {Status::UnexpectedCache, {}, {}};
    }
  }

  auto quarantine = std::make_unique<CacheQuarantine>(quarantineTemplate);
  if (!quarantine->directory.isValid()) {
    return {Status::IoError, {}, quarantineTemplate};
  }

  if (diskCache == nullptr) {
    diskCache = new QNetworkDiskCache;
  }

  quarantine->setParent(diskCache);
  diskCache->setCacheDirectory(prepared.path);
  diskCache->setProperty(ExpectedPathProperty, QDir::cleanPath(prepared.path));
  diskCache->setProperty(QuarantinePathProperty,
                         quarantine->directory.path());

  if (manager.cache() == nullptr) {
    manager.setCache(diskCache);
  }

  delete oldQuarantine;
  quarantine.release();
  return prepared;
}

bool clear(QNetworkAccessManager& manager)
{
  auto* diskCache = qobject_cast<QNetworkDiskCache*>(manager.cache());
  if (diskCache == nullptr) {
    return false;
  }

  const QString actual = QDir::cleanPath(diskCache->cacheDirectory());
  const QString expected = diskCache->property(ExpectedPathProperty).toString();
  const QString quarantine =
      diskCache->property(QuarantinePathProperty).toString();
  if (expected.isEmpty() || quarantine.isEmpty()) {
    return false;
  }
  if (actual != expected || !validateOwnedDirectory(actual)) {
    // Keep the cache object alive for replies that may still reference it, but
    // redirect subsequent publication away from the substituted directory.
    diskCache->setProperty(ExpectedPathProperty, {});
    diskCache->setCacheDirectory(quarantine);
    return false;
  }

  diskCache->clear();
  return true;
}
}
