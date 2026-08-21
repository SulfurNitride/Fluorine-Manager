#include "vfsindex.h"

#include <QByteArray>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

#include <blake3.h>
#include <sqlite3.h>
#include <utf8proc.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <iomanip>
#include <memory>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <system_error>
#include <unistd.h>

#ifndef _WIN32
#include <sys/file.h>
#endif

namespace
{
namespace fs = std::filesystem;

constexpr std::string_view kArchiveMembershipAlgorithm =
    "fnv1a64-double-bloom-v1";
constexpr std::size_t kMaximumArchiveProofBytes = 256 * 1024 * 1024;

struct DbCloser
{
  void operator()(sqlite3* db) const
  {
    if (db != nullptr) sqlite3_close(db);
  }
};
using DbPtr = std::unique_ptr<sqlite3, DbCloser>;

struct StmtCloser
{
  void operator()(sqlite3_stmt* statement) const
  {
    if (statement != nullptr) sqlite3_finalize(statement);
  }
};
using StmtPtr = std::unique_ptr<sqlite3_stmt, StmtCloser>;

[[noreturn]] void throwDb(sqlite3* db, const std::string& operation)
{
  throw std::runtime_error(
      operation + ": " + (db != nullptr ? sqlite3_errmsg(db) : "SQLite error"));
}

void execSql(sqlite3* db, const char* sql)
{
  char* rawError = nullptr;
  if (sqlite3_exec(db, sql, nullptr, nullptr, &rawError) != SQLITE_OK) {
    const std::string error =
        rawError != nullptr ? rawError : sqlite3_errmsg(db);
    sqlite3_free(rawError);
    throw std::runtime_error(error);
  }
}

StmtPtr prepare(sqlite3* db, const char* sql)
{
  sqlite3_stmt* raw = nullptr;
  if (sqlite3_prepare_v2(db, sql, -1, &raw, nullptr) != SQLITE_OK) {
    throwDb(db, "Preparing VFS index statement");
  }
  return StmtPtr(raw);
}

void bindText(sqlite3* db, sqlite3_stmt* statement, int index,
              const std::string& value)
{
  if (sqlite3_bind_text(statement, index, value.data(),
                        static_cast<int>(value.size()),
                        SQLITE_TRANSIENT) != SQLITE_OK) {
    throwDb(db, "Binding VFS index text");
  }
}

void bindDigest(sqlite3* db, sqlite3_stmt* statement, int index,
                const VfsDigest& digest)
{
  if (sqlite3_bind_blob(statement, index, digest.data(), digest.size(),
                        SQLITE_TRANSIENT) != SQLITE_OK) {
    throwDb(db, "Binding VFS index digest");
  }
}

std::string sqliteText(sqlite3_stmt* statement, int column)
{
  const auto* value =
      reinterpret_cast<const char*>(sqlite3_column_text(statement, column));
  return value != nullptr ? std::string(value) : std::string{};
}

bool sqliteDigest(sqlite3_stmt* statement, int column, VfsDigest& digest)
{
  const void* value = sqlite3_column_blob(statement, column);
  const int size = sqlite3_column_bytes(statement, column);
  if (value == nullptr || size != static_cast<int>(digest.size())) return false;
  std::memcpy(digest.data(), value, digest.size());
  return true;
}

std::string digestHex(const VfsDigest& digest)
{
  constexpr char kHex[] = "0123456789abcdef";
  std::string result(digest.size() * 2, '0');
  for (std::size_t i = 0; i < digest.size(); ++i) {
    result[i * 2] = kHex[digest[i] >> 4];
    result[i * 2 + 1] = kHex[digest[i] & 0x0f];
  }
  return result;
}

std::optional<VfsDigest> parseDigest(const QString& text)
{
  const QByteArray bytes = text.toLatin1();
  if (bytes.size() != 64) return std::nullopt;
  VfsDigest digest{};
  for (int i = 0; i < 32; ++i) {
    const auto nibble = [](char value) -> int {
      if (value >= '0' && value <= '9') return value - '0';
      if (value >= 'a' && value <= 'f') return value - 'a' + 10;
      if (value >= 'A' && value <= 'F') return value - 'A' + 10;
      return -1;
    };
    const int high = nibble(bytes[i * 2]);
    const int low = nibble(bytes[i * 2 + 1]);
    if (high < 0 || low < 0) return std::nullopt;
    digest[static_cast<std::size_t>(i)] =
        static_cast<unsigned char>((high << 4) | low);
  }
  return digest;
}

std::string newGeneration()
{
  std::array<unsigned char, 16> bytes{};
  std::random_device random;
  for (auto& byte : bytes) byte = static_cast<unsigned char>(random());
  bytes[6] = static_cast<unsigned char>((bytes[6] & 0x0f) | 0x40);
  bytes[8] = static_cast<unsigned char>((bytes[8] & 0x3f) | 0x80);

  std::ostringstream output;
  output << std::hex << std::setfill('0');
  for (std::size_t i = 0; i < bytes.size(); ++i) {
    if (i == 4 || i == 6 || i == 8 || i == 10) output << '-';
    output << std::setw(2) << static_cast<unsigned int>(bytes[i]);
  }
  return output.str();
}

bool validGeneration(const std::string& generation)
{
  if (generation.size() != 36) return false;
  for (std::size_t i = 0; i < generation.size(); ++i) {
    if (i == 8 || i == 13 || i == 18 || i == 23) {
      if (generation[i] != '-') return false;
    } else if (!std::isxdigit(
                   static_cast<unsigned char>(generation[i]))) {
      return false;
    }
  }
  return true;
}

std::string normalizeIndexPath(const std::string& path)
{
  std::string separators = path;
  std::replace(separators.begin(), separators.end(), '\\', '/');
  while (!separators.empty() && separators.front() == '/') {
    separators.erase(separators.begin());
  }
  std::string collapsed;
  collapsed.reserve(separators.size());
  bool previousSlash = false;
  for (char value : separators) {
    if (value == '/') {
      if (previousSlash) continue;
      previousSlash = true;
    } else {
      previousSlash = false;
    }
    collapsed.push_back(value);
  }

  utf8proc_uint8_t* mapped = nullptr;
  const auto size = utf8proc_map(
      reinterpret_cast<const utf8proc_uint8_t*>(collapsed.data()),
      static_cast<utf8proc_ssize_t>(collapsed.size()), &mapped,
      static_cast<utf8proc_option_t>(
          UTF8PROC_STABLE | UTF8PROC_COMPOSE | UTF8PROC_CASEFOLD));
  if (size < 0 || mapped == nullptr) {
    std::free(mapped);
    throw std::runtime_error(
        std::string("Invalid UTF-8 virtual path: ") +
        utf8proc_errmsg(size));
  }
  std::string normalized(reinterpret_cast<char*>(mapped),
                         static_cast<std::size_t>(size));
  std::free(mapped);
  return normalized;
}

std::string joinVirtual(const std::string& base, const std::string& name)
{
  return base.empty() ? name : base + "/" + name;
}

int64_t timeNs(std::chrono::system_clock::time_point value)
{
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
             value.time_since_epoch())
      .count();
}

bool isReservedVirtualPath(const std::string& normalized)
{
  const std::string locator = normalizeIndexPath(kVfsIndexVirtualLocator);
  const std::string legacy =
      normalizeIndexPath(kLegacyVfsIndexVirtualLocator);
  return normalized == locator || normalized == legacy ||
         normalized == kVfsIndexLocatorName ||
         normalized == kLegacyVfsIndexLocatorName ||
         normalized == ".vfs-indexer/index" ||
         normalized.starts_with(".vfs-indexer/index/") ||
         normalized == ".fluorine/index" ||
         normalized.starts_with(".fluorine/index/");
}

std::vector<VfsIndexResolvedFile> flattenTree(
    const VfsTree& tree, const fs::path& dataDirectory,
    VfsIndexConsumerPathStyle pathStyle)
{
  std::vector<VfsIndexResolvedFile> files;
  files.reserve(tree.file_count);
  const auto visit = [&](const auto& self, const VfsNode& directory,
                         const std::string& parentPath) -> void {
    for (const auto& [key, childPointer] : directory.dir_info.children) {
      if (childPointer == nullptr) continue;
      const auto display = directory.dir_info.display_names.find(key);
      const std::string name =
          display != directory.dir_info.display_names.end()
              ? display->second
              : key;
      const std::string virtualPath = joinVirtual(parentPath, name);
      if (childPointer->is_directory) {
        self(self, *childPointer, virtualPath);
        continue;
      }

      const std::string normalized = normalizeIndexPath(virtualPath);
      if (normalized.empty() || isReservedVirtualPath(normalized)) continue;

      fs::path hostPath = childPointer->file_info.is_backing
                              ? dataDirectory /
                                    fs::path(childPointer->file_info.real_path)
                              : fs::path(childPointer->file_info.real_path);
      if (!hostPath.is_absolute()) {
        throw std::runtime_error(
            "Resolved VFS file has a non-absolute real path: " + virtualPath);
      }
      hostPath = hostPath.lexically_normal();

      VfsIndexResolvedFile exported;
      exported.normalized_path = normalized;
      exported.display_path = virtualPath;
      exported.host_path = hostPath.string();
      exported.consumer_path =
          VfsIndexPublisher::toConsumerPath(hostPath, pathStyle);
      exported.origin = childPointer->file_info.origin;
      exported.size = childPointer->file_info.size;
      exported.mode =
          static_cast<uint32_t>(childPointer->file_info.cached_mode & 07777);
      exported.mtime_ns = timeNs(childPointer->file_info.mtime);
      exported.is_backing = childPointer->file_info.is_backing;
      exported.blake3 = childPointer->file_info.cached_blake3;
      files.push_back(std::move(exported));
    }
  };
  visit(visit, tree.root, {});

  // A flat vector keeps deterministic publication order without retaining a
  // second allocation-heavy std::map of every resolved file. Large profiles
  // otherwise briefly hold both representations during publication.
  std::sort(files.begin(), files.end(),
            [](const VfsIndexResolvedFile& left,
               const VfsIndexResolvedFile& right) {
              return left.normalized_path < right.normalized_path;
            });
  for (size_t i = 1; i < files.size(); ++i) {
    if (files[i - 1].normalized_path == files[i].normalized_path) {
      throw std::runtime_error(
          "Resolved VFS contains duplicate case-insensitive path: " +
          files[i].display_path);
    }
  }
  return files;
}

void hashBytes(blake3_hasher& hasher, const void* bytes, std::size_t size)
{
  blake3_hasher_update(&hasher, bytes, size);
}

void hashUint64(blake3_hasher& hasher, uint64_t value)
{
  std::array<unsigned char, 8> bytes{};
  for (auto& byte : bytes) {
    byte = static_cast<unsigned char>(value & 0xff);
    value >>= 8;
  }
  hashBytes(hasher, bytes.data(), bytes.size());
}

void hashString(blake3_hasher& hasher, const std::string& value)
{
  hashUint64(hasher, value.size());
  hashBytes(hasher, value.data(), value.size());
}

void hashResolvedFile(blake3_hasher& hasher,
                      const VfsIndexResolvedFile& file)
{
  hashString(hasher, file.normalized_path);
  hashString(hasher, file.display_path);
  hashString(hasher, file.host_path);
  hashString(hasher, file.consumer_path);
  hashString(hasher, file.origin);
  hashUint64(hasher, file.size);
  hashUint64(hasher, file.mode);
  hashUint64(hasher, static_cast<uint64_t>(file.mtime_ns));
  const unsigned char backing = file.is_backing ? 1 : 0;
  const unsigned char hasDigest = file.blake3.has_value() ? 1 : 0;
  hashBytes(hasher, &backing, 1);
  hashBytes(hasher, &hasDigest, 1);
  if (file.blake3) {
    hashBytes(hasher, file.blake3->data(), file.blake3->size());
  }
}

VfsDigest resolvedDigest(const std::vector<VfsIndexResolvedFile>& files)
{
  blake3_hasher hasher;
  blake3_hasher_init(&hasher);
  constexpr char kDomain[] = "vfs-index.resolved-snapshot.v1";
  hashBytes(hasher, kDomain, sizeof(kDomain) - 1);
  for (const auto& file : files) {
    hashResolvedFile(hasher, file);
  }
  VfsDigest digest{};
  blake3_hasher_finalize(&hasher, digest.data(), digest.size());
  return digest;
}

VfsDigest archiveMembershipDigest(
    uint64_t probeCount, uint64_t bitCount, uint64_t archiveCount,
    uint64_t memberCount, const std::vector<unsigned char>& bits)
{
  blake3_hasher hasher;
  blake3_hasher_init(&hasher);
  constexpr char kDomain[] = "vfs-index.archive-membership.v1";
  hashBytes(hasher, kDomain, sizeof(kDomain) - 1);
  hashString(hasher, std::string(kArchiveMembershipAlgorithm));
  hashUint64(hasher, probeCount);
  hashUint64(hasher, bitCount);
  hashUint64(hasher, archiveCount);
  hashUint64(hasher, memberCount);
  hashUint64(hasher, bits.size());
  hashBytes(hasher, bits.data(), bits.size());
  VfsDigest digest{};
  blake3_hasher_finalize(&hasher, digest.data(), digest.size());
  return digest;
}

bool tableExists(sqlite3* database, const char* name)
{
  auto query = prepare(
      database,
      "SELECT 1 FROM sqlite_master WHERE type='table' AND name=?1;");
  bindText(database, query.get(), 1, name);
  return sqlite3_step(query.get()) == SQLITE_ROW;
}

QJsonObject locatorJson(const VfsIndexLocator& locator)
{
  QJsonObject json;
  json.insert(QStringLiteral("format"),
              QString::fromStdString(locator.format));
  json.insert(QStringLiteral("format_version"), locator.format_version);
  json.insert(QStringLiteral("schema_version"), locator.schema_version);
  json.insert(QStringLiteral("state"), QString::fromStdString(locator.state));
  json.insert(QStringLiteral("generation"),
              QString::fromStdString(locator.generation));
  json.insert(QStringLiteral("producer"),
              QString::fromStdString(locator.producer));
  json.insert(QStringLiteral("instance"),
              QString::fromStdString(locator.instance_name));
  json.insert(QStringLiteral("profile"),
              QString::fromStdString(locator.profile_name));
  if (locator.profile_digest) {
    json.insert(QStringLiteral("profile_digest"),
                QString::fromStdString(digestHex(*locator.profile_digest)));
  } else {
    json.insert(QStringLiteral("profile_digest"), QJsonValue::Null);
  }
  json.insert(QStringLiteral("resolved_snapshot_digest"),
              QString::fromStdString(
                  digestHex(locator.resolved_snapshot_digest)));
  json.insert(QStringLiteral("path_normalization"),
              QString::fromStdString(locator.path_normalization));
  json.insert(QStringLiteral("host_path_style"),
              QString::fromStdString(locator.host_path_style));
  json.insert(QStringLiteral("host_database_path"),
              QString::fromStdString(locator.host_database_path));
  json.insert(QStringLiteral("consumer_database_path"),
              QString::fromStdString(locator.consumer_database_path));
  json.insert(QStringLiteral("published_utc_ms"),
              static_cast<qint64>(locator.published_utc_ms));
  return json;
}

void fsyncFile(const fs::path& path)
{
  const int descriptor = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
  if (descriptor < 0) {
    throw std::runtime_error("Opening publication for flush failed: " +
                             std::string(std::strerror(errno)));
  }
  const int result = ::fsync(descriptor);
  const int saved = errno;
  ::close(descriptor);
  if (result != 0) {
    throw std::runtime_error("Flushing publication failed: " +
                             std::string(std::strerror(saved)));
  }
}

void fsyncDirectory(const fs::path& path)
{
  const int descriptor =
      ::open(path.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
  if (descriptor < 0) return;
  (void)::fsync(descriptor);
  ::close(descriptor);
}

void writeAtomic(const fs::path& destination, const QByteArray& contents,
                 const std::string& generation)
{
  fs::path temporary = destination;
  temporary += ".tmp-" + generation;
  const int descriptor =
      ::open(temporary.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
  if (descriptor < 0) {
    throw std::runtime_error("Creating temporary locator failed: " +
                             std::string(std::strerror(errno)));
  }

  std::size_t written = 0;
  while (written < static_cast<std::size_t>(contents.size())) {
    const ssize_t count =
        ::write(descriptor, contents.constData() + written,
                static_cast<std::size_t>(contents.size()) - written);
    if (count <= 0) {
      const int saved = errno;
      ::close(descriptor);
      std::error_code ignored;
      fs::remove(temporary, ignored);
      throw std::runtime_error("Writing locator failed: " +
                               std::string(std::strerror(saved)));
    }
    written += static_cast<std::size_t>(count);
  }
  if (::fsync(descriptor) != 0) {
    const int saved = errno;
    ::close(descriptor);
    std::error_code ignored;
    fs::remove(temporary, ignored);
    throw std::runtime_error("Flushing locator failed: " +
                             std::string(std::strerror(saved)));
  }
  if (::close(descriptor) != 0) {
    std::error_code ignored;
    fs::remove(temporary, ignored);
    throw std::runtime_error("Closing locator failed");
  }

  std::error_code error;
  fs::rename(temporary, destination, error);
  if (error) {
    fs::remove(temporary, error);
    throw std::runtime_error("Replacing locator failed: " + error.message());
  }
  fsyncDirectory(destination.parent_path());
}

std::optional<std::string> locatorGeneration(const fs::path& locatorPath)
{
  std::string error;
  auto locator = VfsIndexValidator::parseLocator(locatorPath, error);
  if (!locator || !validGeneration(locator->generation)) return std::nullopt;
  return locator->generation;
}

bool providerRowsMatch(
    const fs::path& databasePath,
    const std::vector<VfsProviderRoot>& providerRoots,
    VfsIndexConsumerPathStyle pathStyle)
{
  sqlite3* raw = nullptr;
  if (sqlite3_open_v2(
          databasePath.c_str(), &raw, SQLITE_OPEN_READONLY, nullptr) !=
      SQLITE_OK) {
    DbPtr failed(raw);
    return false;
  }
  DbPtr database(raw);
  auto rows = prepare(
      database.get(),
      "SELECT priority,root_key,origin,role,host_root,consumer_root,"
      "merkle_digest,file_count FROM providers ORDER BY priority;");
  std::size_t index = 0;
  while (sqlite3_step(rows.get()) == SQLITE_ROW) {
    if (index >= providerRoots.size() ||
        sqlite3_column_int64(rows.get(), 0) !=
            static_cast<sqlite3_int64>(index)) {
      return false;
    }
    const VfsProviderRoot& root = providerRoots[index];
    const std::string role =
        root.is_backing
            ? "base_game"
            : (root.origin == "Overwrite" ? "overwrite" : "mod");
    const std::string hostRoot =
        fs::path(root.root_key).lexically_normal().string();
    VfsDigest digest{};
    if (sqliteText(rows.get(), 1) != root.root_key ||
        sqliteText(rows.get(), 2) != root.origin ||
        sqliteText(rows.get(), 3) != role ||
        sqliteText(rows.get(), 4) != hostRoot ||
        sqliteText(rows.get(), 5) !=
            VfsIndexPublisher::toConsumerPath(
                fs::path(root.root_key), pathStyle) ||
        !sqliteDigest(rows.get(), 6, digest) ||
        digest != root.digest ||
        sqlite3_column_int64(rows.get(), 7) !=
            static_cast<sqlite3_int64>(root.file_count)) {
      return false;
    }
    ++index;
  }
  return index == providerRoots.size();
}

bool generationFile(const fs::path& path, std::string& generation)
{
  const std::string name = path.filename().string();
  constexpr std::string_view kPrefix = "vfs-index-";
  constexpr std::string_view kSuffix = ".sqlite3";
  if (!name.starts_with(kPrefix) || !name.ends_with(kSuffix)) return false;
  generation =
      name.substr(kPrefix.size(), name.size() - kPrefix.size() - kSuffix.size());
  return validGeneration(generation);
}

void retainGenerations(const fs::path& indexDirectory,
                       const std::string& current,
                       const std::optional<std::string>& previous)
{
  struct Candidate
  {
    fs::path path;
    std::string generation;
    fs::file_time_type modified;
  };
  std::vector<Candidate> candidates;
  std::error_code error;
  for (fs::directory_iterator it(indexDirectory, error), end;
       !error && it != end; it.increment(error)) {
    if (!it->is_regular_file(error)) continue;
    std::string generation;
    if (!generationFile(it->path(), generation)) continue;
    candidates.push_back(
        {it->path(), generation, it->last_write_time(error)});
  }

  std::sort(candidates.begin(), candidates.end(),
            [](const Candidate& left, const Candidate& right) {
              return left.modified > right.modified;
            });
  std::vector<std::string> keep{current};
  if (previous && *previous != current) keep.push_back(*previous);
  if (keep.size() < 2) {
    for (const auto& candidate : candidates) {
      if (candidate.generation != current) {
        keep.push_back(candidate.generation);
        break;
      }
    }
  }

  for (const auto& candidate : candidates) {
    if (std::find(keep.begin(), keep.end(), candidate.generation) !=
        keep.end()) {
      continue;
    }
#ifndef _WIN32
    const int descriptor =
        ::open(candidate.path.c_str(), O_RDONLY | O_CLOEXEC);
    if (descriptor < 0) continue;
    if (::flock(descriptor, LOCK_EX | LOCK_NB) != 0) {
      ::close(descriptor);
      continue;
    }
    fs::remove(candidate.path, error);
    ::flock(descriptor, LOCK_UN);
    ::close(descriptor);
#else
    fs::remove(candidate.path, error);
#endif
    error.clear();
  }
}

std::string uriEncodePath(const fs::path& path)
{
  const std::string source = path.string();
  constexpr char kHex[] = "0123456789ABCDEF";
  std::string encoded;
  encoded.reserve(source.size() + 32);
  for (const unsigned char byte : source) {
    if (std::isalnum(byte) || byte == '/' || byte == '-' || byte == '_' ||
        byte == '.' || byte == '~' || byte == ':') {
      encoded.push_back(static_cast<char>(byte));
    } else {
      encoded.push_back('%');
      encoded.push_back(kHex[byte >> 4]);
      encoded.push_back(kHex[byte & 0x0f]);
    }
  }
  return "file:" + encoded + "?mode=ro&immutable=1";
}

VfsIndexValidationResult invalid(std::string error)
{
  return {.index=std::nullopt, .error=std::move(error)};
}

bool exactJsonKeys(const QJsonObject& json)
{
  static const QStringList required{
      QStringLiteral("format"),
      QStringLiteral("format_version"),
      QStringLiteral("schema_version"),
      QStringLiteral("state"),
      QStringLiteral("generation"),
      QStringLiteral("producer"),
      QStringLiteral("instance"),
      QStringLiteral("profile"),
      QStringLiteral("profile_digest"),
      QStringLiteral("resolved_snapshot_digest"),
      QStringLiteral("path_normalization"),
      QStringLiteral("host_path_style"),
      QStringLiteral("host_database_path"),
      QStringLiteral("consumer_database_path"),
      QStringLiteral("published_utc_ms")};
  for (const QString& key : required) {
    if (!json.contains(key)) return false;
  }
  return true;
}
}  // namespace

std::optional<VfsIndexLocator> VfsIndexValidator::parseLocator(
    const fs::path& locatorPath, std::string& error)
{
  QFile file(QString::fromStdString(locatorPath.string()));
  if (!file.open(QIODevice::ReadOnly)) {
    error = "locator is missing or unreadable";
    return std::nullopt;
  }
  QJsonParseError parseError;
  const QJsonDocument document =
      QJsonDocument::fromJson(file.readAll(), &parseError);
  if (parseError.error != QJsonParseError::NoError ||
      !document.isObject()) {
    error = "locator is malformed JSON";
    return std::nullopt;
  }
  const QJsonObject json = document.object();
  if (!exactJsonKeys(json) ||
      json.value(QStringLiteral("format")).toString() !=
          QString::fromLatin1(kVfsIndexFormatName)) {
    error = "locator format identifier or fields are invalid";
    return std::nullopt;
  }

  const bool profileIsNull =
      json.value(QStringLiteral("profile_digest")).isNull();
  const auto profile =
      profileIsNull
          ? std::optional<VfsDigest>{}
          : parseDigest(
                json.value(QStringLiteral("profile_digest")).toString());
  const auto resolved = parseDigest(
      json.value(QStringLiteral("resolved_snapshot_digest")).toString());
  if ((!profileIsNull && !profile) || !resolved) {
    error = "locator contains an invalid digest";
    return std::nullopt;
  }

  VfsIndexLocator locator;
  locator.format =
      json.value(QStringLiteral("format")).toString().toStdString();
  locator.format_version =
      json.value(QStringLiteral("format_version")).toInt(-1);
  locator.schema_version =
      json.value(QStringLiteral("schema_version")).toInt(-1);
  locator.state =
      json.value(QStringLiteral("state")).toString().toStdString();
  locator.generation =
      json.value(QStringLiteral("generation")).toString().toStdString();
  locator.producer =
      json.value(QStringLiteral("producer")).toString().toStdString();
  locator.instance_name =
      json.value(QStringLiteral("instance")).toString().toStdString();
  locator.profile_name =
      json.value(QStringLiteral("profile")).toString().toStdString();
  locator.profile_digest = profile;
  locator.resolved_snapshot_digest = *resolved;
  locator.path_normalization =
      json.value(QStringLiteral("path_normalization")).toString().toStdString();
  locator.host_path_style =
      json.value(QStringLiteral("host_path_style")).toString().toStdString();
  locator.host_database_path =
      json.value(QStringLiteral("host_database_path")).toString().toStdString();
  locator.consumer_database_path =
      json.value(QStringLiteral("consumer_database_path"))
          .toString()
          .toStdString();
  locator.published_utc_ms =
      json.value(QStringLiteral("published_utc_ms")).toInteger(-1);

  if (locator.format_version != kVfsIndexFormatVersion) {
    error = "unsupported locator format version";
    return std::nullopt;
  }
  if (locator.schema_version != kVfsIndexSchemaVersion) {
    error = "unsupported locator schema version";
    return std::nullopt;
  }
  if (locator.state != "complete") {
    error = "locator state is not complete";
    return std::nullopt;
  }
  if (!validGeneration(locator.generation)) {
    error = "locator generation UUID is invalid";
    return std::nullopt;
  }
  if (locator.path_normalization != kVfsIndexNormalization) {
    error = "unsupported locator path normalization";
    return std::nullopt;
  }
  if (locator.host_path_style != "posix" &&
      locator.host_path_style != "windows") {
    error = "unsupported locator host path style";
    return std::nullopt;
  }
  if (!isAbsoluteHostPath(locator.host_database_path,
                          locator.host_path_style)) {
    error = "locator database host path is not absolute";
    return std::nullopt;
  }
  if (!isAbsoluteConsumerPath(locator.consumer_database_path)) {
    error = "locator database path is not an absolute Windows/Wine path";
    return std::nullopt;
  }
  if (locator.published_utc_ms <= 0) {
    error = "locator publication timestamp is invalid";
    return std::nullopt;
  }
  return locator;
}

bool VfsIndexValidator::isAbsoluteConsumerPath(const std::string& path)
{
  if (path.size() >= 4 && std::isalpha(static_cast<unsigned char>(path[0])) &&
      path[1] == ':' && (path[2] == '\\' || path[2] == '/')) {
    return true;
  }
  if (path.size() >= 5 &&
      ((path[0] == '\\' && path[1] == '\\') ||
       (path[0] == '/' && path[1] == '/'))) {
    const std::size_t serverEnd = path.find_first_of("\\/", 2);
    return serverEnd != std::string::npos && serverEnd > 2 &&
           serverEnd + 1 < path.size();
  }
  return false;
}

bool VfsIndexValidator::isAbsoluteHostPath(const std::string& path,
                                           const std::string& style)
{
  if (style == "windows") return isAbsoluteConsumerPath(path);
  return style == "posix" && !path.empty() && path.front() == '/';
}

std::optional<fs::path> VfsIndexValidator::resolveWinePathOnHost(
    const std::string& path)
{
#ifdef _WIN32
  return fs::path(path);
#else
  if (path.size() < 3 ||
      (path[0] != 'Z' && path[0] != 'z') || path[1] != ':' ||
      (path[2] != '\\' && path[2] != '/')) {
    return std::nullopt;
  }
  std::string host = path.substr(2);
  std::replace(host.begin(), host.end(), '\\', '/');
  return fs::path(host);
#endif
}

VfsIndexValidationResult VfsIndexValidator::validate(
    const fs::path& locatorPath, DatabasePathResolver resolver)
{
  std::string error;
  auto locator = parseLocator(locatorPath, error);
  if (!locator) return invalid(std::move(error));

  std::optional<fs::path> databasePath =
      resolver ? resolver(locator->consumer_database_path)
               : resolveWinePathOnHost(locator->consumer_database_path);
  if (!databasePath) {
    return invalid("consumer database path cannot be resolved on this host");
  }
  return validateDatabase(*databasePath, *locator);
}

VfsIndexValidationResult VfsIndexValidator::validateDatabase(
    const fs::path& databasePath, const VfsIndexLocator& locator)
{
  sqlite3* raw = nullptr;
  const std::string uri = uriEncodePath(databasePath);
  if (sqlite3_open_v2(uri.c_str(), &raw,
                      SQLITE_OPEN_READONLY | SQLITE_OPEN_URI |
                          SQLITE_OPEN_NOMUTEX,
                      nullptr) != SQLITE_OK) {
    DbPtr failed(raw);
    return invalid("database is missing, unreadable, or not SQLite");
  }
  DbPtr database(raw);

  try {
    auto applicationId = prepare(database.get(), "PRAGMA application_id;");
    if (sqlite3_step(applicationId.get()) != SQLITE_ROW ||
        sqlite3_column_int(applicationId.get(), 0) !=
            kVfsIndexApplicationId) {
      return invalid("database application ID is not VFSI");
    }
    auto schemaVersion = prepare(database.get(), "PRAGMA user_version;");
    if (sqlite3_step(schemaVersion.get()) != SQLITE_ROW ||
        sqlite3_column_int(schemaVersion.get(), 0) !=
            kVfsIndexSchemaVersion) {
      return invalid("database schema version is unsupported");
    }

    auto integrity = prepare(database.get(), "PRAGMA integrity_check;");
    if (sqlite3_step(integrity.get()) != SQLITE_ROW ||
        sqliteText(integrity.get(), 0) != "ok") {
      return invalid("database integrity check failed");
    }

    auto snapshot = prepare(
        database.get(),
        "SELECT state,generation,producer,instance_name,profile_name,"
        "profile_digest,resolved_snapshot_digest,path_normalization,"
        "host_path_style,published_utc_ms,expected_file_count"
        " FROM snapshot WHERE id=1;");
    if (sqlite3_step(snapshot.get()) != SQLITE_ROW) {
      return invalid("database snapshot row is missing");
    }
    std::optional<VfsDigest> profileDigest;
    VfsDigest resolvedSnapshotDigest{};
    if (sqlite3_column_type(snapshot.get(), 5) != SQLITE_NULL) {
      VfsDigest digest{};
      if (!sqliteDigest(snapshot.get(), 5, digest)) {
        return invalid("database snapshot digest has the wrong size");
      }
      profileDigest = digest;
    }
    if (!sqliteDigest(snapshot.get(), 6, resolvedSnapshotDigest)) {
      return invalid("database snapshot digest has the wrong size");
    }
    if (sqliteText(snapshot.get(), 0) != "complete") {
      return invalid("database snapshot state is not complete");
    }
    if (sqliteText(snapshot.get(), 1) != locator.generation) {
      return invalid("database generation does not match locator");
    }
    if (sqliteText(snapshot.get(), 2) != locator.producer ||
        sqliteText(snapshot.get(), 3) != locator.instance_name ||
        sqliteText(snapshot.get(), 4) != locator.profile_name) {
      return invalid("database producer, instance, or profile identity does "
                     "not match locator");
    }
    if (profileDigest != locator.profile_digest) {
      return invalid("database profile digest does not match locator");
    }
    if (resolvedSnapshotDigest != locator.resolved_snapshot_digest) {
      return invalid(
          "database resolved snapshot digest does not match locator");
    }
    if (sqliteText(snapshot.get(), 7) != locator.path_normalization ||
        sqliteText(snapshot.get(), 8) != locator.host_path_style ||
        sqlite3_column_int64(snapshot.get(), 9) != locator.published_utc_ms) {
      return invalid(
          "database normalization, path style, or timestamp does not match "
          "locator");
    }
    const sqlite3_int64 expectedCount = sqlite3_column_int64(snapshot.get(), 10);
    if (expectedCount < 0) {
      return invalid("database expected file count is invalid");
    }
    if (sqlite3_step(snapshot.get()) != SQLITE_DONE) {
      return invalid("database contains multiple snapshot rows");
    }

    auto providers = prepare(
        database.get(),
        "SELECT priority,host_root,consumer_root,merkle_digest,file_count"
        " FROM providers ORDER BY priority;");
    sqlite3_int64 expectedPriority = 0;
    while (sqlite3_step(providers.get()) == SQLITE_ROW) {
      if (sqlite3_column_int64(providers.get(), 0) != expectedPriority++) {
        return invalid("database provider priorities are not contiguous");
      }
      if (!isAbsoluteHostPath(sqliteText(providers.get(), 1),
                              locator.host_path_style) ||
          !isAbsoluteConsumerPath(sqliteText(providers.get(), 2))) {
        return invalid("database provider root path is not absolute");
      }
      if (sqlite3_column_type(providers.get(), 3) != SQLITE_NULL) {
        VfsDigest providerDigest{};
        if (!sqliteDigest(providers.get(), 3, providerDigest)) {
          return invalid("database provider digest has the wrong size");
        }
      }
      if (sqlite3_column_int64(providers.get(), 4) < 0) {
        return invalid("database provider file count is invalid");
      }
    }

    auto rows = prepare(
        database.get(),
        "SELECT normalized_path,display_path,host_path,consumer_path,origin,"
        "size,mode,mtime_ns,is_backing,blake3"
        " FROM resolved ORDER BY normalized_path;");
    std::vector<VfsIndexResolvedFile> files;
    files.reserve(static_cast<std::size_t>(expectedCount));
    std::string previous;
    while (sqlite3_step(rows.get()) == SQLITE_ROW) {
      VfsIndexResolvedFile file;
      file.normalized_path = sqliteText(rows.get(), 0);
      file.display_path = sqliteText(rows.get(), 1);
      file.host_path = sqliteText(rows.get(), 2);
      file.consumer_path = sqliteText(rows.get(), 3);
      file.origin = sqliteText(rows.get(), 4);
      file.size = static_cast<uint64_t>(sqlite3_column_int64(rows.get(), 5));
      file.mode = static_cast<uint32_t>(sqlite3_column_int64(rows.get(), 6));
      file.mtime_ns = sqlite3_column_int64(rows.get(), 7);
      file.is_backing = sqlite3_column_int(rows.get(), 8) != 0;
      if (!isAbsoluteHostPath(file.host_path, locator.host_path_style)) {
        return invalid("resolved row contains a non-absolute host path");
      }
      if (!isAbsoluteConsumerPath(file.consumer_path)) {
        return invalid("resolved row contains a non-absolute real path");
      }
      if (file.normalized_path.empty() ||
          file.normalized_path != normalizeIndexPath(file.display_path)) {
        return invalid("resolved row contains an invalid normalized path");
      }
      if (!previous.empty() && file.normalized_path <= previous) {
        return invalid("resolved rows are not uniquely normalized");
      }
      previous = file.normalized_path;

      if (sqlite3_column_type(rows.get(), 9) != SQLITE_NULL) {
        VfsDigest digest{};
        if (!sqliteDigest(rows.get(), 9, digest)) {
          return invalid("resolved row BLAKE3 has the wrong size");
        }
        file.blake3 = digest;
      }
      files.push_back(std::move(file));
    }
    if (files.size() != static_cast<std::size_t>(expectedCount)) {
      return invalid("resolved row count does not match snapshot metadata");
    }
    if (resolvedDigest(files) != locator.resolved_snapshot_digest) {
      return invalid("loaded resolved rows do not match snapshot digest");
    }

    std::shared_ptr<const VfsArchiveMemberIndex> archiveMembers;
    if (tableExists(database.get(), "archive_membership")) {
      auto proof = prepare(
          database.get(),
          "SELECT algorithm,probe_count,bit_count,archive_count,"
          "member_count,bits,bits_digest"
          " FROM archive_membership WHERE id=1;");
      const int proofResult = sqlite3_step(proof.get());
      if (proofResult == SQLITE_ROW) {
        const std::string algorithm = sqliteText(proof.get(), 0);
        const sqlite3_int64 probeCount = sqlite3_column_int64(proof.get(), 1);
        const sqlite3_int64 bitCount = sqlite3_column_int64(proof.get(), 2);
        const sqlite3_int64 archiveCount =
            sqlite3_column_int64(proof.get(), 3);
        const sqlite3_int64 memberCount =
            sqlite3_column_int64(proof.get(), 4);
        const int byteCount = sqlite3_column_bytes(proof.get(), 5);
        VfsDigest storedDigest{};
        if (algorithm != kArchiveMembershipAlgorithm ||
            probeCount != static_cast<sqlite3_int64>(
                              VfsArchiveMemberIndex::probeCount()) ||
            bitCount < 64 || archiveCount < 0 || memberCount < 0 ||
            bitCount > static_cast<sqlite3_int64>(
                           kMaximumArchiveProofBytes * 8) ||
            byteCount <= 0 ||
            static_cast<std::size_t>(byteCount) >
                kMaximumArchiveProofBytes ||
            static_cast<uint64_t>(byteCount) !=
                ((static_cast<uint64_t>(bitCount) + 63) / 64) *
                    sizeof(uint64_t) ||
            !sqliteDigest(proof.get(), 6, storedDigest)) {
          return invalid("database archive membership proof is invalid");
        }
        const auto* rawBits = static_cast<const unsigned char*>(
            sqlite3_column_blob(proof.get(), 5));
        if (rawBits == nullptr) {
          return invalid("database archive membership bits are missing");
        }
        std::vector<unsigned char> bits(
            rawBits, rawBits + static_cast<std::size_t>(byteCount));
        const VfsDigest computedDigest = archiveMembershipDigest(
            static_cast<uint64_t>(probeCount),
            static_cast<uint64_t>(bitCount),
            static_cast<uint64_t>(archiveCount),
            static_cast<uint64_t>(memberCount), bits);
        if (computedDigest != storedDigest) {
          return invalid(
              "database archive membership proof digest does not match");
        }
        archiveMembers = VfsArchiveMemberIndex::fromSerialized(
            static_cast<std::size_t>(bitCount),
            static_cast<std::size_t>(archiveCount),
            static_cast<std::size_t>(memberCount), bits);
        if (!archiveMembers) {
          return invalid(
              "database archive membership bitset is malformed");
        }
        if (sqlite3_step(proof.get()) != SQLITE_DONE) {
          return invalid(
              "database contains multiple archive membership proofs");
        }
        auto count = prepare(
            database.get(),
            "SELECT COUNT(*) FROM archive_membership;");
        if (sqlite3_step(count.get()) != SQLITE_ROW ||
            sqlite3_column_int64(count.get(), 0) != 1) {
          return invalid(
              "database contains unexpected archive membership rows");
        }
      } else if (proofResult != SQLITE_DONE) {
        return invalid("reading database archive membership proof failed");
      } else {
        auto count = prepare(
            database.get(),
            "SELECT COUNT(*) FROM archive_membership;");
        if (sqlite3_step(count.get()) != SQLITE_ROW ||
            sqlite3_column_int64(count.get(), 0) != 0) {
          return invalid(
              "database contains an unrecognized archive membership row");
        }
      }
    }

    return {.index=VfsIndexValidated{
                locator, std::move(files), std::move(archiveMembers)},
            .error={}};
  } catch (const std::exception& exception) {
    return invalid(std::string("database validation failed: ") +
                   exception.what());
  }
}

std::string VfsIndexPublisher::toConsumerPath(
    const fs::path& input, VfsIndexConsumerPathStyle style)
{
  std::string path = input.lexically_normal().string();
  if (VfsIndexValidator::isAbsoluteConsumerPath(path)) {
    std::replace(path.begin(), path.end(), '/', '\\');
    return path;
  }
  if (style == VfsIndexConsumerPathStyle::Wine && input.is_absolute()) {
    std::replace(path.begin(), path.end(), '/', '\\');
    return "Z:" + path;
  }
#ifdef _WIN32
  if (style == VfsIndexConsumerPathStyle::NativeWindows &&
      input.is_absolute()) {
    std::replace(path.begin(), path.end(), '/', '\\');
    return path;
  }
#endif
  throw std::runtime_error(
      "Cannot translate host path into an absolute Windows consumer path: " +
      input.string());
}

void VfsIndexPublisher::removePublicationArtifacts(VfsTree& tree)
{
  tree.root.removeFromTree(
      {"SKSE", "Plugins", "VFSIndexer", kVfsIndexLocatorName});
  tree.root.removeFromTree(
      {"SKSE", "Plugins", "Fluorine", kLegacyVfsIndexLocatorName});
  tree.root.removeFromTree({kVfsIndexLocatorName});
  tree.root.removeFromTree({kLegacyVfsIndexLocatorName});
  tree.root.removeFromTree({".vfs-indexer", "index"});
  tree.root.removeFromTree({".fluorine", "index"});
}

VfsIndexPublicationResult VfsIndexPublisher::publish(
    VfsTree& tree, const std::vector<VfsProviderRoot>& providerRoots,
    const VfsDigest& profileDigest, const fs::path& dataDirectory,
    const VfsIndexPublicationContext& context,
    std::shared_ptr<const VfsArchiveMemberIndex> archiveMembers) noexcept
{
  VfsIndexPublicationResult result;
  fs::path temporaryDatabase;
  try {
    removePublicationArtifacts(tree);
    if (context.output_base.empty() || !context.output_base.is_absolute()) {
      throw std::runtime_error(
          "VFS index output base is not an absolute path");
    }
    if (dataDirectory.empty() || !dataDirectory.is_absolute()) {
      throw std::runtime_error("VFS Data directory is not an absolute path");
    }

    const fs::path indexDirectory =
        context.output_base / ".vfs-indexer" / "index";
    const fs::path locatorPath =
        context.output_base / kVfsIndexLocatorName;
    std::error_code error;
    fs::create_directories(indexDirectory, error);
    if (error) {
      throw std::runtime_error(
          "Unable to create VFS index directory: " + error.message());
    }

    const std::optional<std::string> previous =
        locatorGeneration(locatorPath);
    std::vector<VfsIndexResolvedFile> files =
        flattenTree(tree, dataDirectory, context.consumer_path_style);
    const VfsDigest snapshotDigest = resolvedDigest(files);
    const std::size_t fileCount = files.size();
#ifdef _WIN32
    const std::string hostPathStyle = "windows";
#else
    const std::string hostPathStyle = "posix";
#endif

    std::string locatorError;
    const auto existing =
        VfsIndexValidator::parseLocator(locatorPath, locatorError);
    if (existing &&
        existing->producer == context.producer &&
        existing->instance_name == context.instance_name &&
        existing->profile_name == context.profile_name &&
        existing->profile_digest ==
            std::optional<VfsDigest>{profileDigest} &&
        existing->resolved_snapshot_digest == snapshotDigest &&
        existing->host_path_style == hostPathStyle) {
      const fs::path existingDatabase =
          fs::path(existing->host_database_path).lexically_normal();
      const fs::path expectedDatabase =
          indexDirectory /
          ("vfs-index-" + existing->generation + ".sqlite3");
      bool proofMatches = false;
      if (existingDatabase == expectedDatabase.lexically_normal() &&
          existing->consumer_database_path ==
              toConsumerPath(
                  existingDatabase, context.consumer_path_style)) {
        const auto validation =
            VfsIndexValidator::validateDatabase(
                existingDatabase, *existing);
        if (validation &&
            providerRowsMatch(
                existingDatabase, providerRoots,
                context.consumer_path_style)) {
          const auto& oldProof =
              validation.index->archive_members;
          const bool newHasProof =
              archiveMembers && archiveMembers->complete();
          proofMatches =
              newHasProof == static_cast<bool>(oldProof);
          if (proofMatches && newHasProof) {
            proofMatches =
                archiveMembers->archiveCount() ==
                    oldProof->archiveCount() &&
                archiveMembers->memberCount() ==
                    oldProof->memberCount() &&
                archiveMembers->serializedBits() ==
                    oldProof->serializedBits();
          }
        }
      }
      if (proofMatches) {
        result.success = true;
        result.generation = existing->generation;
        result.resolved_snapshot_digest = snapshotDigest;
        result.database_path = existingDatabase;
        result.locator_path = locatorPath;
        result.reused_existing = true;
        result.file_count = fileCount;
        retainGenerations(
            indexDirectory, existing->generation, std::nullopt);
        return result;
      }
    }

    const std::string generation = newGeneration();
    const fs::path databasePath =
        indexDirectory / ("vfs-index-" + generation + ".sqlite3");
    temporaryDatabase =
        indexDirectory / (".vfs-index-" + generation + ".tmp.sqlite3");

    sqlite3* raw = nullptr;
    if (sqlite3_open_v2(
            temporaryDatabase.c_str(), &raw,
            SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE |
                SQLITE_OPEN_EXCLUSIVE | SQLITE_OPEN_NOMUTEX,
            nullptr) != SQLITE_OK) {
      DbPtr failed(raw);
      throwDb(raw, "Creating temporary VFS index");
    }
    DbPtr database(raw);
    execSql(database.get(), "PRAGMA journal_mode=DELETE;");
    execSql(database.get(), "PRAGMA synchronous=FULL;");
    execSql(database.get(), "PRAGMA foreign_keys=ON;");
    execSql(database.get(), "PRAGMA locking_mode=EXCLUSIVE;");
    execSql(database.get(), "PRAGMA application_id=1447449417;");
    execSql(database.get(), "PRAGMA user_version=1;");
    execSql(database.get(),
            "CREATE TABLE snapshot("
            " id INTEGER PRIMARY KEY CHECK(id=1),"
            " state TEXT NOT NULL CHECK(state IN('building','complete')),"
            " generation TEXT NOT NULL,producer TEXT NOT NULL,"
            " instance_name TEXT NOT NULL,profile_name TEXT NOT NULL,"
            " profile_digest BLOB NULL"
            " CHECK(profile_digest IS NULL OR length(profile_digest)=32),"
            " resolved_snapshot_digest BLOB NOT NULL"
            " CHECK(length(resolved_snapshot_digest)=32),"
            " path_normalization TEXT NOT NULL,"
            " host_path_style TEXT NOT NULL,"
            " published_utc_ms INTEGER NOT NULL,"
            " expected_file_count INTEGER NOT NULL CHECK(expected_file_count>=0));"
            "CREATE TABLE providers("
            " priority INTEGER PRIMARY KEY,root_key TEXT NOT NULL,"
            " origin TEXT NOT NULL,role TEXT NOT NULL,"
            " host_root TEXT NOT NULL,consumer_root TEXT NOT NULL,"
            " merkle_digest BLOB NULL"
            " CHECK(merkle_digest IS NULL OR length(merkle_digest)=32),"
            " file_count INTEGER NOT NULL CHECK(file_count>=0));"
            "CREATE TABLE resolved("
            " normalized_path TEXT PRIMARY KEY,display_path TEXT NOT NULL,"
            " host_path TEXT NOT NULL,consumer_path TEXT NOT NULL,"
            " origin TEXT NOT NULL,"
            " size INTEGER NOT NULL CHECK(size>=0),mode INTEGER NOT NULL,"
            " mtime_ns INTEGER NOT NULL,is_backing INTEGER NOT NULL"
            " CHECK(is_backing IN(0,1)),"
            " blake3 BLOB NULL CHECK(blake3 IS NULL OR length(blake3)=32))"
            " WITHOUT ROWID;"
            "CREATE TABLE archive_membership("
            " id INTEGER PRIMARY KEY CHECK(id=1),"
            " algorithm TEXT NOT NULL,"
            " probe_count INTEGER NOT NULL CHECK(probe_count>0),"
            " bit_count INTEGER NOT NULL CHECK(bit_count>=64),"
            " archive_count INTEGER NOT NULL CHECK(archive_count>=0),"
            " member_count INTEGER NOT NULL CHECK(member_count>=0),"
            " bits BLOB NOT NULL,"
            " bits_digest BLOB NOT NULL CHECK(length(bits_digest)=32));");
    execSql(database.get(), "BEGIN IMMEDIATE;");

    auto snapshot = prepare(
        database.get(),
        "INSERT INTO snapshot(id,state,generation,producer,instance_name,"
        "profile_name,profile_digest,resolved_snapshot_digest,"
        "path_normalization,host_path_style,published_utc_ms,"
        "expected_file_count)"
        " VALUES(1,'building',?1,?2,?3,?4,?5,?6,?7,?8,?9,?10);");
    bindText(database.get(), snapshot.get(), 1, generation);
    bindText(database.get(), snapshot.get(), 2, context.producer);
    bindText(database.get(), snapshot.get(), 3, context.instance_name);
    bindText(database.get(), snapshot.get(), 4, context.profile_name);
    bindDigest(database.get(), snapshot.get(), 5, profileDigest);
    bindDigest(database.get(), snapshot.get(), 6, snapshotDigest);
    bindText(database.get(), snapshot.get(), 7, kVfsIndexNormalization);
    bindText(database.get(), snapshot.get(), 8, hostPathStyle);
    const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                         std::chrono::system_clock::now().time_since_epoch())
                         .count();
    sqlite3_bind_int64(snapshot.get(), 9, now);
    sqlite3_bind_int64(snapshot.get(), 10,
                       static_cast<sqlite3_int64>(files.size()));
    if (sqlite3_step(snapshot.get()) != SQLITE_DONE) {
      throwDb(database.get(), "Writing VFS index snapshot");
    }

    auto provider = prepare(
        database.get(),
        "INSERT INTO providers(priority,root_key,origin,role,host_root,"
        "consumer_root,merkle_digest,file_count)"
        " VALUES(?1,?2,?3,?4,?5,?6,?7,?8);");
    for (std::size_t priority = 0; priority < providerRoots.size();
         ++priority) {
      const auto& root = providerRoots[priority];
      sqlite3_reset(provider.get());
      sqlite3_clear_bindings(provider.get());
      sqlite3_bind_int64(provider.get(), 1,
                         static_cast<sqlite3_int64>(priority));
      bindText(database.get(), provider.get(), 2, root.root_key);
      bindText(database.get(), provider.get(), 3, root.origin);
      const std::string role =
          root.is_backing
              ? "base_game"
              : (root.origin == "Overwrite" ? "overwrite" : "mod");
      bindText(database.get(), provider.get(), 4, role);
      bindText(database.get(), provider.get(), 5,
               fs::path(root.root_key).lexically_normal().string());
      bindText(database.get(), provider.get(), 6,
               toConsumerPath(fs::path(root.root_key),
                              context.consumer_path_style));
      bindDigest(database.get(), provider.get(), 7, root.digest);
      sqlite3_bind_int64(provider.get(), 8,
                         static_cast<sqlite3_int64>(root.file_count));
      if (sqlite3_step(provider.get()) != SQLITE_DONE) {
        throwDb(database.get(), "Writing VFS index provider");
      }
    }

    auto resolved = prepare(
        database.get(),
        "INSERT INTO resolved(normalized_path,display_path,host_path,"
        "consumer_path,origin,size,mode,mtime_ns,is_backing,blake3)"
        " VALUES(?1,?2,?3,?4,?5,?6,?7,?8,?9,?10);");
    for (const auto& file : files) {
      sqlite3_reset(resolved.get());
      sqlite3_clear_bindings(resolved.get());
      bindText(database.get(), resolved.get(), 1, file.normalized_path);
      bindText(database.get(), resolved.get(), 2, file.display_path);
      bindText(database.get(), resolved.get(), 3, file.host_path);
      bindText(database.get(), resolved.get(), 4, file.consumer_path);
      bindText(database.get(), resolved.get(), 5, file.origin);
      sqlite3_bind_int64(resolved.get(), 6,
                         static_cast<sqlite3_int64>(file.size));
      sqlite3_bind_int64(resolved.get(), 7, file.mode);
      sqlite3_bind_int64(resolved.get(), 8, file.mtime_ns);
      sqlite3_bind_int(resolved.get(), 9, file.is_backing ? 1 : 0);
      if (file.blake3) {
        bindDigest(database.get(), resolved.get(), 10, *file.blake3);
      } else {
        sqlite3_bind_null(resolved.get(), 10);
      }
      if (sqlite3_step(resolved.get()) != SQLITE_DONE) {
        throwDb(database.get(), "Writing VFS index resolved row");
      }
    }

    if (archiveMembers && archiveMembers->complete()) {
      const std::vector<unsigned char> bits =
          archiveMembers->serializedBits();
      const VfsDigest proofDigest = archiveMembershipDigest(
          VfsArchiveMemberIndex::probeCount(),
          archiveMembers->bitCount(), archiveMembers->archiveCount(),
          archiveMembers->memberCount(), bits);
      auto proof = prepare(
          database.get(),
          "INSERT INTO archive_membership("
          "id,algorithm,probe_count,bit_count,archive_count,member_count,"
          "bits,bits_digest) VALUES(1,?1,?2,?3,?4,?5,?6,?7);");
      bindText(database.get(), proof.get(), 1,
               std::string(kArchiveMembershipAlgorithm));
      sqlite3_bind_int64(
          proof.get(), 2,
          static_cast<sqlite3_int64>(
              VfsArchiveMemberIndex::probeCount()));
      sqlite3_bind_int64(
          proof.get(), 3,
          static_cast<sqlite3_int64>(archiveMembers->bitCount()));
      sqlite3_bind_int64(
          proof.get(), 4,
          static_cast<sqlite3_int64>(archiveMembers->archiveCount()));
      sqlite3_bind_int64(
          proof.get(), 5,
          static_cast<sqlite3_int64>(archiveMembers->memberCount()));
      if (sqlite3_bind_blob(
              proof.get(), 6, bits.data(),
              static_cast<int>(bits.size()), SQLITE_TRANSIENT) != SQLITE_OK) {
        throwDb(database.get(), "Binding archive membership bits");
      }
      bindDigest(database.get(), proof.get(), 7, proofDigest);
      if (sqlite3_step(proof.get()) != SQLITE_DONE) {
        throwDb(database.get(), "Writing archive membership proof");
      }
    }

    execSql(database.get(),
            "UPDATE snapshot SET state='complete' WHERE id=1;");
    execSql(database.get(), "COMMIT;");
    execSql(database.get(), "PRAGMA optimize;");
    resolved.reset();
    provider.reset();
    snapshot.reset();
    database.reset();

    VfsIndexLocator locator;
    locator.format = kVfsIndexFormatName;
    locator.format_version = kVfsIndexFormatVersion;
    locator.schema_version = kVfsIndexSchemaVersion;
    locator.state = "complete";
    locator.generation = generation;
    locator.producer = context.producer;
    locator.instance_name = context.instance_name;
    locator.profile_name = context.profile_name;
    locator.profile_digest = profileDigest;
    locator.resolved_snapshot_digest = snapshotDigest;
    locator.path_normalization = kVfsIndexNormalization;
    locator.host_path_style = hostPathStyle;
    locator.host_database_path = databasePath.lexically_normal().string();
    locator.consumer_database_path =
        toConsumerPath(databasePath, context.consumer_path_style);
    locator.published_utc_ms = now;

    // The tree and immutable index remain live for the session, but the
    // publication vector is only needed until the database is complete. Drop
    // its string-heavy storage before producer validation, which otherwise
    // materializes a second full resolved-file vector on large profiles.
    std::vector<VfsIndexResolvedFile>().swap(files);
    {
      const auto validation =
          VfsIndexValidator::validateDatabase(temporaryDatabase, locator);
      if (!validation) {
        throw std::runtime_error("Producer validation rejected VFS index: " +
                                 validation.error);
      }
    }

    fsyncFile(temporaryDatabase);
    fs::rename(temporaryDatabase, databasePath, error);
    if (error) {
      throw std::runtime_error(
          "Publishing immutable VFS index failed: " + error.message());
    }
    fsyncDirectory(indexDirectory);

    const QByteArray locatorContents =
        QJsonDocument(locatorJson(locator)).toJson(QJsonDocument::Indented);
    writeAtomic(locatorPath, locatorContents, generation);
    retainGenerations(indexDirectory, generation, previous);

    result.success = true;
    result.generation = generation;
    result.resolved_snapshot_digest = snapshotDigest;
    result.database_path = databasePath;
    result.locator_path = locatorPath;
    result.file_count = fileCount;
    return result;
  } catch (const std::exception& exception) {
    result.error = exception.what();
  } catch (...) {
    result.error = "unknown VFS index publication failure";
  }

  if (!temporaryDatabase.empty()) {
    std::error_code ignored;
    fs::remove(temporaryDatabase, ignored);
    fs::remove(temporaryDatabase.string() + "-journal", ignored);
  }
  return result;
}
