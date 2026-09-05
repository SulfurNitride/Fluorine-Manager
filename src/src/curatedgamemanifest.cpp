#include "curatedgamemanifest.h"

#include "vdfparser.h"

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>

#include <algorithm>
#include <limits>
#include <memory>
#include <vector>
#include <zlib.h>

namespace
{
constexpr quint32 SteamPayloadMagic = 0x71f617d0;
constexpr quint32 EpicManifestMagic = 0x44bec00c;

QString canonicalPath(const QString& path)
{
  const QFileInfo info(path);
  const QString canonical = info.canonicalFilePath();
  return QDir::cleanPath(canonical.isEmpty() ? info.absoluteFilePath() : canonical);
}

QString safeRelativePath(QString path)
{
  path.replace('\\', '/');
  path = QDir::cleanPath(path);
  if (path.isEmpty() || path == "." || QDir::isAbsolutePath(path)
      || path == ".." || path.startsWith("../") || path.contains(':'))
    return {};
  return path;
}

QString pathKey(const QString& path)
{
  return safeRelativePath(path).toCaseFolded();
}

QSet<QString> curatedStockCopyExclusions(const CuratedGameManifest& manifest)
{
  const bool isNewVegas = std::any_of(
      manifest.files.cbegin(), manifest.files.cend(),
      [](const CuratedManifestFile& file) {
        return pathKey(file.path) == QStringLiteral("falloutnv.exe");
      });
  if (!isNewVegas) return {};

  // InstallScript.vdf is Steam installation metadata. The guide's BSA patching
  // flow manages the Vorbis replacements itself, so copying or validating the
  // source installation's copies would reject an otherwise usable install.
  return {QStringLiteral("installscript.vdf"),
          QStringLiteral("libvorbis.dll"),
          QStringLiteral("libvorbisfile.dll")};
}

quint32 little32(const char* data)
{
  const auto* p = reinterpret_cast<const unsigned char*>(data);
  return quint32(p[0]) | (quint32(p[1]) << 8) | (quint32(p[2]) << 16)
         | (quint32(p[3]) << 24);
}

quint64 little64(const char* data)
{
  return quint64(little32(data)) | (quint64(little32(data + 4)) << 32);
}

class ByteReader
{
public:
  explicit ByteReader(const QByteArray& data) : m_data(data) {}

  qsizetype position() const { return m_position; }
  qsizetype size() const { return m_data.size(); }
  bool good() const { return m_good; }

  bool seek(qsizetype position)
  {
    if (position < 0 || position > m_data.size()) return m_good = false;
    m_position = position;
    return true;
  }

  bool skip(qsizetype amount) { return seek(m_position + amount); }

  quint8 u8()
  {
    if (!require(1)) return 0;
    return static_cast<quint8>(m_data[m_position++]);
  }

  quint32 u32()
  {
    if (!require(4)) return 0;
    const quint32 value = little32(m_data.constData() + m_position);
    m_position += 4;
    return value;
  }

  qint32 i32() { return static_cast<qint32>(u32()); }

  quint64 u64()
  {
    if (!require(8)) return 0;
    const quint64 value = little64(m_data.constData() + m_position);
    m_position += 8;
    return value;
  }

  QByteArray bytes(qsizetype count)
  {
    if (!require(count)) return {};
    const QByteArray result = m_data.mid(m_position, count);
    m_position += count;
    return result;
  }

  QString fstring()
  {
    const qint32 length = i32();
    if (!m_good || length == 0) return {};
    if (length > 0) {
      const QByteArray raw = bytes(length);
      if (!m_good || raw.isEmpty() || raw.back() != '\0') {
        m_good = false;
        return {};
      }
      return QString::fromUtf8(raw.constData(), raw.size() - 1);
    }
    if (length == std::numeric_limits<qint32>::min()) {
      m_good = false;
      return {};
    }
    const qsizetype byteCount = qsizetype(-qint64(length)) * 2;
    const QByteArray raw = bytes(byteCount);
    if (!m_good || raw.size() < 2 || raw.right(2) != QByteArray(2, '\0')) {
      m_good = false;
      return {};
    }
    return QString::fromUtf16(reinterpret_cast<const char16_t*>(raw.constData()),
                              raw.size() / 2 - 1);
  }

private:
  bool require(qsizetype amount)
  {
    if (!m_good || amount < 0 || amount > m_data.size() - m_position) {
      m_good = false;
      return false;
    }
    return true;
  }

  const QByteArray& m_data;
  qsizetype m_position{0};
  bool m_good{true};
};

bool readVarint(const QByteArray& data, qsizetype& position, quint64& value)
{
  value = 0;
  for (int shift = 0; shift < 64 && position < data.size(); shift += 7) {
    const quint8 byte = static_cast<quint8>(data[position++]);
    value |= quint64(byte & 0x7f) << shift;
    if (!(byte & 0x80)) return true;
  }
  return false;
}

bool readProtoBytes(const QByteArray& data, qsizetype& position, QByteArray& value)
{
  quint64 length = 0;
  if (!readVarint(data, position, length)
      || length > quint64(data.size() - position))
    return false;
  value = data.mid(position, static_cast<qsizetype>(length));
  position += static_cast<qsizetype>(length);
  return true;
}

bool skipProtoField(const QByteArray& data, qsizetype& position, int wire)
{
  quint64 ignored = 0;
  QByteArray bytes;
  switch (wire) {
  case 0: return readVarint(data, position, ignored);
  case 1:
    if (data.size() - position < 8) return false;
    position += 8;
    return true;
  case 2: return readProtoBytes(data, position, bytes);
  case 5:
    if (data.size() - position < 4) return false;
    position += 4;
    return true;
  default: return false;
  }
}

void addManifestFile(CuratedGameManifest& manifest,
                     QHash<QString, qsizetype>& indexes,
                     const QString& rawPath,
                     const CuratedManifestFileVariant& variant,
                     bool executable = false)
{
  const QString relative = safeRelativePath(rawPath);
  if (relative.isEmpty()) return;
  const QString key = relative.toCaseFolded();
  auto existing = indexes.constFind(key);
  if (existing == indexes.constEnd()) {
    indexes.insert(key, manifest.files.size());
    manifest.files.push_back({relative, {variant}, executable});
    return;
  }
  auto& file = manifest.files[*existing];
  file.executable = file.executable || executable;
  for (const auto& current : file.variants) {
    if (current.size == variant.size && current.digest == variant.digest
        && current.digestAlgorithm == variant.digestAlgorithm)
      return;
  }
  file.variants.push_back(variant);
}

QByteArray inflateData(const QByteArray& compressed, qint64 expectedSize,
                       QString* error)
{
  z_stream stream{};
  stream.next_in = reinterpret_cast<Bytef*>(
      const_cast<char*>(compressed.constData()));
  stream.avail_in = static_cast<uInt>(compressed.size());
  if (inflateInit(&stream) != Z_OK) {
    if (error) *error = "Cannot initialise zlib";
    return {};
  }
  QByteArray output;
  if (expectedSize > 0 && expectedSize <= std::numeric_limits<int>::max())
    output.reserve(static_cast<int>(expectedSize));
  char buffer[64 * 1024];
  int code = Z_OK;
  while (code == Z_OK) {
    stream.next_out = reinterpret_cast<Bytef*>(buffer);
    stream.avail_out = sizeof(buffer);
    code = inflate(&stream, Z_NO_FLUSH);
    const auto produced = sizeof(buffer) - stream.avail_out;
    if (produced) output.append(buffer, static_cast<qsizetype>(produced));
  }
  inflateEnd(&stream);
  if (code != Z_STREAM_END || (expectedSize >= 0 && output.size() != expectedSize)) {
    if (error) *error = "Compressed store manifest is corrupt";
    return {};
  }
  if (error) error->clear();
  return output;
}

QString storeRepairInstruction(const QString& store)
{
  if (store == "Steam")
    return "Use Steam → Properties → Installed Files → Verify integrity, then retry.";
  if (store == "Epic Games")
    return "Use Heroic/Epic's Verify or Repair command, then retry.";
  if (store == "GOG")
    return "Use Heroic/GOG's Repair command, then retry.";
  return "Repair the game through its store, then retry.";
}

QStringList steamRoots()
{
  const QString home = QDir::homePath();
  QStringList result;
  for (const char* relative : {".local/share/Steam", ".steam/debian-installation",
                               ".steam/steam",
                               ".var/app/com.valvesoftware.Steam/data/Steam",
                               ".var/app/com.valvesoftware.Steam/.local/share/Steam",
                               "snap/steam/common/.local/share/Steam"}) {
    const QString candidate = QDir(home).filePath(relative);
    if (QFileInfo::exists(candidate)) result.push_back(candidate);
  }
  return result;
}

CuratedManifestResolution resolveSteamManifest(const QString& source)
{
  CuratedManifestResolution result;
  const QString sourceCanonical = canonicalPath(source);
  const QDir common = QFileInfo(sourceCanonical).dir();
  const QDir steamapps = QFileInfo(common.absolutePath()).dir();
  if (common.dirName().compare("common", Qt::CaseInsensitive) != 0) {
    result.error = "The selected Steam game is not inside a steamapps/common folder.";
    return result;
  }

  AppManifest app;
  QString appManifestPath;
  for (const auto& name : steamapps.entryList({"appmanifest_*.acf"}, QDir::Files)) {
    QFile file(steamapps.filePath(name));
    if (!file.open(QIODevice::ReadOnly)) continue;
    const AppManifest candidate = AppManifest::fromVdf(QString::fromUtf8(file.readAll()));
    if (canonicalPath(QDir(common.absolutePath()).filePath(candidate.install_dir))
        == sourceCanonical) {
      app = candidate;
      appManifestPath = file.fileName();
      break;
    }
  }
  if (app.app_id.isEmpty()) {
    result.error = "No Steam appmanifest matches the selected game folder.";
    return result;
  }
  if (app.installed_depots.isEmpty()) {
    result.error = QString("Steam's appmanifest for %1 contains no installed depots.")
                       .arg(app.name);
    return result;
  }

  QStringList caches{steamapps.filePath("depotcache"),
                     QFileInfo(steamapps.absolutePath()).dir().filePath("depotcache")};
  for (const auto& root : steamRoots()) caches.push_back(QDir(root).filePath("depotcache"));
  caches.removeDuplicates();

  CuratedGameManifest combined;
  combined.store = "Steam";
  combined.gameId = app.app_id;
  combined.buildId = app.build_id;
  QHash<QString, qsizetype> indexes;
  QStringList missing;
  for (const auto& depot : app.installed_depots) {
    const QString filename = depot.depot_id + '_' + depot.manifest_id + ".manifest";
    QString path;
    for (const auto& cache : caches) {
      const QString candidate = QDir(cache).filePath(filename);
      if (QFileInfo(candidate).isFile()) {
        path = candidate;
        break;
      }
    }
    if (path.isEmpty()) {
      missing.push_back(filename);
      continue;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
      result.error = QString("Cannot read Steam depot manifest %1: %2")
                         .arg(path, file.errorString());
      return result;
    }
    auto parsed = parseSteamDepotManifestData(file.readAll(), depot.depot_id);
    if (!parsed.success) {
      result.error = QString("Cannot read Steam depot manifest %1: %2")
                         .arg(filename, parsed.error);
      return result;
    }
    combined.manifestIds.push_back(depot.depot_id + ':' + depot.manifest_id);
    for (const auto& parsedFile : parsed.manifest.files)
      for (const auto& variant : parsedFile.variants)
        addManifestFile(combined, indexes, parsedFile.path, variant,
                        parsedFile.executable);
  }
  if (!missing.isEmpty()) {
    result.error = QString("Steam is missing %1 content manifest(s), including %2. "
                           "Restart Steam and verify the game so it refreshes its depot cache.")
                       .arg(missing.size()).arg(missing.first());
    return result;
  }
  if (combined.files.isEmpty()) {
    result.error = "Steam's depot manifests did not contain any game files.";
    return result;
  }
  result.success = true;
  result.manifest = std::move(combined);
  return result;
}

QStringList legendaryRoots()
{
  const QString home = QDir::homePath();
  QStringList roots;
  for (const char* relative : {".config/legendary",
                               ".config/heroic/legendaryConfig/legendary",
                               ".var/app/com.heroicgameslauncher.hgl/config/heroic/legendaryConfig/legendary"}) {
    const QString path = QDir(home).filePath(relative);
    if (QFileInfo::exists(path)) roots.push_back(path);
  }
  return roots;
}

QString cleanLegendaryFilename(QString value)
{
  static const QString invalid = "<>:\"/\\|?*";
  for (const auto& character : invalid) value.remove(character);
  return value;
}

CuratedManifestResolution resolveEpicManifest(const QString& source)
{
  const QString expectedPath = canonicalPath(source);
  for (const auto& root : legendaryRoots()) {
    QFile installed(QDir(root).filePath("installed.json"));
    if (!installed.open(QIODevice::ReadOnly)) continue;
    const auto object = QJsonDocument::fromJson(installed.readAll()).object();
    for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
      const auto game = it.value().toObject();
      if (canonicalPath(game.value("install_path").toString()) != expectedPath) continue;
      const QString appName = game.value("app_name").toString(it.key());
      const QString version = game.value("version").toString();
      const QString platform = game.value("platform").toString("Windows");
      QStringList candidates;
      const QString recorded = game.value("manifest_path").toString();
      if (!recorded.isEmpty()) candidates.push_back(recorded);
      candidates.push_back(QDir(root).filePath(
          "manifests/" + cleanLegendaryFilename(appName + '_' + platform + '_'
                                                 + version) + ".manifest"));
      candidates.push_back(QDir(root).filePath(
          "manifests/" + cleanLegendaryFilename(appName + '_' + version)
          + ".manifest"));
      QString manifestPath;
      for (const auto& candidate : candidates) {
        if (QFileInfo(candidate).isFile()) {
          manifestPath = candidate;
          break;
        }
      }
      if (manifestPath.isEmpty()) {
        QDir manifests(QDir(root).filePath("manifests"));
        const auto matches = manifests.entryList(
            {cleanLegendaryFilename(appName + "_*") + ".manifest"}, QDir::Files,
            QDir::Time);
        if (!matches.isEmpty()) manifestPath = manifests.filePath(matches.first());
      }
      if (manifestPath.isEmpty()) {
        return {false, {}, QString("Legendary's installed manifest for %1 is missing. "
                                   "Verify the game in Heroic, then retry.").arg(appName)};
      }
      QFile manifestFile(manifestPath);
      if (!manifestFile.open(QIODevice::ReadOnly))
        return {false, {}, manifestFile.errorString()};
      auto parsed = parseEpicBuildPatchManifestData(manifestFile.readAll(), appName,
                                                     version);
      if (parsed.success)
        parsed.manifest.manifestIds = {QFileInfo(manifestPath).fileName()};
      return parsed;
    }
  }
  return {false, {}, "No Legendary/Heroic installation record matches the selected game folder."};
}

QStringList heroicRoots()
{
  const QString home = QDir::homePath();
  QStringList roots;
  for (const char* relative : {".config/heroic",
                               ".var/app/com.heroicgameslauncher.hgl/config/heroic"}) {
    const QString path = QDir(home).filePath(relative);
    if (QFileInfo::exists(path)) roots.push_back(path);
  }
  return roots;
}

QByteArray httpGet(const QUrl& url, QString* error)
{
  QNetworkAccessManager manager;
  QNetworkRequest request(url);
  request.setHeader(QNetworkRequest::UserAgentHeader,
                    QStringLiteral("Fluorine curated stock verifier"));
  request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                       QNetworkRequest::NoLessSafeRedirectPolicy);
  QNetworkReply* reply = manager.get(request);
  QEventLoop loop;
  QTimer timer;
  timer.setSingleShot(true);
  QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
  QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
  timer.start(30000);
  loop.exec();
  if (!reply->isFinished()) {
    reply->abort();
    if (error) *error = QString("Timed out retrieving %1").arg(url.host());
    reply->deleteLater();
    return {};
  }
  if (reply->error() != QNetworkReply::NoError) {
    if (error) *error = reply->errorString();
    reply->deleteLater();
    return {};
  }
  const QByteArray data = reply->readAll();
  reply->deleteLater();
  if (error) error->clear();
  return data;
}

bool languageMatches(const QJsonArray& languages, const QString& selected,
                     const QString& neutral)
{
  if (languages.isEmpty()) return true;
  for (const auto& value : languages) {
    const QString language = value.toString();
    if (language == neutral || language.compare(selected, Qt::CaseInsensitive) == 0)
      return true;
  }
  return false;
}

QSet<QString> gogProducts(const QJsonObject& stored, const QString& baseId)
{
  QSet<QString> products{baseId};
  for (const auto& value : stored.value("HGLdlcs").toArray()) {
    if (value.isObject()) products.insert(value.toObject().value("id").toString());
    else products.insert(value.toVariant().toString());
  }
  return products;
}

CuratedManifestResolution resolveGogManifest(const QString& source)
{
  const QString expectedPath = canonicalPath(source);
  QString gameId;
  QString buildId;
  for (const auto& heroicRoot : heroicRoots()) {
    QFile installed(QDir(heroicRoot).filePath("gog_store/installed.json"));
    if (!installed.open(QIODevice::ReadOnly)) continue;
    const auto document = QJsonDocument::fromJson(installed.readAll());
    const QJsonArray entries = document.isArray()
                                   ? document.array()
                                   : document.object().value("installed").toArray();
    for (const auto& value : entries) {
      const auto game = value.toObject();
      if (canonicalPath(game.value("install_path").toString()) != expectedPath) continue;
      gameId = game.value("appName").toVariant().toString();
      buildId = game.value("buildId").toVariant().toString();
      break;
    }
    if (!gameId.isEmpty()) break;
  }
  if (gameId.isEmpty())
    return {false, {}, "No Heroic GOG installation record matches the selected game folder."};

  QStringList manifestCandidates;
  const QString home = QDir::homePath();
  for (const char* relative : {".config/heroic_gogdl/manifests",
                               ".config/heroic/gogdlConfig/heroic_gogdl/manifests",
                               ".var/app/com.heroicgameslauncher.hgl/config/heroic_gogdl/manifests",
                               ".var/app/com.heroicgameslauncher.hgl/config/heroic/gogdlConfig/heroic_gogdl/manifests"})
    manifestCandidates.push_back(QDir(QDir(home).filePath(relative)).filePath(gameId));
  QString storedPath;
  for (const auto& candidate : manifestCandidates) {
    if (QFileInfo(candidate).isFile()) {
      storedPath = candidate;
      break;
    }
  }
  if (storedPath.isEmpty())
    return {false, {}, QString("Heroic's installed GOG depot manifest for %1 is missing. "
                               "Repair the game in Heroic, then retry.").arg(gameId)};
  QFile storedFile(storedPath);
  if (!storedFile.open(QIODevice::ReadOnly)) return {false, {}, storedFile.errorString()};
  const QJsonObject stored = QJsonDocument::fromJson(storedFile.readAll()).object();
  if (stored.isEmpty()) return {false, {}, "The installed GOG manifest is invalid."};

  CuratedGameManifest manifest;
  manifest.store = "GOG";
  manifest.gameId = gameId;
  manifest.buildId = buildId;
  QHash<QString, qsizetype> indexes;
  QString networkError;

  if (stored.value("version").toInt() == 1 || stored.contains("product")) {
    const auto product = stored.value("product").toObject();
    const QString baseId = product.value("rootGameID").toVariant().toString();
    const auto products = gogProducts(stored, baseId);
    const QString language = stored.value("HGLInstallLanguage").toString();
    const QString timestamp = product.value("timestamp").toVariant().toString();
    for (const auto& depotValue : product.value("depots").toArray()) {
      const auto depot = depotValue.toObject();
      if (depot.contains("redist")
          || !languageMatches(depot.value("languages").toArray(), language, "Neutral"))
        continue;
      QString productId;
      for (const auto& id : depot.value("gameIDs").toArray()) {
        const QString candidate = id.toVariant().toString();
        if (products.contains(candidate)) {
          productId = candidate;
          break;
        }
      }
      if (productId.isEmpty()) continue;
      const QString depotId = depot.value("manifest").toString();
      const QUrl url(QString("https://gog-cdn-fastly.gog.com/content-system/v1/manifests/%1/windows/%2/%3")
                         .arg(productId, timestamp, depotId));
      const QByteArray data = httpGet(url, &networkError);
      if (!networkError.isEmpty()) return {false, {}, networkError};
      const auto depotRoot = QJsonDocument::fromJson(data).object()
                                 .value("depot").toObject();
      for (const auto& fileValue : depotRoot.value("files").toArray()) {
        const auto file = fileValue.toObject();
        if (file.contains("directory")) continue;
        CuratedManifestFileVariant variant;
        variant.size = file.value("size").toVariant().toLongLong();
        variant.digest = QByteArray::fromHex(file.value("hash").toString().toLatin1());
        variant.digestAlgorithm = "md5";
        addManifestFile(manifest, indexes, file.value("path").toString(), variant,
                        file.value("executable").toBool());
      }
      manifest.manifestIds.push_back(depotId);
    }
  } else {
    const QString baseId = stored.value("baseProductId").toVariant().toString();
    const auto products = gogProducts(stored, baseId);
    const QString language = stored.value("HGLInstallLanguage").toString();
    for (const auto& depotValue : stored.value("depots").toArray()) {
      const auto depot = depotValue.toObject();
      const QString productId = depot.value("productId").toVariant().toString();
      if (!products.contains(productId)
          || !languageMatches(depot.value("languages").toArray(), language, "*"))
        continue;
      const QString depotId = depot.value("manifest").toString();
      QString galaxyPath = depotId;
      if (!galaxyPath.contains('/') && galaxyPath.size() >= 4)
        galaxyPath = galaxyPath.left(2) + '/' + galaxyPath.mid(2, 2) + '/' + galaxyPath;
      QByteArray data = httpGet(QUrl("https://gog-cdn-fastly.gog.com/content-system/v2/meta/"
                                    + galaxyPath), &networkError);
      if (!networkError.isEmpty()) return {false, {}, networkError};
      if (!data.trimmed().startsWith('{')) {
        data = inflateData(data, -1, &networkError);
        if (!networkError.isEmpty()) return {false, {}, networkError};
      }
      const auto items = QJsonDocument::fromJson(data).object()
                             .value("depot").toObject().value("items").toArray();
      for (const auto& itemValue : items) {
        const auto item = itemValue.toObject();
        if (item.value("type").toString() != "DepotFile") continue;
        CuratedManifestFileVariant variant;
        for (const auto& chunkValue : item.value("chunks").toArray()) {
          const auto chunk = chunkValue.toObject();
          const qint64 size = chunk.value("size").toVariant().toLongLong();
          variant.size += size;
          variant.chunks.push_back(
              {size, QByteArray::fromHex(chunk.value("md5").toString().toLatin1())});
        }
        const QString md5 = item.value("md5").toString();
        const QString sha256 = item.value("sha256").toString();
        if (!md5.isEmpty()) {
          variant.digestAlgorithm = "md5";
          variant.digest = QByteArray::fromHex(md5.toLatin1());
        } else if (!sha256.isEmpty()) {
          variant.digestAlgorithm = "sha256";
          variant.digest = QByteArray::fromHex(sha256.toLatin1());
        } else {
          variant.digestAlgorithm = "chunk-md5";
        }
        QString path = item.value("path").toString();
        if (item.value("flags").toArray().contains("support"))
          path = productId + '/' + path;
        addManifestFile(manifest, indexes, path, variant,
                        item.value("flags").toArray().contains("executable"));
      }
      manifest.manifestIds.push_back(depotId);
    }
  }
  if (manifest.files.isEmpty())
    return {false, {}, "The installed GOG depots did not contain any game files."};
  return {true, std::move(manifest), {}};
}

bool copyAndVerifyFile(const QString& sourcePath, const QString& outputPath,
                       const QVector<CuratedManifestFileVariant>& variants,
                       QString* error)
{
  QFile input(sourcePath);
  QSaveFile output(outputPath);
  if (!input.open(QIODevice::ReadOnly)) {
    if (error) *error = input.errorString();
    return false;
  }
  if (!output.open(QIODevice::WriteOnly)) {
    if (error) *error = output.errorString();
    return false;
  }

  bool needsSha1 = false;
  bool needsMd5 = false;
  bool needsSha256 = false;
  struct ChunkRuntime {
    const CuratedManifestFileVariant* variant{};
    qsizetype index{0};
    qint64 remaining{0};
    bool failed{false};
    std::unique_ptr<QCryptographicHash> hash;
  };
  std::vector<ChunkRuntime> chunkRuntimes;
  for (const auto& variant : variants) {
    needsSha1 = needsSha1 || variant.digestAlgorithm == "sha1";
    needsMd5 = needsMd5 || variant.digestAlgorithm == "md5";
    needsSha256 = needsSha256 || variant.digestAlgorithm == "sha256";
    if (variant.digestAlgorithm == "chunk-md5" && !variant.chunks.isEmpty())
      chunkRuntimes.push_back(
          {&variant, 0, variant.chunks.front().size, false,
           std::make_unique<QCryptographicHash>(QCryptographicHash::Md5)});
  }
  QCryptographicHash sha1(QCryptographicHash::Sha1);
  QCryptographicHash md5(QCryptographicHash::Md5);
  QCryptographicHash sha256(QCryptographicHash::Sha256);
  char buffer[1024 * 1024];
  while (!input.atEnd()) {
    const qint64 count = input.read(buffer, sizeof(buffer));
    if (count < 0 || output.write(buffer, count) != count) {
      if (error) *error = count < 0 ? input.errorString() : output.errorString();
      output.cancelWriting();
      return false;
    }
    if (needsSha1) sha1.addData(QByteArrayView(buffer, count));
    if (needsMd5) md5.addData(QByteArrayView(buffer, count));
    if (needsSha256) sha256.addData(QByteArrayView(buffer, count));
    for (auto& runtime : chunkRuntimes) {
      qint64 offset = 0;
      while (!runtime.failed && offset < count
             && runtime.index < runtime.variant->chunks.size()) {
        const qint64 amount = qMin(runtime.remaining, count - offset);
        runtime.hash->addData(QByteArrayView(buffer + offset, amount));
        runtime.remaining -= amount;
        offset += amount;
        if (runtime.remaining != 0) continue;
        if (runtime.hash->result()
            != runtime.variant->chunks[runtime.index].md5) {
          runtime.failed = true;
          break;
        }
        ++runtime.index;
        runtime.hash->reset();
        if (runtime.index < runtime.variant->chunks.size())
          runtime.remaining = runtime.variant->chunks[runtime.index].size;
      }
      if (!runtime.failed && offset < count) runtime.failed = true;
    }
  }

  bool matched = false;
  for (const auto& variant : variants) {
    if (variant.digestAlgorithm == "sha1") matched = sha1.result() == variant.digest;
    else if (variant.digestAlgorithm == "md5") matched = md5.result() == variant.digest;
    else if (variant.digestAlgorithm == "sha256")
      matched = sha256.result() == variant.digest;
    else if (variant.digestAlgorithm == "chunk-md5") {
      for (const auto& runtime : chunkRuntimes) {
        if (runtime.variant == &variant && !runtime.failed
            && runtime.index == variant.chunks.size()) {
          matched = true;
          break;
        }
      }
    }
    if (matched) break;
  }
  if (!matched) {
    output.cancelWriting();
    if (error) error->clear();
    return false;
  }
  if (!output.commit()) {
    if (error) *error = output.errorString();
    return false;
  }
  if (error) error->clear();
  return true;
}
}

QJsonObject CuratedGameManifest::provenance() const
{
  QJsonArray ids;
  for (const auto& id : manifestIds) ids.push_back(id);
  return {{"store", store}, {"gameId", gameId}, {"buildId", buildId},
          {"manifestIds", ids}, {"fileCount", files.size()}};
}

CuratedManifestResolution parseSteamDepotManifestData(const QByteArray& data,
                                                       const QString& depotId)
{
  if (data.size() < 8 || little32(data.constData()) != SteamPayloadMagic)
    return {false, {}, "Invalid Steam content-manifest header"};
  const quint32 payloadSize = little32(data.constData() + 4);
  if (payloadSize > quint32(data.size() - 8))
    return {false, {}, "Truncated Steam content-manifest payload"};
  const QByteArray payload = data.mid(8, payloadSize);
  CuratedGameManifest manifest;
  manifest.store = "Steam";
  manifest.gameId = depotId;
  QHash<QString, qsizetype> indexes;
  qsizetype position = 0;
  while (position < payload.size()) {
    quint64 tag = 0;
    if (!readVarint(payload, position, tag))
      return {false, {}, "Invalid Steam protobuf field"};
    const int field = int(tag >> 3);
    const int wire = int(tag & 7);
    if (field != 1 || wire != 2) {
      if (!skipProtoField(payload, position, wire))
        return {false, {}, "Unsupported Steam protobuf field"};
      continue;
    }
    QByteArray mapping;
    if (!readProtoBytes(payload, position, mapping))
      return {false, {}, "Truncated Steam file mapping"};
    QString filename;
    QString linkTarget;
    qint64 size = 0;
    quint64 flags = 0;
    QByteArray sha1;
    qsizetype mp = 0;
    while (mp < mapping.size()) {
      quint64 mappingTag = 0;
      if (!readVarint(mapping, mp, mappingTag))
        return {false, {}, "Invalid Steam file mapping"};
      const int mappingField = int(mappingTag >> 3);
      const int mappingWire = int(mappingTag & 7);
      QByteArray bytes;
      quint64 number = 0;
      if (mappingField == 1 && mappingWire == 2) {
        if (!readProtoBytes(mapping, mp, bytes)) return {false, {}, "Invalid Steam filename"};
        filename = QString::fromUtf8(bytes);
      } else if ((mappingField == 2 || mappingField == 3) && mappingWire == 0) {
        if (!readVarint(mapping, mp, number)) return {false, {}, "Invalid Steam file value"};
        if (mappingField == 2) size = static_cast<qint64>(number);
        else flags = number;
      } else if (mappingField == 5 && mappingWire == 2) {
        if (!readProtoBytes(mapping, mp, sha1)) return {false, {}, "Invalid Steam file hash"};
      } else if (mappingField == 7 && mappingWire == 2) {
        if (!readProtoBytes(mapping, mp, bytes)) return {false, {}, "Invalid Steam link"};
        linkTarget = QString::fromUtf8(bytes);
      } else if (!skipProtoField(mapping, mp, mappingWire)) {
        return {false, {}, "Unsupported Steam file mapping"};
      }
    }
    if ((flags & 64) || !linkTarget.isEmpty()) continue;
    if (filename.isEmpty() || sha1.size() != 20)
      return {false, {}, "Steam manifest contains an incomplete file record"};
    addManifestFile(manifest, indexes, filename,
                    {size, sha1, "sha1", {}}, (flags & 32) != 0);
  }
  if (manifest.files.isEmpty()) return {false, {}, "Steam manifest contains no files"};
  return {true, std::move(manifest), {}};
}

CuratedManifestResolution parseEpicBuildPatchManifestData(const QByteArray& input,
                                                           const QString& appName,
                                                           const QString& version)
{
  CuratedGameManifest manifest;
  manifest.store = "Epic Games";
  manifest.gameId = appName;
  manifest.buildId = version;
  QHash<QString, qsizetype> indexes;

  if (input.trimmed().startsWith('{')) {
    const auto root = QJsonDocument::fromJson(input).object();
    manifest.buildId = root.value("BuildVersionString").toString(version);
    auto blobNumber = [](const QString& blob) {
      quint64 number = 0;
      int shift = 0;
      for (int i = 0; i + 2 < blob.size() && shift < 64; i += 3, shift += 8)
        number |= quint64(blob.mid(i, 3).toUInt()) << shift;
      return number;
    };
    auto blobBytes = [](const QString& blob) {
      QByteArray bytes;
      for (int i = 0; i + 2 < blob.size(); i += 3)
        bytes.append(static_cast<char>(blob.mid(i, 3).toUInt()));
      return bytes;
    };
    for (const auto& value : root.value("FileManifestList").toArray()) {
      const auto file = value.toObject();
      qint64 size = 0;
      for (const auto& part : file.value("FileChunkParts").toArray())
        size += static_cast<qint64>(blobNumber(part.toObject().value("Size").toString()));
      const QByteArray digest = blobBytes(file.value("FileHash").toString());
      if (digest.size() != 20)
        return {false, {}, "Epic JSON manifest contains an invalid file hash"};
      addManifestFile(manifest, indexes, file.value("Filename").toString(),
                      {size, digest, "sha1", {}},
                      file.value("bIsUnixExecutable").toBool());
    }
    if (manifest.files.isEmpty()) return {false, {}, "Epic manifest contains no files"};
    return {true, std::move(manifest), {}};
  }

  if (input.size() < 41 || little32(input.constData()) != EpicManifestMagic)
    return {false, {}, "Invalid Epic BuildPatch manifest header"};
  const quint32 headerSize = little32(input.constData() + 4);
  const quint32 uncompressedSize = little32(input.constData() + 8);
  const QByteArray bodySha1 = input.mid(16, 20);
  const quint8 storedAs = static_cast<quint8>(input[36]);
  if (storedAs & 2)
    return {false, {}, "The local Epic manifest is encrypted; verify it through Heroic and retry"};
  if (headerSize > quint32(input.size())) return {false, {}, "Truncated Epic manifest"};
  QString inflateError;
  QByteArray body = input.mid(headerSize);
  if (storedAs & 1) body = inflateData(body, uncompressedSize, &inflateError);
  if (!inflateError.isEmpty()) return {false, {}, inflateError};
  if (QCryptographicHash::hash(body, QCryptographicHash::Sha1) != bodySha1)
    return {false, {}, "Epic manifest payload hash does not match"};

  ByteReader reader(body);
  const qsizetype metaStart = reader.position();
  const quint32 metaSize = reader.u32();
  const quint8 metaVersion = reader.u8();
  Q_UNUSED(metaVersion);
  const quint32 featureLevel = reader.u32();
  reader.u8();
  reader.u32();
  const QString embeddedAppName = reader.fstring();
  const QString embeddedVersion = reader.fstring();
  if (!embeddedAppName.isEmpty()) manifest.gameId = embeddedAppName;
  if (!embeddedVersion.isEmpty()) manifest.buildId = embeddedVersion;
  if (!reader.good() || metaSize < 9 || metaStart + metaSize > body.size())
    return {false, {}, "Epic manifest metadata is corrupt"};
  reader.seek(metaStart + metaSize);

  const qsizetype chunkStart = reader.position();
  const quint32 chunkSize = reader.u32();
  if (!reader.good() || chunkSize < 9 || chunkStart + chunkSize > body.size())
    return {false, {}, "Epic chunk list is corrupt"};
  reader.seek(chunkStart + chunkSize);

  const qsizetype filesStart = reader.position();
  const quint32 filesSize = reader.u32();
  const quint8 filesVersion = reader.u8();
  const quint32 count = reader.u32();
  if (!reader.good() || filesSize < 9 || filesStart + filesSize > body.size()
      || count > 1000000)
    return {false, {}, "Epic file list is corrupt"};
  QVector<QString> names;
  QVector<QString> links;
  QVector<QByteArray> hashes;
  QVector<bool> executable;
  QVector<qint64> sizes(count, 0);
  names.reserve(count);
  links.reserve(count);
  hashes.reserve(count);
  executable.reserve(count);
  for (quint32 i = 0; i < count; ++i) names.push_back(reader.fstring());
  for (quint32 i = 0; i < count; ++i) links.push_back(reader.fstring());
  for (quint32 i = 0; i < count; ++i) hashes.push_back(reader.bytes(20));
  for (quint32 i = 0; i < count; ++i) executable.push_back((reader.u8() & 4) != 0);
  for (quint32 i = 0; i < count; ++i) {
    const quint32 tags = reader.u32();
    if (tags > 10000) return {false, {}, "Epic install-tag list is corrupt"};
    for (quint32 tag = 0; tag < tags; ++tag) reader.fstring();
  }
  for (quint32 i = 0; i < count; ++i) {
    const quint32 parts = reader.u32();
    if (parts > 1000000) return {false, {}, "Epic chunk-part list is corrupt"};
    for (quint32 part = 0; part < parts; ++part) {
      const qsizetype partStart = reader.position();
      const quint32 partSize = reader.u32();
      reader.skip(16);
      reader.u32();
      sizes[i] += reader.u32();
      if (!reader.good() || partSize < 28 || partStart + partSize > body.size())
        return {false, {}, "Epic chunk part is corrupt"};
      reader.seek(partStart + partSize);
    }
  }
  if (filesVersion >= 1) {
    for (quint32 i = 0; i < count; ++i)
      if (reader.u32()) reader.skip(16);
    for (quint32 i = 0; i < count; ++i) reader.fstring();
  }
  if (filesVersion >= 2)
    for (quint32 i = 0; i < count; ++i) reader.skip(32);
  if (!reader.good() || reader.position() > filesStart + filesSize)
    return {false, {}, "Epic file list is truncated"};
  Q_UNUSED(featureLevel);
  for (quint32 i = 0; i < count; ++i) {
    if (!links[i].isEmpty()) continue;
    addManifestFile(manifest, indexes, names[i],
                    {sizes[i], hashes[i], "sha1", {}}, executable[i]);
  }
  if (manifest.files.isEmpty()) return {false, {}, "Epic manifest contains no files"};
  return {true, std::move(manifest), {}};
}

CuratedManifestResolution resolveCuratedGameManifest(const QString& source,
                                                      const QString& requestedStore)
{
  if (requestedStore == "Steam") return resolveSteamManifest(source);
  if (requestedStore == "Epic Games") return resolveEpicManifest(source);
  if (requestedStore == "GOG") return resolveGogManifest(source);
  return {false, {}, QString("Store '%1' does not provide a supported clean-file manifest.")
                         .arg(requestedStore)};
}

CuratedVerifiedCopyResult copyCuratedGameFromManifest(
    const QString& source, const QString& destination,
    const CuratedGameManifest& manifest,
    bool excludeGuideManagedNewVegasFiles)
{
  CuratedVerifiedCopyResult result;
  result.provenance = manifest.provenance();
  if (!manifest.isValid()) {
    result.error = "The store content manifest is empty or invalid.";
    return result;
  }
  const QDir sourceRoot(source);
  if (!sourceRoot.exists()) {
    result.error = QString("Game source folder is missing: %1").arg(source);
    return result;
  }
  if (!QDir().mkpath(destination)) {
    result.error = QString("Cannot create stock game folder: %1").arg(destination);
    return result;
  }

  QStringList problems;
  QSet<QString> expected;
  const QSet<QString> exclusions = excludeGuideManagedNewVegasFiles
                                       ? curatedStockCopyExclusions(manifest)
                                       : QSet<QString>{};
  for (const auto& file : manifest.files) {
    const QString relative = safeRelativePath(file.path);
    if (relative.isEmpty()) {
      problems.push_back(QString("Unsafe manifest path: %1").arg(file.path));
      continue;
    }
    const QString key = relative.toCaseFolded();
    expected.insert(key);
    if (exclusions.contains(key)) continue;
    const QString sourcePath = sourceRoot.filePath(relative);
    const QFileInfo sourceInfo(sourcePath);
    if (!sourceInfo.exists()) {
      problems.push_back(QString("Missing: %1").arg(relative));
      continue;
    }
    if (!sourceInfo.isFile() || sourceInfo.isSymLink()) {
      problems.push_back(QString("Not an original regular file: %1").arg(relative));
      continue;
    }
    const QString outputPath = QDir(destination).filePath(relative);
    if (!QDir().mkpath(QFileInfo(outputPath).absolutePath())) {
      problems.push_back(QString("Cannot create destination for: %1").arg(relative));
      continue;
    }
    QVector<CuratedManifestFileVariant> sizeVariants;
    for (const auto& variant : file.variants)
      if (variant.size == sourceInfo.size()) sizeVariants.push_back(variant);
    if (sizeVariants.isEmpty()) {
      problems.push_back(QString("Modified or from a different build: %1").arg(relative));
      continue;
    }
    QString copyError;
    if (!copyAndVerifyFile(sourcePath, outputPath, sizeVariants, &copyError)) {
      problems.push_back(copyError.isEmpty()
                             ? QString("Modified or from a different build: %1").arg(relative)
                             : QString("Cannot copy %1: %2").arg(relative, copyError));
      continue;
    }
    QFile::setPermissions(outputPath, sourceInfo.permissions());
  }

  QDirIterator iterator(source, QDir::Files | QDir::NoDotAndDotDot,
                        QDirIterator::Subdirectories);
  while (iterator.hasNext()) {
    const QString relative = safeRelativePath(sourceRoot.relativeFilePath(iterator.next()));
    if (!relative.isEmpty() && !expected.contains(relative.toCaseFolded()))
      result.unexpectedFiles.push_back(relative);
  }
  std::sort(result.unexpectedFiles.begin(), result.unexpectedFiles.end(),
            [](const QString& left, const QString& right) {
              return left.compare(right, Qt::CaseInsensitive) < 0;
            });

  if (!problems.isEmpty()) {
    const int omitted = qMax(0, problems.size() - 10);
    problems = problems.mid(0, 10);
    result.error = QString("The selected %1 installation does not match its original "
                           "store manifest:\n\n• %2")
                       .arg(manifest.store, problems.join("\n• "));
    if (omitted) result.error += QString("\n• …and %1 more problem(s)").arg(omitted);
    result.error += "\n\n" + storeRepairInstruction(manifest.store);
    return result;
  }
  result.success = true;
  return result;
}
