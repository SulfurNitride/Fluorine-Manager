#include "vfscatalog.h"

#include "../fluorinepaths.h"

#include <QDir>

#include <blake3.h>
#include <sqlite3.h>
#ifdef FLUORINE_HAS_BSA_FFI
#include <bsa_ffi.h>
#endif

#include <array>
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <memory>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <stdexcept>
#include <sys/stat.h>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <unistd.h>

namespace
{
namespace fs = std::filesystem;

// v5 allows an empty blake3 blob for unverified content and uses metadata
// identities for provider generations and archive manifest cache keys.
// v6 folds non-ASCII letters in normalized paths (see normalizeForLookup), so
// derived roots and archive manifests keyed by the old ASCII-only form rebuild.
constexpr int kSchemaVersion = 6;
constexpr size_t kHashBufferSize = 1024 * 1024;
constexpr size_t kMaximumArchiveMembershipCacheBytes = 256 * 1024 * 1024;

struct DbCloser
{
  void operator()(sqlite3* db) const { if (db != nullptr) sqlite3_close(db); }
};
using DbPtr = std::unique_ptr<sqlite3, DbCloser>;

struct StmtCloser
{
  void operator()(sqlite3_stmt* stmt) const { if (stmt != nullptr) sqlite3_finalize(stmt); }
};
using StmtPtr = std::unique_ptr<sqlite3_stmt, StmtCloser>;

struct FileDescriptorGuard
{
  int descriptor = -1;
  ~FileDescriptorGuard()
  {
    if (descriptor >= 0) ::close(descriptor);
  }
};

class ThreadJoinGuard
{
public:
  ThreadJoinGuard(std::vector<std::thread>& threads,
                  std::atomic<bool>& stop_requested)
      : m_threads(threads), m_stop_requested(stop_requested)
  {}

  ~ThreadJoinGuard()
  {
    if (m_joined) return;
    m_stop_requested.store(true, std::memory_order_relaxed);
    joinAll();
  }

  void join()
  {
    joinAll();
    m_joined = true;
  }

private:
  void joinAll()
  {
    for (std::thread& thread : m_threads) {
      if (thread.joinable()) thread.join();
    }
  }

  std::vector<std::thread>& m_threads;
  std::atomic<bool>& m_stop_requested;
  bool m_joined = false;
};

struct HashWorkerStopped
{};

void throwIfCancelled(std::stop_token stop_token)
{
  if (stop_token.stop_requested()) throw VfsCatalogCancelled();
}

void throwIfHashStopped(
    std::stop_token stop_token,
    const std::atomic<bool>* worker_stop_requested)
{
  throwIfCancelled(stop_token);
  if (worker_stop_requested != nullptr &&
      worker_stop_requested->load(std::memory_order_relaxed)) {
    throw HashWorkerStopped{};
  }
}

[[noreturn]] void throwDb(sqlite3* db, const std::string& operation)
{
  throw std::runtime_error(operation + ": " +
                           (db != nullptr ? sqlite3_errmsg(db) : "SQLite error"));
}

void exec(sqlite3* db, const char* sql)
{
  char* error = nullptr;
  if (sqlite3_exec(db, sql, nullptr, nullptr, &error) != SQLITE_OK) {
    const std::string message = error != nullptr ? error : sqlite3_errmsg(db);
    sqlite3_free(error);
    throw std::runtime_error(message);
  }
}

StmtPtr prepare(sqlite3* db, const char* sql)
{
  sqlite3_stmt* raw = nullptr;
  if (sqlite3_prepare_v2(db, sql, -1, &raw, nullptr) != SQLITE_OK) {
    throwDb(db, "Preparing VFS catalog statement");
  }
  return StmtPtr(raw);
}

void bindText(sqlite3* db, sqlite3_stmt* stmt, int index, const std::string& value)
{
  if (sqlite3_bind_text(stmt, index, value.data(), static_cast<int>(value.size()),
                        SQLITE_TRANSIENT) != SQLITE_OK) {
    throwDb(db, "Binding VFS catalog text");
  }
}

void bindDigest(sqlite3* db, sqlite3_stmt* stmt, int index,
                const VfsDigest& digest)
{
  if (sqlite3_bind_blob(stmt, index, digest.data(), digest.size(),
                        SQLITE_TRANSIENT) != SQLITE_OK) {
    throwDb(db, "Binding VFS catalog digest");
  }
}

std::vector<std::string> splitPath(const std::string& path)
{
  std::vector<std::string> parts;
  size_t start = 0;
  while (start < path.size()) {
    while (start < path.size() &&
           (path[start] == '/' || static_cast<unsigned char>(path[start]) == 92)) {
      ++start;
    }
    if (start >= path.size()) break;
    const size_t end = path.find_first_of("/\\", start);
    parts.push_back(path.substr(start, end == std::string::npos
                                          ? std::string::npos : end - start));
    if (end == std::string::npos) break;
    start = end + 1;
  }
  return parts;
}

std::string fastRelative(const std::string& path, const std::string& root)
{
  if (path.size() <= root.size()) return {};
  size_t start = root.size();
  while (start < path.size() &&
         (path[start] == '/' || static_cast<unsigned char>(path[start]) == 92)) {
    ++start;
  }
  return start < path.size() ? path.substr(start) : std::string{};
}

int64_t timespecNs(const timespec& value)
{
  return static_cast<int64_t>(value.tv_sec) * 1000000000LL + value.tv_nsec;
}

std::chrono::system_clock::time_point timePoint(const timespec& value)
{
  return std::chrono::system_clock::time_point(
      std::chrono::seconds(value.tv_sec) + std::chrono::nanoseconds(value.tv_nsec));
}

bool sameContentFingerprint(const struct stat& left, const struct stat& right)
{
  return left.st_dev == right.st_dev && left.st_ino == right.st_ino &&
         left.st_size == right.st_size &&
         timespecNs(left.st_mtim) == timespecNs(right.st_mtim) &&
         timespecNs(left.st_ctim) == timespecNs(right.st_ctim);
}

std::array<unsigned char, BLAKE3_OUT_LEN> hashFile(
    const fs::path& path,
    std::stop_token stop_token = {},
    const std::atomic<bool>* worker_stop_requested = nullptr)
{
  throwIfHashStopped(stop_token, worker_stop_requested);
  const int fd = ::open(path.c_str(),
                        O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
  if (fd < 0) {
    throw std::runtime_error("Unable to hash " + path.string() + ": " +
                             std::strerror(errno));
  }
  const FileDescriptorGuard fdGuard{fd};
  struct stat opened {};
  if (::fstat(fd, &opened) != 0 || !S_ISREG(opened.st_mode)) {
    throw std::runtime_error("VFS catalog file is no longer regular: " +
                             path.string());
  }

  blake3_hasher hasher;
  blake3_hasher_init(&hasher);
  std::array<unsigned char, kHashBufferSize> buffer{};
  for (;;) {
    throwIfHashStopped(stop_token, worker_stop_requested);
    const ssize_t count = ::read(fd, buffer.data(), buffer.size());
    if (count == 0) break;
    if (count < 0) {
      const int saved = errno;
      if (saved == EINTR) continue;
      throw std::runtime_error("Unable to hash " + path.string() + ": " +
                               std::strerror(saved));
    }
    throwIfHashStopped(stop_token, worker_stop_requested);
    blake3_hasher_update(&hasher, buffer.data(), static_cast<size_t>(count));
  }
  throwIfHashStopped(stop_token, worker_stop_requested);

  std::array<unsigned char, BLAKE3_OUT_LEN> digest{};
  blake3_hasher_finalize(&hasher, digest.data(), digest.size());
  return digest;
}

uint64_t fnv1a(const std::string& value)
{
  uint64_t hash = 1469598103934665603ULL;
  for (const unsigned char c : value) {
    hash ^= c;
    hash *= 1099511628211ULL;
  }
  return hash;
}

std::string catalogRootIdentity(const std::string& path)
{
  // Persistent keys must not vary with aliases such as Bazzite's /home and
  // /var/home. Callers retain the original path for filesystem traversal.
  std::error_code error;
  fs::path identity = fs::weakly_canonical(fs::path(path), error);
  if (error) {
    error.clear();
    identity = fs::absolute(fs::path(path), error);
  }
  if (error) identity = fs::path(path);
  return identity.lexically_normal().string();
}

bool isBethesdaArchive(const std::string& path)
{
  const std::string normalized = normalizeForLookup(path);
  return normalized.ends_with(".bsa") || normalized.ends_with(".ba2");
}

struct ArchiveCandidate
{
  std::string real_path;
  VfsDigest digest{};
};

struct PendingCatalogFile
{
  fs::path path;
  std::string full;
  std::string relative;
  std::string normalized;
  struct stat metadata {};
  std::optional<VfsDigest> digest;
  bool reuse_row = false;
};

struct CachedCatalogFile
{
  int64_t device = 0;
  int64_t inode = 0;
  int64_t size = 0;
  int64_t mode = 0;
  int64_t mtime_ns = 0;
  int64_t ctime_ns = 0;
  VfsDigest digest{};
  bool has_digest = false;
};

using HashCompletion =
    std::function<void(const PendingCatalogFile&)>;

std::size_t hashChangedFiles(
    std::vector<PendingCatalogFile>& files,
    const HashCompletion& completion,
    std::stop_token stop_token)
{
  throwIfCancelled(stop_token);
  std::vector<std::size_t> changed;
  changed.reserve(files.size());
  for (std::size_t index = 0; index < files.size(); ++index) {
    throwIfCancelled(stop_token);
    if (!files[index].reuse_row) changed.push_back(index);
  }
  if (changed.empty()) {
    throwIfCancelled(stop_token);
    return 0;
  }

  const unsigned int hardware =
      std::max(1u, std::thread::hardware_concurrency());
  const std::size_t workerCount =
      std::min<std::size_t>(changed.size(), hardware);
  std::atomic<std::size_t> next{0};
  std::atomic<bool> stop{false};
  std::mutex stateMutex;
  std::condition_variable completedCondition;
  std::vector<std::size_t> completed;
  completed.reserve(changed.size());
  std::exception_ptr workerError;
  const auto recordWorkerError = [&](std::exception_ptr error) {
    {
      std::scoped_lock lock(stateMutex);
      if (workerError == nullptr) workerError = std::move(error);
    }
    stop.store(true, std::memory_order_relaxed);
    completedCondition.notify_all();
  };
  std::stop_callback wakeOnStop(stop_token, [&]() {
    completedCondition.notify_all();
  });

  std::vector<std::thread> workers;
  workers.reserve(workerCount);
  ThreadJoinGuard joinGuard(workers, stop);
  try {
    for (std::size_t worker = 0; worker < workerCount; ++worker) {
      throwIfCancelled(stop_token);
      workers.emplace_back([&]() {
        for (;;) {
          if (stop.load(std::memory_order_relaxed)) return;
          try {
            throwIfCancelled(stop_token);
            const std::size_t work =
                next.fetch_add(1, std::memory_order_relaxed);
            if (work >= changed.size()) return;
            throwIfCancelled(stop_token);
            const std::size_t fileIndex = changed[work];
            PendingCatalogFile& file = files[fileIndex];
            bool stable = false;
            for (int attempt = 0; attempt < 3; ++attempt) {
              throwIfHashStopped(stop_token, &stop);
              const struct stat before = file.metadata;
              file.digest = hashFile(file.path, stop_token, &stop);
              throwIfHashStopped(stop_token, &stop);
              struct stat after {};
              if (::lstat(file.full.c_str(), &after) != 0) {
                throw std::runtime_error(
                    "File disappeared while hashing: " + file.full);
              }
              file.metadata = after;
              if (sameContentFingerprint(before, after)) {
                stable = true;
                break;
              }
            }
            if (!stable) {
              throw std::runtime_error(
                  "File kept changing while hashing: " + file.full);
            }
            throwIfHashStopped(stop_token, &stop);
            {
              std::scoped_lock lock(stateMutex);
              completed.push_back(fileIndex);
            }
            completedCondition.notify_one();
          } catch (const HashWorkerStopped&) {
            return;
          } catch (...) {
            recordWorkerError(std::current_exception());
            return;
          }
        }
      });
    }
  } catch (...) {
    stop.store(true, std::memory_order_relaxed);
    completedCondition.notify_all();
    joinGuard.join();
    throw;
  }

  std::size_t reported = 0;
  std::exception_ptr completionError;
  try {
    while (reported < changed.size()) {
      throwIfCancelled(stop_token);
      std::vector<std::size_t> ready;
      {
        std::unique_lock lock(stateMutex);
        completedCondition.wait_for(
            lock, std::chrono::milliseconds(100), [&]() {
              return !completed.empty() || workerError != nullptr ||
                     stop_token.stop_requested();
            });
        ready.swap(completed);
        if (ready.empty() && workerError != nullptr) break;
      }
      for (const std::size_t index : ready) {
        throwIfCancelled(stop_token);
        completion(files[index]);
        ++reported;
        throwIfCancelled(stop_token);
      }
    }
  } catch (...) {
    stop.store(true, std::memory_order_relaxed);
    completionError = std::current_exception();
  }

  joinGuard.join();
  if (completionError != nullptr) std::rethrow_exception(completionError);
  throwIfCancelled(stop_token);
  if (workerError != nullptr) std::rethrow_exception(workerError);
  if (reported != changed.size()) {
    throw std::runtime_error(
        "Parallel file hashing stopped before all files completed");
  }
  return workerCount;
}

struct Root
{
  std::string key;
  std::string path;
  std::string origin;
  bool backing = false;
  bool missing_is_empty = false;
};

void hashBytes(blake3_hasher& hasher, const void* data, size_t size)
{
  blake3_hasher_update(&hasher, data, size);
}

void hashBytesCancellable(blake3_hasher& hasher, const void* data,
                          size_t size, std::stop_token stop_token)
{
  throwIfCancelled(stop_token);
  const auto* bytes = static_cast<const unsigned char*>(data);
  size_t offset = 0;
  while (offset < size) {
    throwIfCancelled(stop_token);
    const size_t count = std::min(kHashBufferSize, size - offset);
    blake3_hasher_update(&hasher, bytes + offset, count);
    offset += count;
  }
}

void hashString(blake3_hasher& hasher, const std::string& value)
{
  uint64_t size = static_cast<uint64_t>(value.size());
  std::array<unsigned char, sizeof(size)> encoded{};
  for (size_t i = 0; i < encoded.size(); ++i) {
    encoded[i] = static_cast<unsigned char>(size & 0xffu);
    size >>= 8;
  }
  hashBytes(hasher, encoded.data(), encoded.size());
  hashBytes(hasher, value.data(), value.size());
}

VfsDigest finishHash(blake3_hasher& hasher)
{
  VfsDigest digest{};
  static_assert(std::tuple_size_v<VfsDigest> == BLAKE3_OUT_LEN);
  blake3_hasher_finalize(&hasher, digest.data(), digest.size());
  return digest;
}

void hashU64(blake3_hasher& hasher, uint64_t value)
{
  std::array<unsigned char, sizeof(value)> encoded{};
  for (size_t i = 0; i < encoded.size(); ++i) {
    encoded[i] = static_cast<unsigned char>(value & 0xffu);
    value >>= 8;
  }
  hashBytes(hasher, encoded.data(), encoded.size());
}

// A local cache identity, not a digest of archive contents. The filesystem
// fingerprint is also checked around parsing so a changing archive cannot
// publish its member list under an earlier identity.
VfsDigest archiveIdentity(const std::string& path, const struct stat& metadata)
{
  blake3_hasher hasher;
  blake3_hasher_init(&hasher);
  constexpr char domain[] = "fluorine.vfs.archive-metadata.v1";
  hashBytes(hasher, domain, sizeof(domain) - 1);
  hashString(hasher, catalogRootIdentity(path));
  hashU64(hasher, metadata.st_dev);
  hashU64(hasher, metadata.st_ino);
  hashU64(hasher, metadata.st_size);
  hashU64(hasher, metadata.st_mode & 07777);
  hashU64(hasher, timespecNs(metadata.st_mtim));
  hashU64(hasher, timespecNs(metadata.st_ctim));
  return finishHash(hasher);
}

VfsDigest archiveSetDigest(
    const std::set<VfsDigest>& digests,
    std::stop_token stop_token = {})
{
  blake3_hasher hasher;
  blake3_hasher_init(&hasher);
  constexpr char domain[] = "fluorine.vfs.archive-set.v1";
  hashBytes(hasher, domain, sizeof(domain) - 1);
  hashU64(hasher, static_cast<uint64_t>(digests.size()));
  for (const VfsDigest& digest : digests) {
    throwIfCancelled(stop_token);
    hashBytes(hasher, digest.data(), digest.size());
  }
  return finishHash(hasher);
}

VfsDigest archiveMembershipCacheDigest(
    size_t bitCount, size_t archiveCount, size_t memberCount,
    const std::vector<unsigned char>& bits,
    std::stop_token stop_token = {})
{
  blake3_hasher hasher;
  blake3_hasher_init(&hasher);
  constexpr char domain[] = "fluorine.vfs.archive-membership-cache.v1";
  hashBytes(hasher, domain, sizeof(domain) - 1);
  hashU64(hasher, VfsArchiveMemberIndex::probeCount());
  hashU64(hasher, static_cast<uint64_t>(bitCount));
  hashU64(hasher, static_cast<uint64_t>(archiveCount));
  hashU64(hasher, static_cast<uint64_t>(memberCount));
  hashBytesCancellable(hasher, bits.data(), bits.size(), stop_token);
  return finishHash(hasher);
}

struct MerkleNode
{
  std::map<std::string, MerkleNode> directories;
  std::map<std::string, VfsDigest> files;
};

VfsDigest hashMerkleDirectory(const MerkleNode& node,
                              std::stop_token stop_token = {})
{
  blake3_hasher hasher;
  blake3_hasher_init(&hasher);
  constexpr char domain[] = "fluorine.vfs.directory.v1";
  hashBytes(hasher, domain, sizeof(domain) - 1);

  for (const auto& [name, directory] : node.directories) {
    throwIfCancelled(stop_token);
    const unsigned char type = 1;
    const VfsDigest child = hashMerkleDirectory(directory, stop_token);
    hashBytes(hasher, &type, sizeof(type));
    hashString(hasher, name);
    hashBytes(hasher, child.data(), child.size());
  }
  for (const auto& [name, digest] : node.files) {
    throwIfCancelled(stop_token);
    const unsigned char type = 0;
    hashBytes(hasher, &type, sizeof(type));
    hashString(hasher, name);
    hashBytes(hasher, digest.data(), digest.size());
  }
  return finishHash(hasher);
}

VfsProviderRoot calculateProviderRoot(
    sqlite3* db, const Root& root, std::stop_token stop_token = {})
{
  auto rows = prepare(db,
      "SELECT normalized_path,device,inode,size,mode,mtime_ns,ctime_ns,blake3"
      " FROM catalog_files"
      " WHERE root_key=?1 ORDER BY normalized_path;");
  bindText(db, rows.get(), 1, root.key);

  MerkleNode merkle;
  uint64_t fileCount = 0;
  while (sqlite3_step(rows.get()) == SQLITE_ROW) {
    throwIfCancelled(stop_token);
    const auto* path = reinterpret_cast<const char*>(sqlite3_column_text(rows.get(), 0));
    if (path == nullptr) continue;

    const std::string normalized(path);
    const auto parts = splitPath(normalized);
    if (parts.empty()) continue;
    MerkleNode* directory = &merkle;
    for (size_t i = 0; i + 1 < parts.size(); ++i) {
      directory = &directory->directories[parts[i]];
    }

    blake3_hasher leaf;
    blake3_hasher_init(&leaf);
    constexpr char domain[] = "fluorine.vfs.file-metadata.v1";
    hashBytes(leaf, domain, sizeof(domain) - 1);
    hashString(leaf, normalized);
    for (int column = 1; column <= 6; ++column) {
      hashU64(leaf, static_cast<uint64_t>(sqlite3_column_int64(rows.get(), column)));
    }
    const void* blob = sqlite3_column_blob(rows.get(), 7);
    const int bytes = sqlite3_column_bytes(rows.get(), 7);
    const uint64_t hasDigest = blob != nullptr && bytes == BLAKE3_OUT_LEN;
    hashU64(leaf, hasDigest);
    if (hasDigest) hashBytes(leaf, blob, BLAKE3_OUT_LEN);
    directory->files[parts.back()] = finishHash(leaf);
    ++fileCount;
  }

  return {root.key, root.origin, root.backing, fileCount,
          hashMerkleDirectory(merkle, stop_token)};
}

VfsDigest calculateProfileRoot(
    const std::vector<VfsProviderRoot>& roots,
    std::stop_token stop_token = {})
{
  blake3_hasher hasher;
  blake3_hasher_init(&hasher);
  constexpr char domain[] = "fluorine.vfs.profile.v1";
  hashBytes(hasher, domain, sizeof(domain) - 1);
  for (const auto& root : roots) {
    throwIfCancelled(stop_token);
    const unsigned char role = root.is_backing ? 1 : 0;
    hashBytes(hasher, &role, sizeof(role));
    hashString(hasher, root.origin);
    hashBytes(hasher, root.digest.data(), root.digest.size());
  }
  return finishHash(hasher);
}

void initializeSchema(sqlite3* db)
{
  exec(db, "PRAGMA journal_mode=WAL;");
  exec(db, "PRAGMA synchronous=NORMAL;");
  exec(db, "PRAGMA foreign_keys=ON;");
  exec(db, "PRAGMA temp_store=MEMORY;");
  // Catalog connections are short-lived. Map database pages directly into the
  // process on demand and let sqlite3_close() release the mapping after the VFS
  // generation has been built. This avoids a second full heap copy of large
  // catalogs while still allowing the OS to reclaim clean pages immediately.
  exec(db, "PRAGMA mmap_size=1073741824;");
  exec(db, "PRAGMA cache_size=-32768;");
  exec(db,
       "CREATE TABLE IF NOT EXISTS catalog_meta("
       " key TEXT PRIMARY KEY, value INTEGER NOT NULL);"
       "CREATE TABLE IF NOT EXISTS catalog_files("
       " root_key TEXT NOT NULL, relative_path TEXT NOT NULL,"
       " normalized_path TEXT NOT NULL, origin TEXT NOT NULL,"
       " is_backing INTEGER NOT NULL, device INTEGER NOT NULL,"
       " inode INTEGER NOT NULL, size INTEGER NOT NULL, mode INTEGER NOT NULL,"
       " mtime_ns INTEGER NOT NULL, ctime_ns INTEGER NOT NULL,"
       " blake3 BLOB NOT NULL, seen_generation INTEGER NOT NULL,"
       " PRIMARY KEY(root_key, normalized_path));"
       "CREATE INDEX IF NOT EXISTS catalog_files_seen"
       " ON catalog_files(root_key, seen_generation);");
  exec(db,
       "CREATE TABLE IF NOT EXISTS catalog_roots("
       " root_key TEXT PRIMARY KEY, merkle_root BLOB NOT NULL,"
       " file_count INTEGER NOT NULL, generation INTEGER NOT NULL);");
  exec(db,
       "CREATE TABLE IF NOT EXISTS archive_catalogs("
       " digest BLOB PRIMARY KEY, member_count INTEGER NOT NULL,"
       " error TEXT NOT NULL DEFAULT '');"
       "CREATE TABLE IF NOT EXISTS archive_members("
       " digest BLOB NOT NULL, relative_path TEXT NOT NULL,"
       " normalized_path TEXT NOT NULL,"
       " PRIMARY KEY(digest,normalized_path)) WITHOUT ROWID;");
  exec(db,
       "CREATE TABLE IF NOT EXISTS archive_membership_cache("
       " archive_set_digest BLOB PRIMARY KEY,"
       " probe_count INTEGER NOT NULL,bit_count INTEGER NOT NULL,"
       " archive_count INTEGER NOT NULL,member_count INTEGER NOT NULL,"
       " bits BLOB NOT NULL,bits_digest BLOB NOT NULL);");
  exec(db,
       "CREATE TEMP TABLE IF NOT EXISTS catalog_seen("
       " root_key TEXT NOT NULL, normalized_path TEXT NOT NULL,"
       " PRIMARY KEY(root_key,normalized_path)) WITHOUT ROWID;");
  exec(db,
       "CREATE TEMP TABLE IF NOT EXISTS enabled_provider_priority("
       " root_key TEXT PRIMARY KEY,priority INTEGER NOT NULL) WITHOUT ROWID;");

  auto current = prepare(db,
      "SELECT value FROM catalog_meta WHERE key='schema_version';");
  const int currentVersion = sqlite3_step(current.get()) == SQLITE_ROW
                                 ? sqlite3_column_int(current.get(), 0) : 0;
  current.reset();
  if (currentVersion > kSchemaVersion) {
    throw std::runtime_error("VFS catalog was created by a newer Fluorine version");
  }
  if (currentVersion == kSchemaVersion) return;
  exec(db, "BEGIN IMMEDIATE;");
  try {
    // Preserve file fingerprints and any verified hashes. Only derived cache
    // identities changed, so upgrading never requires reading file contents.
    exec(db, "DELETE FROM catalog_roots; DELETE FROM archive_catalogs;"
             "DELETE FROM archive_members; DELETE FROM archive_membership_cache;");
    auto stmt = prepare(db,
        "INSERT INTO catalog_meta(key,value) VALUES('schema_version',?1)"
        " ON CONFLICT(key) DO UPDATE SET value=excluded.value;");
    sqlite3_bind_int(stmt.get(), 1, kSchemaVersion);
    if (sqlite3_step(stmt.get()) != SQLITE_DONE) throwDb(db, "Writing catalog schema");
    exec(db, "COMMIT;");
  } catch (...) {
    sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr);
    throw;
  }
}

int64_t nextGeneration(sqlite3* db)
{
  exec(db, "INSERT OR IGNORE INTO catalog_meta(key,value) VALUES('generation',0);");
  exec(db, "UPDATE catalog_meta SET value=value+1 WHERE key='generation';");
  auto stmt = prepare(db, "SELECT value FROM catalog_meta WHERE key='generation';");
  if (sqlite3_step(stmt.get()) != SQLITE_ROW) throwDb(db, "Reading catalog generation");
  return sqlite3_column_int64(stmt.get(), 0);
}

std::shared_ptr<const VfsArchiveMemberIndex> reconcileArchiveManifests(
    sqlite3* db,
    const std::map<VfsDigest, std::string>& archive_candidates,
    const std::map<std::string, VfsDigest>& visible_archives,
    VfsCatalogProgress& state,
    std::stop_token stop_token)
{
  throwIfCancelled(stop_token);
  auto findCatalog = prepare(db,
      "SELECT member_count,error FROM archive_catalogs WHERE digest=?1;");
  std::vector<ArchiveCandidate> uncached;
  uncached.reserve(archive_candidates.size());
  for (const auto& [digest, path] : archive_candidates) {
    throwIfCancelled(stop_token);
    sqlite3_reset(findCatalog.get());
    sqlite3_clear_bindings(findCatalog.get());
    bindDigest(db, findCatalog.get(), 1, digest);
    if (sqlite3_step(findCatalog.get()) == SQLITE_ROW) {
      ++state.archives_reused;
      const auto* error = reinterpret_cast<const char*>(
          sqlite3_column_text(findCatalog.get(), 1));
      if (error != nullptr && *error != '\0') ++state.archive_errors;
      continue;
    }
    uncached.push_back({path, digest});
  }

#ifdef FLUORINE_HAS_BSA_FFI
  if (!uncached.empty()) {
    std::atomic<std::size_t> next{0};
    std::atomic<uint64_t> indexed{0};
    std::atomic<uint64_t> errors{0};
    std::mutex dbMutex;
    std::mutex errorMutex;
    std::exception_ptr workerError;
    std::atomic<bool> stop{false};
    const auto recordWorkerError = [&](std::exception_ptr error) {
      {
        std::scoped_lock lock(errorMutex);
        if (workerError == nullptr) workerError = std::move(error);
      }
      stop.store(true, std::memory_order_relaxed);
    };

    const unsigned int hardware =
        std::max(1u, std::thread::hardware_concurrency());
    const std::size_t workerCount =
        std::min<std::size_t>(
            uncached.size(), std::min<unsigned int>(hardware, 4u));
    state.archive_workers =
        std::max<uint64_t>(state.archive_workers, workerCount);
    std::vector<std::thread> workers;
    workers.reserve(workerCount);
    ThreadJoinGuard joinGuard(workers, stop);
    try {
      for (std::size_t worker = 0; worker < workerCount; ++worker) {
        throwIfCancelled(stop_token);
        workers.emplace_back([&]() {
          while (!stop.load(std::memory_order_relaxed)) {
            try {
              throwIfCancelled(stop_token);
              const std::size_t index =
                  next.fetch_add(1, std::memory_order_relaxed);
              if (index >= uncached.size()) return;
              const ArchiveCandidate& candidate = uncached[index];
              const auto checkIdentity = [&] {
                struct stat metadata {};
                if (::lstat(candidate.real_path.c_str(), &metadata) != 0 ||
                    !S_ISREG(metadata.st_mode) ||
                    archiveIdentity(candidate.real_path, metadata) != candidate.digest) {
                  throw std::runtime_error("Archive changed while indexing: " +
                                           candidate.real_path);
                }
              };
              checkIdentity();
              BsaFfiStringList list =
                  bsa_ffi_list_files(candidate.real_path.c_str());
              struct StringListGuard
              {
                BsaFfiStringList& list;
                ~StringListGuard() { bsa_ffi_string_list_free(list); }
              } listGuard{list};
              checkIdentity();
              // The archive parser is a third-party call and cannot be
              // interrupted internally. Check the request as soon as it ends.
              throwIfHashStopped(stop_token, &stop);
              const std::string parseError =
                  list.error != nullptr ? list.error : "";

              {
                std::scoped_lock lock(dbMutex);
                throwIfHashStopped(stop_token, &stop);
                auto removeMembers = prepare(
                    db, "DELETE FROM archive_members WHERE digest=?1;");
                bindDigest(db, removeMembers.get(), 1, candidate.digest);
                if (sqlite3_step(removeMembers.get()) != SQLITE_DONE) {
                  throwDb(db, "Clearing archive manifest members");
                }

                uint64_t memberCount = 0;
                if (parseError.empty()) {
                  auto insertMember = prepare(db,
                      "INSERT OR IGNORE INTO archive_members("
                      "digest,relative_path,normalized_path)"
                      " VALUES(?1,?2,?3);");
                  for (std::size_t item = 0; item < list.count; ++item) {
                    throwIfHashStopped(stop_token, &stop);
                    if (list.items == nullptr || list.items[item] == nullptr) {
                      continue;
                    }
                    std::string relative(list.items[item]);
                    std::replace(relative.begin(), relative.end(), '\\', '/');
                    const std::string normalized =
                        normalizeForLookup(relative);
                    if (normalized.empty()) continue;
                    sqlite3_reset(insertMember.get());
                    sqlite3_clear_bindings(insertMember.get());
                    bindDigest(db, insertMember.get(), 1, candidate.digest);
                    bindText(db, insertMember.get(), 2, relative);
                    bindText(db, insertMember.get(), 3, normalized);
                    if (sqlite3_step(insertMember.get()) != SQLITE_DONE) {
                      throwDb(db, "Adding archive manifest member");
                    }
                    if (sqlite3_changes(db) != 0) ++memberCount;
                  }
                }

                throwIfHashStopped(stop_token, &stop);
                auto insertCatalog = prepare(db,
                    "INSERT OR REPLACE INTO archive_catalogs("
                    "digest,member_count,error) VALUES(?1,?2,?3);");
                bindDigest(db, insertCatalog.get(), 1, candidate.digest);
                sqlite3_bind_int64(insertCatalog.get(), 2,
                                   static_cast<sqlite3_int64>(memberCount));
                bindText(db, insertCatalog.get(), 3, parseError);
                if (sqlite3_step(insertCatalog.get()) != SQLITE_DONE) {
                  throwDb(db, "Publishing archive manifest");
                }
                indexed.fetch_add(1, std::memory_order_relaxed);
                if (!parseError.empty()) {
                  errors.fetch_add(1, std::memory_order_relaxed);
                }
              }
            } catch (...) {
              if (stop_token.stop_requested()) {
                recordWorkerError(std::current_exception());
                return;
              }
              if (stop.load(std::memory_order_relaxed)) return;
              recordWorkerError(std::current_exception());
              return;
            }
          }
        });
      }
    } catch (...) {
      stop.store(true, std::memory_order_relaxed);
      joinGuard.join();
      throw;
    }
    joinGuard.join();
    throwIfCancelled(stop_token);
    if (workerError != nullptr) std::rethrow_exception(workerError);
    state.archives_indexed += indexed.load(std::memory_order_relaxed);
    state.archive_errors += errors.load(std::memory_order_relaxed);
  }
#else
  // Archive support is optional at build time. Keep the filesystem catalog
  // usable without pretending the archives were successfully indexed.
  state.archive_errors += uncached.size();
#endif

  std::set<VfsDigest> visibleDigests;
  for (const auto& [path, digest] : visible_archives) {
    throwIfCancelled(stop_token);
    (void)path;
    visibleDigests.insert(digest);
  }

  std::size_t memberCount = 0;
  std::size_t archiveCount = 0;
  for (const VfsDigest& digest : visibleDigests) {
    throwIfCancelled(stop_token);
    sqlite3_reset(findCatalog.get());
    sqlite3_clear_bindings(findCatalog.get());
    bindDigest(db, findCatalog.get(), 1, digest);
    if (sqlite3_step(findCatalog.get()) != SQLITE_ROW) continue;
    const auto* error = reinterpret_cast<const char*>(
        sqlite3_column_text(findCatalog.get(), 1));
    if (error != nullptr && *error != '\0') continue;
    memberCount += static_cast<std::size_t>(
        sqlite3_column_int64(findCatalog.get(), 0));
    ++archiveCount;
  }

  // The archive membership filter is the expensive part of a warm mount: a
  // large list can contain hundreds of thousands of paths even when every BSA
  // manifest itself is cached. Key the serialized filter by the exact set of
  // visible archive content digests so an unchanged set can be copied directly
  // into its compact in-memory representation without replaying every row.
  const bool manifestsComplete = archiveCount == visibleDigests.size();
  const VfsDigest setDigest = archiveSetDigest(visibleDigests, stop_token);
  if (manifestsComplete) {
    throwIfCancelled(stop_token);
    auto cached = prepare(db,
        "SELECT probe_count,bit_count,archive_count,member_count,bits,bits_digest"
        " FROM archive_membership_cache WHERE archive_set_digest=?1;");
    bindDigest(db, cached.get(), 1, setDigest);
    if (sqlite3_step(cached.get()) == SQLITE_ROW) {
      const sqlite3_int64 probeCount = sqlite3_column_int64(cached.get(), 0);
      const sqlite3_int64 bitCount = sqlite3_column_int64(cached.get(), 1);
      const sqlite3_int64 cachedArchiveCount =
          sqlite3_column_int64(cached.get(), 2);
      const sqlite3_int64 cachedMemberCount =
          sqlite3_column_int64(cached.get(), 3);
      const int byteCount = sqlite3_column_bytes(cached.get(), 4);
      const auto* rawBits = static_cast<const unsigned char*>(
          sqlite3_column_blob(cached.get(), 4));
      const void* rawDigest = sqlite3_column_blob(cached.get(), 5);
      const int digestBytes = sqlite3_column_bytes(cached.get(), 5);
      const bool shapeValid =
          probeCount == static_cast<sqlite3_int64>(
                            VfsArchiveMemberIndex::probeCount()) &&
          bitCount >= 64 &&
          cachedArchiveCount == static_cast<sqlite3_int64>(archiveCount) &&
          cachedMemberCount == static_cast<sqlite3_int64>(memberCount) &&
          byteCount > 0 &&
          static_cast<size_t>(byteCount) <=
              kMaximumArchiveMembershipCacheBytes &&
          static_cast<uint64_t>(byteCount) ==
              ((static_cast<uint64_t>(bitCount) + 63) / 64) * sizeof(uint64_t) &&
          rawBits != nullptr && rawDigest != nullptr &&
          digestBytes == static_cast<int>(VfsDigest{}.size());
      if (shapeValid) {
        std::vector<unsigned char> bits(
            rawBits, rawBits + static_cast<size_t>(byteCount));
        VfsDigest storedDigest{};
        std::memcpy(storedDigest.data(), rawDigest, storedDigest.size());
        if (archiveMembershipCacheDigest(
                static_cast<size_t>(bitCount), archiveCount, memberCount,
                bits, stop_token) == storedDigest) {
          throwIfCancelled(stop_token);
          auto index = VfsArchiveMemberIndex::fromSerialized(
              static_cast<size_t>(bitCount), archiveCount, memberCount, bits);
          if (index) {
            throwIfCancelled(stop_token);
            state.archive_members = memberCount;
            state.archive_membership_cache_hits = 1;
            state.archive_membership_cache_bytes = bits.size();
            return index;
          }
        }
      }
      // A malformed local cache is never trusted as an absence proof. Remove
      // it and rebuild from authoritative archive member rows below.
      cached.reset();
      auto remove = prepare(db,
          "DELETE FROM archive_membership_cache WHERE archive_set_digest=?1;");
      bindDigest(db, remove.get(), 1, setDigest);
      if (sqlite3_step(remove.get()) != SQLITE_DONE) {
        throwDb(db, "Removing invalid archive membership cache");
      }
    }
  }

  auto index = std::make_shared<VfsArchiveMemberIndex>(
      memberCount, archiveCount, manifestsComplete);
  auto members = prepare(db,
      "SELECT normalized_path FROM archive_members WHERE digest=?1;");
  for (const VfsDigest& digest : visibleDigests) {
    throwIfCancelled(stop_token);
    sqlite3_reset(members.get());
    sqlite3_clear_bindings(members.get());
    bindDigest(db, members.get(), 1, digest);
    while (sqlite3_step(members.get()) == SQLITE_ROW) {
      throwIfCancelled(stop_token);
      const auto* path = reinterpret_cast<const char*>(
          sqlite3_column_text(members.get(), 0));
      if (path != nullptr) index->add(path);
    }
  }
  state.archive_members = memberCount;

  if (manifestsComplete) {
    throwIfCancelled(stop_token);
    const std::vector<unsigned char> bits = index->serializedBits();
    const VfsDigest bitsDigest = archiveMembershipCacheDigest(
        index->bitCount(), archiveCount, memberCount, bits, stop_token);
    auto store = prepare(db,
        "INSERT INTO archive_membership_cache("
        "archive_set_digest,probe_count,bit_count,archive_count,member_count,"
        "bits,bits_digest) VALUES(?1,?2,?3,?4,?5,?6,?7)"
        " ON CONFLICT(archive_set_digest) DO UPDATE SET"
        " probe_count=excluded.probe_count,bit_count=excluded.bit_count,"
        " archive_count=excluded.archive_count,member_count=excluded.member_count,"
        " bits=excluded.bits,bits_digest=excluded.bits_digest;");
    bindDigest(db, store.get(), 1, setDigest);
    sqlite3_bind_int64(store.get(), 2,
                       VfsArchiveMemberIndex::probeCount());
    sqlite3_bind_int64(store.get(), 3,
                       static_cast<sqlite3_int64>(index->bitCount()));
    sqlite3_bind_int64(store.get(), 4,
                       static_cast<sqlite3_int64>(archiveCount));
    sqlite3_bind_int64(store.get(), 5,
                       static_cast<sqlite3_int64>(memberCount));
    if (sqlite3_bind_blob(store.get(), 6, bits.data(),
                          static_cast<int>(bits.size()),
                          SQLITE_TRANSIENT) != SQLITE_OK) {
      throwDb(db, "Binding archive membership cache bits");
    }
    bindDigest(db, store.get(), 7, bitsDigest);
    if (sqlite3_step(store.get()) != SQLITE_DONE) {
      throwDb(db, "Storing archive membership cache");
    }
    state.archive_membership_cache_bytes = bits.size();
  }
  return index;
}

}  // namespace

VfsCatalog::VfsCatalog(fs::path database_path, VfsCatalogMode mode)
    : m_database_path(std::move(database_path)), m_mode(mode)
{}

fs::path VfsCatalog::databasePath(const std::string& data_dir)
{
  char name[48];
  std::snprintf(name, sizeof(name), "%016llx.catalog.sqlite",
                static_cast<unsigned long long>(
                    fnv1a(catalogRootIdentity(data_dir))));
  return fs::path(fluorineVfsCacheDir().toStdString()) / name;
}

VfsCatalogResult VfsCatalog::reconcileAndBuild(
    const std::string& data_dir,
    const std::vector<std::pair<std::string, std::string>>& mods,
    const std::string& overwrite_dir,
    bool scan_base,
    ProgressCallback progress,
    std::stop_token stop_token)
{
  throwIfCancelled(stop_token);
  const auto scanStart = std::chrono::steady_clock::now();
  std::error_code ec;
  fs::create_directories(m_database_path.parent_path(), ec);
  if (ec) {
    throw std::runtime_error("Unable to create local VFS catalog directory: " +
                             ec.message());
  }
  throwIfCancelled(stop_token);

  sqlite3* raw = nullptr;
  if (sqlite3_open_v2(m_database_path.c_str(), &raw,
                      SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE |
                          SQLITE_OPEN_FULLMUTEX,
                      nullptr) != SQLITE_OK) {
    DbPtr failed(raw);
    throwDb(raw, "Opening local VFS catalog");
  }
  DbPtr db(raw);
  sqlite3_busy_timeout(db.get(), 30000);
  initializeSchema(db.get());
  throwIfCancelled(stop_token);
  exec(db.get(), "BEGIN IMMEDIATE;");

  VfsCatalogProgress state;
  bool progressCallbackFailed = false;
  std::function<void()> reportProgress;
  try {
    throwIfCancelled(stop_token);
    const int64_t generation = nextGeneration(db.get());
    VfsTree tree;
    tree.root.is_directory = true;
    tree.dir_count = 1;
    std::map<VfsDigest, std::string> archiveCandidates;
    std::map<std::string, VfsDigest> visibleArchives;

    std::vector<Root> roots;
    std::unordered_map<std::string, size_t> modIndexByRoot;
    const std::string dataRootKey = catalogRootIdentity(data_dir);
    const std::string overwriteRootKey = catalogRootIdentity(overwrite_dir);
    if (scan_base) {
      roots.push_back({dataRootKey, data_dir, "_base_game", true});
    }
    for (size_t index = 0; index < mods.size(); ++index) {
      throwIfCancelled(stop_token);
      const auto& [name, path] = mods[index];
      const std::string rootKey = catalogRootIdentity(path);
      roots.push_back({rootKey, path, name, false});
      modIndexByRoot.insert_or_assign(rootKey, index);
    }
    roots.push_back(
        {overwriteRootKey, overwrite_dir, "Overwrite", false, true});

    if (!scan_base) {
      auto base = prepare(db.get(),
          "SELECT relative_path,size,mtime_ns,mode,blake3,device,inode,ctime_ns"
          " FROM catalog_files"
          " WHERE root_key=?1 ORDER BY normalized_path;");
      bindText(db.get(), base.get(), 1, dataRootKey);
      while (sqlite3_step(base.get()) == SQLITE_ROW) {
        throwIfCancelled(stop_token);
        const auto* rel = reinterpret_cast<const char*>(sqlite3_column_text(base.get(), 0));
        if (rel == nullptr) continue;
        const std::string relative(rel);
        tree.root.insertFile(
            splitPath(relative), relative,
            static_cast<uint64_t>(sqlite3_column_int64(base.get(), 1)),
            std::chrono::system_clock::time_point(
                std::chrono::nanoseconds(sqlite3_column_int64(base.get(), 2))),
            "_base_game", true,
            static_cast<mode_t>(sqlite3_column_int64(base.get(), 3)),
            [&]() -> std::optional<VfsDigest> {
              const void* blob = sqlite3_column_blob(base.get(), 4);
              const int bytes = sqlite3_column_bytes(base.get(), 4);
              if (blob == nullptr || bytes != BLAKE3_OUT_LEN) return std::nullopt;
              VfsDigest value{};
              std::memcpy(value.data(), blob, value.size());
              return value;
            }());
        ++tree.file_count;
        if (isBethesdaArchive(relative)) {
          struct stat metadata {};
          metadata.st_size = sqlite3_column_int64(base.get(), 1);
          metadata.st_mode = sqlite3_column_int64(base.get(), 3);
          metadata.st_dev = sqlite3_column_int64(base.get(), 5);
          metadata.st_ino = sqlite3_column_int64(base.get(), 6);
          const int64_t mtime = sqlite3_column_int64(base.get(), 2);
          const int64_t ctime = sqlite3_column_int64(base.get(), 7);
          metadata.st_mtim = {mtime / 1000000000LL, mtime % 1000000000LL};
          metadata.st_ctim = {ctime / 1000000000LL, ctime % 1000000000LL};
          const std::string full = (fs::path(data_dir) / relative).string();
          const VfsDigest identity = archiveIdentity(full, metadata);
          archiveCandidates.insert_or_assign(identity, full);
          visibleArchives.insert_or_assign(normalizeForLookup(relative), identity);
          ++state.archives_discovered;
        }
      }
    }

    auto loadRootFiles = prepare(db.get(),
        "SELECT normalized_path,device,inode,size,mode,mtime_ns,ctime_ns,blake3"
        " FROM catalog_files WHERE root_key=?1;");
    auto upsert = prepare(db.get(),
        "INSERT INTO catalog_files(root_key,relative_path,normalized_path,origin,"
        "is_backing,device,inode,size,mode,mtime_ns,ctime_ns,blake3,seen_generation)"
        " VALUES(?1,?2,?3,?4,?5,?6,?7,?8,?9,?10,?11,?12,?13)"
        " ON CONFLICT(root_key,normalized_path) DO UPDATE SET"
        " relative_path=excluded.relative_path,origin=excluded.origin,"
        " is_backing=excluded.is_backing,device=excluded.device,inode=excluded.inode,"
        " size=excluded.size,mode=excluded.mode,mtime_ns=excluded.mtime_ns,"
        " ctime_ns=excluded.ctime_ns,blake3=excluded.blake3,"
        " seen_generation=excluded.seen_generation;");
    auto refreshRootOrigin = prepare(db.get(),
        "UPDATE catalog_files SET origin=?2,is_backing=?3"
        " WHERE root_key=?1 AND (origin<>?2 OR is_backing<>?3);");
    auto deleteFile = prepare(db.get(),
        "DELETE FROM catalog_files"
        " WHERE root_key=?1 AND normalized_path=?2;");
    auto findRoot = prepare(db.get(),
        "SELECT merkle_root,file_count FROM catalog_roots WHERE root_key=?1;");
    auto upsertRoot = prepare(db.get(),
        "INSERT INTO catalog_roots(root_key,merkle_root,file_count,generation)"
        " VALUES(?1,?2,?3,?4) ON CONFLICT(root_key) DO UPDATE SET"
        " merkle_root=excluded.merkle_root,file_count=excluded.file_count,"
        " generation=excluded.generation;");

    std::vector<VfsProviderRoot> providerRoots;
    providerRoots.reserve(roots.size() + (scan_base ? 0 : 1));
    std::vector<VfsCatalogProviderSummary> providerSummaries;
    providerSummaries.reserve(roots.size() + (scan_base ? 0 : 1));
    if (!scan_base) {
      sqlite3_reset(findRoot.get());
      sqlite3_clear_bindings(findRoot.get());
      bindText(db.get(), findRoot.get(), 1, dataRootKey);
      VfsProviderRoot baseRoot{
          dataRootKey, "_base_game", true, 0, {}};
      if (sqlite3_step(findRoot.get()) == SQLITE_ROW) {
        const void* digest = sqlite3_column_blob(findRoot.get(), 0);
        const int bytes = sqlite3_column_bytes(findRoot.get(), 0);
        if (digest != nullptr &&
            bytes == static_cast<int>(baseRoot.digest.size())) {
          std::memcpy(
              baseRoot.digest.data(), digest, baseRoot.digest.size());
          baseRoot.file_count = static_cast<uint64_t>(
              sqlite3_column_int64(findRoot.get(), 1));
          ++state.merkle_roots_reused;
        } else {
          baseRoot = calculateProviderRoot(
              db.get(), Root{dataRootKey, data_dir, "_base_game", true},
              stop_token);
        }
      } else {
        baseRoot = calculateProviderRoot(
            db.get(), Root{dataRootKey, data_dir, "_base_game", true},
            stop_token);
      }
      providerRoots.push_back(std::move(baseRoot));
      VfsCatalogProviderSummary baseSummary;
      baseSummary.root_key = dataRootKey;
      baseSummary.origin = "_base_game";
      // The backing root is supplied by the prior catalog generation when
      // scan_base is disabled, so it has no work in this reconciliation.
      baseSummary.completed = true;
      providerSummaries.push_back(std::move(baseSummary));
    }
    reportProgress = [&]() {
      state.elapsed_ms = static_cast<uint64_t>(
          std::chrono::duration_cast<std::chrono::milliseconds>(
              std::chrono::steady_clock::now() - scanStart).count());
      state.hash_mib_per_second = state.elapsed_ms == 0 ? 0.0 :
          (static_cast<double>(state.bytes_hashed) / (1024.0 * 1024.0)) /
          (static_cast<double>(state.elapsed_ms) / 1000.0);
      if (progress) {
        try {
          progress(state);
        } catch (...) {
          progressCallbackFailed = true;
          throw;
        }
      }
    };
    const auto providersStart = std::chrono::steady_clock::now();
    state.phase = VfsCatalogPhase::Metadata;
    reportProgress();
    for (const Root& root : roots) {
      throwIfCancelled(stop_token);
      state.phase = VfsCatalogPhase::Metadata;
      state.current_root = root.path;
      state.current_provider = {};
      state.current_provider.root_key = root.key;
      state.current_provider.origin = root.origin;
      reportProgress();
      throwIfCancelled(stop_token);
      const std::string rootPrefix = fs::path(root.path).string();
      std::vector<PendingCatalogFile> pendingFiles;
      std::unordered_map<std::string, CachedCatalogFile> cachedFiles;
      std::unordered_set<std::string> seenFiles;

      sqlite3_reset(loadRootFiles.get());
      sqlite3_clear_bindings(loadRootFiles.get());
      bindText(db.get(), loadRootFiles.get(), 1, root.key);
      while (sqlite3_step(loadRootFiles.get()) == SQLITE_ROW) {
        throwIfCancelled(stop_token);
        const auto* normalized = reinterpret_cast<const char*>(
            sqlite3_column_text(loadRootFiles.get(), 0));
        if (normalized == nullptr) continue;
        CachedCatalogFile cached;
        cached.device = sqlite3_column_int64(loadRootFiles.get(), 1);
        cached.inode = sqlite3_column_int64(loadRootFiles.get(), 2);
        cached.size = sqlite3_column_int64(loadRootFiles.get(), 3);
        cached.mode = sqlite3_column_int64(loadRootFiles.get(), 4);
        cached.mtime_ns = sqlite3_column_int64(loadRootFiles.get(), 5);
        cached.ctime_ns = sqlite3_column_int64(loadRootFiles.get(), 6);
        const void* digest = sqlite3_column_blob(loadRootFiles.get(), 7);
        const int bytes = sqlite3_column_bytes(loadRootFiles.get(), 7);
        cached.has_digest =
            digest != nullptr &&
            bytes == static_cast<int>(cached.digest.size());
        if (cached.has_digest) {
          std::memcpy(cached.digest.data(), digest, cached.digest.size());
        }
        cachedFiles.emplace(normalized, cached);
        ++state.catalog_rows_loaded;
        ++state.current_provider.catalog_rows_loaded;
      }
      seenFiles.reserve(cachedFiles.size());
      pendingFiles.reserve(cachedFiles.size());

      sqlite3_reset(findRoot.get());
      sqlite3_clear_bindings(findRoot.get());
      bindText(db.get(), findRoot.get(), 1, root.key);
      VfsDigest previousRootDigest{};
      uint64_t previousRootFileCount = 0;
      bool hasPreviousRoot = false;
      if (sqlite3_step(findRoot.get()) == SQLITE_ROW) {
        const void* digest = sqlite3_column_blob(findRoot.get(), 0);
        const int bytes = sqlite3_column_bytes(findRoot.get(), 0);
        hasPreviousRoot =
            digest != nullptr &&
            bytes == static_cast<int>(previousRootDigest.size());
        if (hasPreviousRoot) {
          std::memcpy(
              previousRootDigest.data(), digest, previousRootDigest.size());
          previousRootFileCount = static_cast<uint64_t>(
              sqlite3_column_int64(findRoot.get(), 1));
        }
      }

      struct stat rootMetadata {};
      bool rootExists = true;
      if (::stat(root.path.c_str(), &rootMetadata) != 0) {
        const int saved = errno;
        if (saved == ENOENT && root.missing_is_empty) {
          // An absent Overwrite directory is a valid empty provider. Enabled
          // mod roots and the game's backing Data root must remain available;
          // treating a missing configured root as empty could silently drop
          // content and prune its reusable cache rows.
          rootExists = false;
        } else if (saved == ENOENT) {
          throw std::runtime_error(
              "Enabled VFS provider root is missing: " + root.path);
        } else {
          throw std::runtime_error("Unable to inspect VFS provider " + root.path +
                                   ": " + std::strerror(saved));
        }
      } else if (!S_ISDIR(rootMetadata.st_mode)) {
        throw std::runtime_error("VFS provider root is not a directory: " +
                                 root.path);
      }

      if (rootExists) {
        ec.clear();
        fs::recursive_directory_iterator it(
            root.path, fs::directory_options::none, ec);
        const fs::recursive_directory_iterator end;
        if (ec) {
          throw std::runtime_error("Unable to begin scanning VFS provider " +
                                   root.path + ": " + ec.message());
        }
        while (it != end) {
          throwIfCancelled(stop_token);
          const fs::directory_entry& entry = *it;
          const std::string full = entry.path().string();
          const std::string relative = fastRelative(full, rootPrefix);
          if (!relative.empty() && relative != "meta.ini") {
            struct stat st {};
            if (::lstat(full.c_str(), &st) != 0) {
              const int saved = errno;
              throw std::runtime_error(
                  "Unable to inspect VFS provider entry " + full + ": " +
                  std::strerror(saved));
            }
            const auto components = splitPath(relative);
            if (S_ISDIR(st.st_mode)) {
              tree.root.insertDirectory(components);
              visibleArchives.erase(normalizeForLookup(relative));
              ++tree.dir_count;
            } else if (S_ISREG(st.st_mode)) {
              ++state.files_scanned;
              ++state.current_provider.files_scanned;
              const std::string normalized = normalizeForLookup(relative);
              seenFiles.insert(normalized);
              const int64_t mtimeNs = timespecNs(st.st_mtim);
              const int64_t ctimeNs = timespecNs(st.st_ctim);
              std::optional<VfsDigest> digest;
              bool reuseRow = false;

              const auto cached = cachedFiles.find(normalized);
              if (cached != cachedFiles.end()) {
                const CachedCatalogFile& old = cached->second;
                const bool deviceMatches = old.device == st.st_dev;
                const bool inodeMatches = old.inode == st.st_ino;
                const bool sizeMatches = old.size == st.st_size;
                const bool modeMatches = old.mode == (st.st_mode & 07777);
                const bool mtimeMatches = old.mtime_ns == mtimeNs;
                const bool ctimeMatches = old.ctime_ns == ctimeNs;
                reuseRow = deviceMatches && inodeMatches && sizeMatches &&
                            modeMatches && mtimeMatches && ctimeMatches &&
                            (m_mode == VfsCatalogMode::Metadata || old.has_digest);
                if (reuseRow) {
                  if (old.has_digest) digest = old.digest;
                } else {
                  ++state.fingerprint_misses;
                  ++state.current_provider.fingerprint_misses;
                  if (!deviceMatches) {
                    ++state.fingerprint_device_mismatches;
                    ++state.current_provider.fingerprint_device_mismatches;
                  }
                  if (!inodeMatches) {
                    ++state.fingerprint_inode_mismatches;
                    ++state.current_provider.fingerprint_inode_mismatches;
                  }
                  if (!sizeMatches) {
                    ++state.fingerprint_size_mismatches;
                    ++state.current_provider.fingerprint_size_mismatches;
                  }
                  if (!modeMatches) {
                    ++state.fingerprint_mode_mismatches;
                    ++state.current_provider.fingerprint_mode_mismatches;
                  }
                  if (!mtimeMatches) {
                    ++state.fingerprint_mtime_mismatches;
                    ++state.current_provider.fingerprint_mtime_mismatches;
                  }
                  if (!ctimeMatches) {
                    ++state.fingerprint_ctime_mismatches;
                    ++state.current_provider.fingerprint_ctime_mismatches;
                  }
                  if (!old.has_digest && m_mode == VfsCatalogMode::VerifyContents) {
                    ++state.fingerprint_missing_digests;
                    ++state.current_provider.fingerprint_missing_digests;
                  }
                }
              } else {
                ++state.fingerprint_misses;
                ++state.fingerprint_uncached;
                ++state.current_provider.fingerprint_misses;
                ++state.current_provider.fingerprint_uncached;
              }

              pendingFiles.push_back({entry.path(), full, relative,
                                      normalized, st, digest, reuseRow});
              if (progress && (state.files_scanned % 2048 == 0)) {
                reportProgress();
              }
            }
          }

          ec.clear();
          it.increment(ec);
          throwIfCancelled(stop_token);
          if (ec) {
            throw std::runtime_error("Unable to complete scan of VFS provider " +
                                     root.path + ": " + ec.message());
          }
        }
      }

      auto firstChanged = pendingFiles.end();
      for (auto file = pendingFiles.begin(); file != pendingFiles.end(); ++file) {
        throwIfCancelled(stop_token);
        if (!file->reuse_row && m_mode == VfsCatalogMode::VerifyContents) {
          firstChanged = file;
          break;
        }
      }
      if (firstChanged != pendingFiles.end()) {
        state.current_file = firstChanged->full;
        state.current_file_size =
            static_cast<uint64_t>(firstChanged->metadata.st_size);
        state.phase = VfsCatalogPhase::Hashing;
        reportProgress();
      }
      const std::size_t hashWorkers = firstChanged == pendingFiles.end() ? 0 : hashChangedFiles(
          pendingFiles, [&](const PendingCatalogFile& file) {
            ++state.files_hashed;
            ++state.current_provider.files_hashed;
            state.bytes_hashed +=
                static_cast<uint64_t>(file.metadata.st_size);
            state.current_provider.bytes_hashed +=
                static_cast<uint64_t>(file.metadata.st_size);
            state.current_file = file.full;
            state.current_file_size =
                static_cast<uint64_t>(file.metadata.st_size);
            reportProgress();
          }, stop_token);
      state.hash_workers =
          std::max<uint64_t>(state.hash_workers, hashWorkers);
      if (firstChanged != pendingFiles.end()) {
        state.current_file.clear();
        state.current_file_size = 0;
        state.phase = VfsCatalogPhase::Metadata;
        reportProgress();
      }

      for (const PendingCatalogFile& file : pendingFiles) {
        throwIfCancelled(stop_token);
        const struct stat& st = file.metadata;
        const int64_t mtimeNs = timespecNs(st.st_mtim);
        const int64_t ctimeNs = timespecNs(st.st_ctim);
        if (!file.reuse_row) {
          sqlite3_reset(upsert.get());
          sqlite3_clear_bindings(upsert.get());
          bindText(db.get(), upsert.get(), 1, root.key);
          bindText(db.get(), upsert.get(), 2, file.relative);
          bindText(db.get(), upsert.get(), 3, file.normalized);
          bindText(db.get(), upsert.get(), 4, root.origin);
          sqlite3_bind_int(upsert.get(), 5, root.backing ? 1 : 0);
          sqlite3_bind_int64(upsert.get(), 6, st.st_dev);
          sqlite3_bind_int64(upsert.get(), 7, st.st_ino);
          sqlite3_bind_int64(upsert.get(), 8, st.st_size);
          sqlite3_bind_int64(upsert.get(), 9, st.st_mode & 07777);
          sqlite3_bind_int64(upsert.get(), 10, mtimeNs);
          sqlite3_bind_int64(upsert.get(), 11, ctimeNs);
          if (file.digest) {
            bindDigest(db.get(), upsert.get(), 12, *file.digest);
          } else {
            // Empty means unverified, never a metadata hash masquerading as
            // a content digest. This retains the old NOT NULL column safely.
            sqlite3_bind_zeroblob(upsert.get(), 12, 0);
          }
          sqlite3_bind_int64(upsert.get(), 13, generation);
          if (sqlite3_step(upsert.get()) != SQLITE_DONE) throwDb(db.get(), "Updating catalog file");
          ++state.catalog_rows_written;
          ++state.current_provider.catalog_rows_written;
        }

        const std::string storedPath =
            root.backing ? file.relative : file.full;
        tree.root.insertFile(splitPath(file.relative), storedPath,
                             static_cast<uint64_t>(st.st_size),
                             timePoint(st.st_mtim), root.origin, root.backing,
                             st.st_mode & 07777, file.digest);
        ++tree.file_count;
        if (isBethesdaArchive(file.relative)) {
          const VfsDigest identity = archiveIdentity(file.full, st);
          archiveCandidates.insert_or_assign(identity, file.full);
          visibleArchives.insert_or_assign(file.normalized, identity);
          ++state.archives_discovered;
        }
      }
      state.current_file.clear();
      state.current_file_size = 0;

      uint64_t rootRowsDeleted = 0;
      for (const auto& [normalized, cached] : cachedFiles) {
        throwIfCancelled(stop_token);
        (void)cached;
        if (seenFiles.contains(normalized)) continue;
        sqlite3_reset(deleteFile.get());
        sqlite3_clear_bindings(deleteFile.get());
        bindText(db.get(), deleteFile.get(), 1, root.key);
        bindText(db.get(), deleteFile.get(), 2, normalized);
        if (sqlite3_step(deleteFile.get()) != SQLITE_DONE) {
          throwDb(db.get(), "Pruning catalog file");
        }
        ++state.catalog_rows_deleted;
        ++state.current_provider.catalog_rows_deleted;
        ++rootRowsDeleted;
      }

      sqlite3_reset(refreshRootOrigin.get());
      sqlite3_clear_bindings(refreshRootOrigin.get());
      bindText(db.get(), refreshRootOrigin.get(), 1, root.key);
      bindText(db.get(), refreshRootOrigin.get(), 2, root.origin);
      sqlite3_bind_int(refreshRootOrigin.get(), 3, root.backing ? 1 : 0);
      if (sqlite3_step(refreshRootOrigin.get()) != SQLITE_DONE) {
        throwDb(db.get(), "Refreshing catalog root origin");
      }

      bool catalogContentChanged = rootRowsDeleted != 0;
      for (const PendingCatalogFile& file : pendingFiles) {
        throwIfCancelled(stop_token);
        if (!file.reuse_row) {
          catalogContentChanged = true;
          break;
        }
      }
      VfsProviderRoot providerRoot;
      if (!catalogContentChanged && hasPreviousRoot &&
          previousRootFileCount == pendingFiles.size()) {
        providerRoot = {
            root.key, root.origin, root.backing,
            previousRootFileCount, previousRootDigest};
        ++state.merkle_roots_reused;
      } else {
        providerRoot = calculateProviderRoot(db.get(), root, stop_token);
      }
      const bool rootChanged =
          !hasPreviousRoot || previousRootDigest != providerRoot.digest;
      if (rootChanged) ++state.provider_roots_changed;

      sqlite3_reset(upsertRoot.get());
      sqlite3_clear_bindings(upsertRoot.get());
      bindText(db.get(), upsertRoot.get(), 1, root.key);
      sqlite3_bind_blob(upsertRoot.get(), 2, providerRoot.digest.data(),
                        providerRoot.digest.size(), SQLITE_TRANSIENT);
      sqlite3_bind_int64(upsertRoot.get(), 3, providerRoot.file_count);
      sqlite3_bind_int64(upsertRoot.get(), 4, generation);
      if (sqlite3_step(upsertRoot.get()) != SQLITE_DONE) {
        throwDb(db.get(), "Updating catalog Merkle root");
      }
      state.current_provider.completed = true;
      providerSummaries.push_back(state.current_provider);
      reportProgress();
      providerRoots.push_back(std::move(providerRoot));
    }
    state.provider_reconcile_ms = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - providersStart)
            .count());

    const auto archivesStart = std::chrono::steady_clock::now();
    state.phase = VfsCatalogPhase::Archives;
    reportProgress();
    throwIfCancelled(stop_token);
    auto archiveMemberIndex = reconcileArchiveManifests(
        db.get(), archiveCandidates, visibleArchives, state, stop_token);
    state.archive_reconcile_ms = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - archivesStart)
            .count());

    const auto duplicatesStart = std::chrono::steady_clock::now();
    state.phase = VfsCatalogPhase::Duplicates;
    reportProgress();
    throwIfCancelled(stop_token);
    std::vector<VfsCatalogDuplicate> duplicates;
    exec(db.get(), "DELETE FROM enabled_provider_priority;");
    auto insertProvider = prepare(db.get(),
        "INSERT INTO enabled_provider_priority(root_key,priority)"
        " VALUES(?1,?2);");
    for (const auto& [rootKey, index] : modIndexByRoot) {
      throwIfCancelled(stop_token);
      sqlite3_reset(insertProvider.get());
      sqlite3_clear_bindings(insertProvider.get());
      bindText(db.get(), insertProvider.get(), 1, rootKey);
      sqlite3_bind_int64(insertProvider.get(), 2,
                         static_cast<sqlite3_int64>(index));
      if (sqlite3_step(insertProvider.get()) != SQLITE_DONE) {
        throwDb(db.get(), "Staging enabled provider priority");
      }
    }
    insertProvider.reset();

    // Resolve every Overwrite collision in one indexed join. The old nested
    // loop canonicalized every mod path for every Overwrite file and issued
    // millions of point queries on large profiles.
    auto collisions = prepare(db.get(),
        "SELECT candidate.normalized_path,candidate.blake3,"
        " overwrite.relative_path,overwrite.blake3,enabled.root_key,"
        " candidate.size,overwrite.size"
        " FROM enabled_provider_priority AS enabled"
        " JOIN catalog_files AS candidate"
        " ON candidate.root_key=enabled.root_key"
        " JOIN catalog_files AS overwrite"
        " ON overwrite.root_key=?1"
        " AND overwrite.normalized_path=candidate.normalized_path"
        " ORDER BY candidate.normalized_path,enabled.priority DESC;");
    bindText(db.get(), collisions.get(), 1, overwriteRootKey);
    std::string previousNormalized;
    while (sqlite3_step(collisions.get()) == SQLITE_ROW) {
      throwIfCancelled(stop_token);
      const auto* normalized = reinterpret_cast<const char*>(
          sqlite3_column_text(collisions.get(), 0));
      const auto* relative = reinterpret_cast<const char*>(
          sqlite3_column_text(collisions.get(), 2));
      const auto* rootKey = reinterpret_cast<const char*>(
          sqlite3_column_text(collisions.get(), 4));
      if (normalized == nullptr || relative == nullptr || rootKey == nullptr ||
          normalized == previousNormalized) {
        continue;
      }
      previousNormalized = normalized;
      const auto modIndex = modIndexByRoot.find(rootKey);
      if (modIndex == modIndexByRoot.end()) continue;
      const void* modHash = sqlite3_column_blob(collisions.get(), 1);
      const int modHashSize = sqlite3_column_bytes(collisions.get(), 1);
      const void* overwriteHash = sqlite3_column_blob(collisions.get(), 3);
      const int overwriteHashSize = sqlite3_column_bytes(collisions.get(), 3);
      const bool verified =
          modHash != nullptr && modHashSize == BLAKE3_OUT_LEN &&
          overwriteHash != nullptr && overwriteHashSize == BLAKE3_OUT_LEN;
      VfsDuplicateState duplicateState = VfsDuplicateState::Unverified;
      if (sqlite3_column_int64(collisions.get(), 5) !=
          sqlite3_column_int64(collisions.get(), 6)) {
        duplicateState = VfsDuplicateState::Different;
      } else if (verified) {
        duplicateState = std::memcmp(overwriteHash, modHash, BLAKE3_OUT_LEN) == 0
                             ? VfsDuplicateState::Identical
                             : VfsDuplicateState::Different;
      }
      const auto& [modName, modPath] = mods[modIndex->second];
      duplicates.push_back({relative, modName, modPath, duplicateState});
    }
    state.duplicate_scan_ms = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - duplicatesStart)
            .count());

    const VfsDigest profileRoot = calculateProfileRoot(providerRoots, stop_token);
    const auto commitStart = std::chrono::steady_clock::now();
    state.phase = VfsCatalogPhase::Commit;
    reportProgress();
    throwIfCancelled(stop_token);
    exec(db.get(), "COMMIT;");
    state.commit_ms = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - commitStart)
            .count());
    state.current_file.clear();
    state.current_file_size = 0;
    state.phase = VfsCatalogPhase::Complete;
    reportProgress();
    VfsCatalogResult result;
    tree.recount();
    result.tree = std::move(tree);
    result.provider_roots = std::move(providerRoots);
    result.provider_summaries = std::move(providerSummaries);
    result.profile_root = profileRoot;
    result.overwrite_duplicates = std::move(duplicates);
    result.archive_member_index = std::move(archiveMemberIndex);
    return result;
  } catch (...) {
    const std::exception_ptr error = std::current_exception();
    sqlite3_exec(db.get(), "ROLLBACK;", nullptr, nullptr, nullptr);
    // Give the caller one final bounded snapshot of the provider that failed.
    // A failed reconciliation must never be presented as complete.
    if (progress && reportProgress && !progressCallbackFailed &&
        state.phase != VfsCatalogPhase::Complete) {
      try {
        reportProgress();
      } catch (...) {
        // Preserve the reconciliation failure if reporting also fails.
      }
    }
    std::rethrow_exception(error);
  }
}

VfsCatalogRefreshResult VfsCatalog::forceRefreshProviderFiles(
    const std::string& rootKey, const std::string& origin, bool isBacking,
    const std::vector<std::string>& relativePaths)
{
  const std::string rootIdentity = catalogRootIdentity(rootKey);
  std::error_code ec;
  fs::create_directories(m_database_path.parent_path(), ec);
  if (ec) {
    throw std::runtime_error("Unable to create local VFS catalog directory: " +
                             ec.message());
  }

  sqlite3* raw = nullptr;
  if (sqlite3_open_v2(m_database_path.c_str(), &raw,
                      SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE |
                          SQLITE_OPEN_FULLMUTEX,
                      nullptr) != SQLITE_OK) {
    DbPtr failed(raw);
    throwDb(raw, "Opening local VFS catalog");
  }
  DbPtr db(raw);
  sqlite3_busy_timeout(db.get(), 30000);
  initializeSchema(db.get());
  exec(db.get(), "BEGIN IMMEDIATE;");
  try {
    const int64_t generation = nextGeneration(db.get());
    auto upsert = prepare(db.get(),
        "INSERT INTO catalog_files(root_key,relative_path,normalized_path,origin,"
        "is_backing,device,inode,size,mode,mtime_ns,ctime_ns,blake3,seen_generation)"
        " VALUES(?1,?2,?3,?4,?5,?6,?7,?8,?9,?10,?11,?12,?13)"
        " ON CONFLICT(root_key,normalized_path) DO UPDATE SET"
        " relative_path=excluded.relative_path,origin=excluded.origin,"
        " is_backing=excluded.is_backing,device=excluded.device,inode=excluded.inode,"
        " size=excluded.size,mode=excluded.mode,mtime_ns=excluded.mtime_ns,"
        " ctime_ns=excluded.ctime_ns,blake3=excluded.blake3,"
        " seen_generation=excluded.seen_generation;");
    auto remove = prepare(db.get(),
        "DELETE FROM catalog_files WHERE root_key=?1 AND normalized_path=?2;");

    VfsCatalogRefreshResult result;
    result.files.reserve(relativePaths.size());
    for (const std::string& relative : relativePaths) {
      const fs::path relativePath(relative);
      if (relativePath.empty() || relativePath.is_absolute() ||
          std::find(relativePath.begin(), relativePath.end(), fs::path("..")) !=
              relativePath.end()) {
        throw std::runtime_error("Unsafe catalog refresh path: " + relative);
      }
      const std::string normalized = normalizeForLookup(relativePath.generic_string());
      const fs::path full = fs::path(rootKey) / relativePath;
      struct stat st {};
      if (::lstat(full.c_str(), &st) != 0) {
        if (errno != ENOENT) {
          throw std::runtime_error("Unable to inspect catalog refresh path " +
                                   full.string() + ": " + std::strerror(errno));
        }
        sqlite3_reset(remove.get());
        sqlite3_clear_bindings(remove.get());
        bindText(db.get(), remove.get(), 1, rootIdentity);
        bindText(db.get(), remove.get(), 2, normalized);
        if (sqlite3_step(remove.get()) != SQLITE_DONE) {
          throwDb(db.get(), "Removing missing catalog file");
        }
        result.files.push_back({relativePath.generic_string(), false, {}});
        continue;
      }
      if (!S_ISREG(st.st_mode)) {
        throw std::runtime_error("Catalog refresh path is not a regular file: " +
                                 full.string());
      }

      VfsDigest digest{};
      bool stable = false;
      for (int attempt = 0; attempt < 3; ++attempt) {
        const struct stat before = st;
        digest = hashFile(full);
        if (::lstat(full.c_str(), &st) != 0) {
          throw std::runtime_error("File disappeared while force-hashing: " +
                                   full.string());
        }
        if (sameContentFingerprint(before, st)) {
          stable = true;
          break;
        }
      }
      if (!stable) {
        throw std::runtime_error("File kept changing while force-hashing: " +
                                 full.string());
      }

      sqlite3_reset(upsert.get());
      sqlite3_clear_bindings(upsert.get());
      bindText(db.get(), upsert.get(), 1, rootIdentity);
      bindText(db.get(), upsert.get(), 2, relativePath.generic_string());
      bindText(db.get(), upsert.get(), 3, normalized);
      bindText(db.get(), upsert.get(), 4, origin);
      sqlite3_bind_int(upsert.get(), 5, isBacking ? 1 : 0);
      sqlite3_bind_int64(upsert.get(), 6, st.st_dev);
      sqlite3_bind_int64(upsert.get(), 7, st.st_ino);
      sqlite3_bind_int64(upsert.get(), 8, st.st_size);
      sqlite3_bind_int64(upsert.get(), 9, st.st_mode & 07777);
      sqlite3_bind_int64(upsert.get(), 10, timespecNs(st.st_mtim));
      sqlite3_bind_int64(upsert.get(), 11, timespecNs(st.st_ctim));
      sqlite3_bind_blob(upsert.get(), 12, digest.data(), digest.size(), SQLITE_TRANSIENT);
      sqlite3_bind_int64(upsert.get(), 13, generation);
      if (sqlite3_step(upsert.get()) != SQLITE_DONE) {
        throwDb(db.get(), "Force-refreshing catalog file");
      }
      result.files.push_back({relativePath.generic_string(), true, digest});
    }

    const Root root{rootIdentity, rootKey, origin, isBacking};
    result.provider_root = calculateProviderRoot(db.get(), root);
    auto upsertRoot = prepare(db.get(),
        "INSERT INTO catalog_roots(root_key,merkle_root,file_count,generation)"
        " VALUES(?1,?2,?3,?4) ON CONFLICT(root_key) DO UPDATE SET"
        " merkle_root=excluded.merkle_root,file_count=excluded.file_count,"
        " generation=excluded.generation;");
    bindText(db.get(), upsertRoot.get(), 1, rootIdentity);
    sqlite3_bind_blob(upsertRoot.get(), 2, result.provider_root.digest.data(),
                      result.provider_root.digest.size(), SQLITE_TRANSIENT);
    sqlite3_bind_int64(upsertRoot.get(), 3, result.provider_root.file_count);
    sqlite3_bind_int64(upsertRoot.get(), 4, generation);
    if (sqlite3_step(upsertRoot.get()) != SQLITE_DONE) {
      throwDb(db.get(), "Updating force-refreshed catalog root");
    }
    exec(db.get(), "COMMIT;");
    return result;
  } catch (...) {
    sqlite3_exec(db.get(), "ROLLBACK;", nullptr, nullptr, nullptr);
    throw;
  }
}

void VfsCatalog::invalidateProviderFiles(
    const std::string& rootKey,
    const std::vector<std::string>& relativePaths) noexcept
{
  const std::string rootIdentity = catalogRootIdentity(rootKey);
  sqlite3* raw = nullptr;
  if (sqlite3_open_v2(m_database_path.c_str(), &raw,
                      SQLITE_OPEN_READWRITE | SQLITE_OPEN_FULLMUTEX,
                      nullptr) != SQLITE_OK) {
    if (raw != nullptr) sqlite3_close(raw);
    return;
  }
  DbPtr db(raw);
  sqlite3_busy_timeout(db.get(), 30000);
  try {
    initializeSchema(db.get());
    exec(db.get(), "BEGIN IMMEDIATE;");
    auto remove = prepare(db.get(),
        "DELETE FROM catalog_files WHERE root_key=?1 AND normalized_path=?2;");
    for (const std::string& relative : relativePaths) {
      sqlite3_reset(remove.get());
      sqlite3_clear_bindings(remove.get());
      bindText(db.get(), remove.get(), 1, rootIdentity);
      bindText(db.get(), remove.get(), 2, normalizeForLookup(relative));
      if (sqlite3_step(remove.get()) != SQLITE_DONE) throwDb(db.get(), "Invalidating catalog file");
    }
    auto removeRoot = prepare(db.get(), "DELETE FROM catalog_roots WHERE root_key=?1;");
    bindText(db.get(), removeRoot.get(), 1, rootIdentity);
    if (sqlite3_step(removeRoot.get()) != SQLITE_DONE) throwDb(db.get(), "Invalidating catalog root");
    exec(db.get(), "COMMIT;");
  } catch (...) {
    sqlite3_exec(db.get(), "ROLLBACK;", nullptr, nullptr, nullptr);
  }
}

std::vector<CachedBaseFile> VfsCatalog::loadBaseSnapshot(
    const std::string& data_dir) const
{
  const std::string dataRootKey = catalogRootIdentity(data_dir);
  sqlite3* raw = nullptr;
  if (sqlite3_open_v2(m_database_path.c_str(), &raw,
                      SQLITE_OPEN_READONLY | SQLITE_OPEN_FULLMUTEX,
                      nullptr) != SQLITE_OK) {
    DbPtr failed(raw);
    throwDb(raw, "Opening VFS catalog base snapshot");
  }
  DbPtr db(raw);
  auto stmt = prepare(db.get(),
      "SELECT relative_path,size,mtime_ns,mode FROM catalog_files"
      " WHERE root_key=?1 ORDER BY normalized_path;");
  bindText(db.get(), stmt.get(), 1, dataRootKey);

  std::vector<CachedBaseFile> files;
  while (sqlite3_step(stmt.get()) == SQLITE_ROW) {
    const auto* path = reinterpret_cast<const char*>(sqlite3_column_text(stmt.get(), 0));
    if (path == nullptr) continue;
    CachedBaseFile file;
    file.relative_path = path;
    file.size = static_cast<uint64_t>(sqlite3_column_int64(stmt.get(), 1));
    file.mtime = std::chrono::system_clock::time_point(
        std::chrono::nanoseconds(sqlite3_column_int64(stmt.get(), 2)));
    file.mode = static_cast<mode_t>(sqlite3_column_int64(stmt.get(), 3));
    files.push_back(std::move(file));
  }
  return files;
}
