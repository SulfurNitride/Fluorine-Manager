#include "vfscatalog.h"

#include "../fluorinepaths.h"

#include <QDir>

#include <blake3.h>
#include <sqlite3.h>

#include <array>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <memory>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>

namespace
{
namespace fs = std::filesystem;

constexpr int kSchemaVersion = 1;
constexpr size_t kHashBufferSize = 1024 * 1024;

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

std::array<unsigned char, BLAKE3_OUT_LEN> hashFile(const fs::path& path)
{
  const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
  if (fd < 0) {
    throw std::runtime_error("Unable to hash " + path.string() + ": " +
                             std::strerror(errno));
  }

  blake3_hasher hasher;
  blake3_hasher_init(&hasher);
  std::array<unsigned char, kHashBufferSize> buffer{};
  for (;;) {
    const ssize_t count = ::read(fd, buffer.data(), buffer.size());
    if (count == 0) break;
    if (count < 0) {
      const int saved = errno;
      ::close(fd);
      throw std::runtime_error("Unable to hash " + path.string() + ": " +
                               std::strerror(saved));
    }
    blake3_hasher_update(&hasher, buffer.data(), static_cast<size_t>(count));
  }
  ::close(fd);

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

struct Root
{
  std::string key;
  std::string path;
  std::string origin;
  bool backing = false;
};

void initializeSchema(sqlite3* db)
{
  exec(db, "PRAGMA journal_mode=WAL;");
  exec(db, "PRAGMA synchronous=NORMAL;");
  exec(db, "PRAGMA foreign_keys=ON;");
  exec(db, "PRAGMA temp_store=MEMORY;");
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
       "CREATE TEMP TABLE IF NOT EXISTS catalog_seen("
       " root_key TEXT NOT NULL, normalized_path TEXT NOT NULL,"
       " PRIMARY KEY(root_key,normalized_path)) WITHOUT ROWID;");

  auto stmt = prepare(db,
      "INSERT INTO catalog_meta(key,value) VALUES('schema_version',?1)"
      " ON CONFLICT(key) DO UPDATE SET value=excluded.value;");
  sqlite3_bind_int(stmt.get(), 1, kSchemaVersion);
  if (sqlite3_step(stmt.get()) != SQLITE_DONE) throwDb(db, "Writing catalog schema");
}

int64_t nextGeneration(sqlite3* db)
{
  exec(db, "INSERT OR IGNORE INTO catalog_meta(key,value) VALUES('generation',0);");
  exec(db, "UPDATE catalog_meta SET value=value+1 WHERE key='generation';");
  auto stmt = prepare(db, "SELECT value FROM catalog_meta WHERE key='generation';");
  if (sqlite3_step(stmt.get()) != SQLITE_ROW) throwDb(db, "Reading catalog generation");
  return sqlite3_column_int64(stmt.get(), 0);
}

}  // namespace

VfsCatalog::VfsCatalog(fs::path database_path)
    : m_database_path(std::move(database_path))
{}

fs::path VfsCatalog::databasePath(const std::string& data_dir)
{
  char name[48];
  std::snprintf(name, sizeof(name), "%016llx.catalog.sqlite",
                static_cast<unsigned long long>(fnv1a(data_dir)));
  return fs::path(fluorineVfsCacheDir().toStdString()) / name;
}

VfsTree VfsCatalog::reconcileAndBuild(
    const std::string& data_dir,
    const std::vector<std::pair<std::string, std::string>>& mods,
    const std::string& overwrite_dir,
    bool scan_base,
    ProgressCallback progress)
{
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
    VfsTree tree;
    tree.root.is_directory = true;
    tree.dir_count = 1;

    std::vector<Root> roots;
    if (scan_base) roots.push_back({data_dir, data_dir, "_base_game", true});
    for (const auto& [name, path] : mods) roots.push_back({path, path, name, false});
    roots.push_back({overwrite_dir, overwrite_dir, "Overwrite", false});

    if (!scan_base) {
      auto base = prepare(db.get(),
          "SELECT relative_path,size,mtime_ns,mode FROM catalog_files"
          " WHERE root_key=?1 ORDER BY normalized_path;");
      bindText(db.get(), base.get(), 1, data_dir);
      while (sqlite3_step(base.get()) == SQLITE_ROW) {
        const auto* rel = reinterpret_cast<const char*>(sqlite3_column_text(base.get(), 0));
        if (rel == nullptr) continue;
        const std::string relative(rel);
        tree.root.insertFile(
            splitPath(relative), relative,
            static_cast<uint64_t>(sqlite3_column_int64(base.get(), 1)),
            std::chrono::system_clock::time_point(
                std::chrono::nanoseconds(sqlite3_column_int64(base.get(), 2))),
            "_base_game", true,
            static_cast<mode_t>(sqlite3_column_int64(base.get(), 3)));
        ++tree.file_count;
      }
    }

    auto find = prepare(db.get(),
        "SELECT device,inode,size,mode,mtime_ns,ctime_ns,blake3"
        " FROM catalog_files WHERE root_key=?1 AND normalized_path=?2;");
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
    auto markSeen = prepare(db.get(),
        "INSERT OR IGNORE INTO catalog_seen(root_key,normalized_path) VALUES(?1,?2);");
    auto refreshOrigin = prepare(db.get(),
        "UPDATE catalog_files SET origin=?3,is_backing=?4"
        " WHERE root_key=?1 AND normalized_path=?2"
        " AND (origin<>?3 OR is_backing<>?4);");
    auto prune = prepare(db.get(),
        "DELETE FROM catalog_files WHERE root_key=?1 AND normalized_path NOT IN"
        " (SELECT normalized_path FROM catalog_seen WHERE root_key=?1);");

    VfsCatalogProgress state;
    for (const Root& root : roots) {
      state.current_root = root.path;
      if (!fs::exists(root.path, ec)) continue;
      const std::string rootPrefix = fs::path(root.path).string();

      for (auto it = fs::recursive_directory_iterator(
               root.path, fs::directory_options::skip_permission_denied, ec);
           !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
        const fs::directory_entry& entry = *it;
        const std::string full = entry.path().string();
        const std::string relative = fastRelative(full, rootPrefix);
        if (relative.empty() || relative == "meta.ini") continue;

        struct stat st {};
        if (::lstat(full.c_str(), &st) != 0) continue;
        const auto components = splitPath(relative);
        if (S_ISDIR(st.st_mode)) {
          tree.root.insertDirectory(components);
          ++tree.dir_count;
          continue;
        }
        if (!S_ISREG(st.st_mode)) continue;

        ++state.files_scanned;
        const std::string normalized = normalizeForLookup(relative);
        sqlite3_reset(markSeen.get());
        sqlite3_clear_bindings(markSeen.get());
        bindText(db.get(), markSeen.get(), 1, root.key);
        bindText(db.get(), markSeen.get(), 2, normalized);
        if (sqlite3_step(markSeen.get()) != SQLITE_DONE) throwDb(db.get(), "Marking catalog file seen");
        int64_t mtimeNs = timespecNs(st.st_mtim);
        int64_t ctimeNs = timespecNs(st.st_ctim);
        std::array<unsigned char, BLAKE3_OUT_LEN> digest{};
        bool reuseHash = false;

        sqlite3_reset(find.get());
        sqlite3_clear_bindings(find.get());
        bindText(db.get(), find.get(), 1, root.key);
        bindText(db.get(), find.get(), 2, normalized);
        if (sqlite3_step(find.get()) == SQLITE_ROW) {
          const void* blob = sqlite3_column_blob(find.get(), 6);
          const int bytes = sqlite3_column_bytes(find.get(), 6);
          reuseHash = sqlite3_column_int64(find.get(), 0) == st.st_dev &&
                      sqlite3_column_int64(find.get(), 1) == st.st_ino &&
                      sqlite3_column_int64(find.get(), 2) == st.st_size &&
                      sqlite3_column_int64(find.get(), 4) == mtimeNs &&
                      blob != nullptr && bytes == BLAKE3_OUT_LEN;
          if (reuseHash) std::memcpy(digest.data(), blob, digest.size());
        }
        if (!reuseHash) {
          if (progress) progress(state);  // show the file count before a long hash
          bool stable = false;
          for (int attempt = 0; attempt < 3; ++attempt) {
            const struct stat before = st;
            digest = hashFile(entry.path());
            struct stat after {};
            if (::lstat(full.c_str(), &after) != 0) {
              throw std::runtime_error("File disappeared while hashing: " + full);
            }
            st = after;
            mtimeNs = timespecNs(st.st_mtim);
            ctimeNs = timespecNs(st.st_ctim);
            if (sameContentFingerprint(before, after)) {
              stable = true;
              break;
            }
          }
          if (!stable) {
            throw std::runtime_error("File kept changing while hashing: " + full);
          }
          ++state.files_hashed;
          state.bytes_hashed += static_cast<uint64_t>(st.st_size);
        }

        if (!reuseHash) {
          sqlite3_reset(upsert.get());
          sqlite3_clear_bindings(upsert.get());
          bindText(db.get(), upsert.get(), 1, root.key);
          bindText(db.get(), upsert.get(), 2, relative);
          bindText(db.get(), upsert.get(), 3, normalized);
          bindText(db.get(), upsert.get(), 4, root.origin);
          sqlite3_bind_int(upsert.get(), 5, root.backing ? 1 : 0);
          sqlite3_bind_int64(upsert.get(), 6, st.st_dev);
          sqlite3_bind_int64(upsert.get(), 7, st.st_ino);
          sqlite3_bind_int64(upsert.get(), 8, st.st_size);
          sqlite3_bind_int64(upsert.get(), 9, st.st_mode & 07777);
          sqlite3_bind_int64(upsert.get(), 10, mtimeNs);
          sqlite3_bind_int64(upsert.get(), 11, ctimeNs);
          sqlite3_bind_blob(upsert.get(), 12, digest.data(), digest.size(), SQLITE_TRANSIENT);
          sqlite3_bind_int64(upsert.get(), 13, generation);
          if (sqlite3_step(upsert.get()) != SQLITE_DONE) throwDb(db.get(), "Updating catalog file");
        } else {
          sqlite3_reset(refreshOrigin.get());
          sqlite3_clear_bindings(refreshOrigin.get());
          bindText(db.get(), refreshOrigin.get(), 1, root.key);
          bindText(db.get(), refreshOrigin.get(), 2, normalized);
          bindText(db.get(), refreshOrigin.get(), 3, root.origin);
          sqlite3_bind_int(refreshOrigin.get(), 4, root.backing ? 1 : 0);
          if (sqlite3_step(refreshOrigin.get()) != SQLITE_DONE) throwDb(db.get(), "Refreshing catalog origin");
        }

        const std::string storedPath = root.backing ? relative : full;
        tree.root.insertFile(components, storedPath,
                             static_cast<uint64_t>(st.st_size),
                             timePoint(st.st_mtim), root.origin, root.backing,
                             st.st_mode & 07777);
        ++tree.file_count;

        if (progress && (state.files_scanned % 2048 == 0)) progress(state);
      }
      ec.clear();

      sqlite3_reset(prune.get());
      sqlite3_clear_bindings(prune.get());
      bindText(db.get(), prune.get(), 1, root.key);
      if (sqlite3_step(prune.get()) != SQLITE_DONE) throwDb(db.get(), "Pruning catalog root");
    }

    exec(db.get(), "COMMIT;");
    if (progress) progress(state);
    return tree;
  } catch (...) {
    sqlite3_exec(db.get(), "ROLLBACK;", nullptr, nullptr, nullptr);
    throw;
  }
}

std::vector<CachedBaseFile> VfsCatalog::loadBaseSnapshot(
    const std::string& data_dir) const
{
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
  bindText(db.get(), stmt.get(), 1, data_dir);

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
