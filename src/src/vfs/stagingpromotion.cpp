#include "stagingpromotion.h"

#include <QByteArray>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

#include <blake3.h>

#include <array>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <optional>
#include <stdexcept>
#include <sys/stat.h>
#include <unordered_set>
#include <unistd.h>

namespace
{
namespace fs = std::filesystem;

struct JournalFile
{
  StagingPromotedFile file;
  std::optional<VfsDigest> previous;
};

struct Journal
{
  fs::path destination;
  std::vector<JournalFile> files;
};

constexpr const char* UnjournaledReason =
    "Fluorine found unjournaled files in VFS_staging. They were preserved and launch was blocked.";

class Fd
{
public:
  explicit Fd(int value = -1) : m_value(value) {}
  ~Fd() { if (m_value >= 0) ::close(m_value); }
  Fd(const Fd&) = delete;
  Fd& operator=(const Fd&) = delete;
  int get() const { return m_value; }
private:
  int m_value;
};

[[noreturn]] void fail(const std::string& what, const fs::path& path)
{
  throw std::runtime_error(what + " '" + path.string() + "': " +
                           std::strerror(errno));
}

std::string hexDigest(const VfsDigest& digest)
{
  static constexpr char digits[] = "0123456789abcdef";
  std::string value(digest.size() * 2, '0');
  for (size_t i = 0; i < digest.size(); ++i) {
    value[i * 2] = digits[digest[i] >> 4];
    value[i * 2 + 1] = digits[digest[i] & 0x0f];
  }
  return value;
}

std::optional<VfsDigest> parseDigest(const QString& text)
{
  const QByteArray bytes = text.toLatin1();
  if (bytes.size() != 64) return std::nullopt;
  VfsDigest digest{};
  for (size_t i = 0; i < digest.size(); ++i) {
    bool ok = false;
    const int value = QByteArray(bytes.constData() + i * 2, 2).toInt(&ok, 16);
    if (!ok) return std::nullopt;
    digest[i] = static_cast<unsigned char>(value);
  }
  return digest;
}

VfsDigest hashFd(int fd, const fs::path& path)
{
  if (::lseek(fd, 0, SEEK_SET) < 0) fail("Unable to seek", path);
  blake3_hasher hasher;
  blake3_hasher_init(&hasher);
  std::array<unsigned char, 1024 * 1024> buffer{};
  for (;;) {
    const ssize_t count = ::read(fd, buffer.data(), buffer.size());
    if (count == 0) break;
    if (count < 0) fail("Unable to read", path);
    blake3_hasher_update(&hasher, buffer.data(), static_cast<size_t>(count));
  }
  VfsDigest digest{};
  blake3_hasher_finalize(&hasher, digest.data(), digest.size());
  return digest;
}

VfsDigest hashStableFile(const fs::path& path, struct stat* finalStat = nullptr)
{
  Fd fd(::open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW));
  if (fd.get() < 0) fail("Unable to open", path);
  struct stat before {};
  if (::fstat(fd.get(), &before) != 0 || !S_ISREG(before.st_mode)) {
    throw std::runtime_error("Promotion source is not a regular file: " + path.string());
  }
  const VfsDigest digest = hashFd(fd.get(), path);
  struct stat after {};
  if (::fstat(fd.get(), &after) != 0) fail("Unable to inspect", path);
  if (before.st_dev != after.st_dev || before.st_ino != after.st_ino ||
      before.st_size != after.st_size || before.st_mtim.tv_sec != after.st_mtim.tv_sec ||
      before.st_mtim.tv_nsec != after.st_mtim.tv_nsec ||
      before.st_ctim.tv_sec != after.st_ctim.tv_sec ||
      before.st_ctim.tv_nsec != after.st_ctim.tv_nsec) {
    throw std::runtime_error("File changed while being hashed: " + path.string());
  }
  if (finalStat != nullptr) *finalStat = after;
  return digest;
}

std::optional<VfsDigest> existingDigest(const fs::path& path)
{
  struct stat st {};
  if (::lstat(path.c_str(), &st) != 0) {
    if (errno == ENOENT) return std::nullopt;
    fail("Unable to inspect", path);
  }
  if (!S_ISREG(st.st_mode)) {
    throw std::runtime_error("Promotion destination is not a regular file: " + path.string());
  }
  return hashStableFile(path);
}

bool safeRelative(const fs::path& path)
{
  if (path.empty() || path.is_absolute()) return false;
  for (const auto& part : path) {
    if (part == ".." || part == "." || part.empty()) return false;
  }
  return true;
}

fs::path canonicalDestination(const fs::path& destination)
{
  std::error_code ec;
  fs::create_directories(destination, ec);
  if (ec) throw std::runtime_error("Unable to create promotion destination: " + ec.message());
  const fs::path result = fs::weakly_canonical(destination, ec);
  if (ec) throw std::runtime_error("Unable to resolve promotion destination: " + ec.message());
  return result;
}

void fsyncDirectory(const fs::path& path)
{
  Fd fd(::open(path.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC));
  if (fd.get() < 0) fail("Unable to open directory", path);
  if (::fsync(fd.get()) != 0) fail("Unable to sync directory", path);
}

QByteArray serialize(const Journal& journal)
{
  QJsonObject root;
  root.insert("version", 1);
  root.insert("destination", QString::fromStdString(journal.destination.string()));
  QJsonArray files;
  for (const JournalFile& entry : journal.files) {
    QJsonObject value;
    value.insert("path", QString::fromStdString(entry.file.relative_path));
    value.insert("digest", QString::fromStdString(hexDigest(entry.file.digest)));
    value.insert("size", static_cast<qint64>(entry.file.size));
    value.insert("mode", static_cast<int>(entry.file.mode));
    if (entry.previous) {
      value.insert("previousDigest", QString::fromStdString(hexDigest(*entry.previous)));
    } else {
      value.insert("previousDigest", QJsonValue::Null);
    }
    files.append(value);
  }
  root.insert("files", files);
  return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

void writeAll(int fd, const QByteArray& data, const fs::path& path)
{
  qsizetype offset = 0;
  while (offset < data.size()) {
    const ssize_t count = ::write(fd, data.constData() + offset,
                                  static_cast<size_t>(data.size() - offset));
    if (count < 0) fail("Unable to write", path);
    offset += count;
  }
}

void writeJournal(const fs::path& staging, const Journal& journal)
{
  const fs::path final = staging / StagingPromotion::JournalName;
  const fs::path temp = staging / ".fluorine-promotion-v1.tmp";
  Fd fd(::open(temp.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600));
  if (fd.get() < 0) fail("Unable to create promotion journal", temp);
  writeAll(fd.get(), serialize(journal), temp);
  if (::fsync(fd.get()) != 0) fail("Unable to sync promotion journal", temp);
  if (::rename(temp.c_str(), final.c_str()) != 0) fail("Unable to publish promotion journal", final);
  fsyncDirectory(staging);
}

std::string readSmallFile(const fs::path& path)
{
  Fd fd(::open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW));
  if (fd.get() < 0) fail("Unable to open", path);
  struct stat st {};
  if (::fstat(fd.get(), &st) != 0 || !S_ISREG(st.st_mode) ||
      st.st_size < 0 || st.st_size > 64 * 1024) {
    throw std::runtime_error("Invalid recovery marker: " + path.string());
  }
  std::string value(static_cast<size_t>(st.st_size), '\0');
  size_t offset = 0;
  while (offset < value.size()) {
    const ssize_t count = ::read(fd.get(), value.data() + offset,
                                 value.size() - offset);
    if (count <= 0) fail("Unable to read", path);
    offset += static_cast<size_t>(count);
  }
  return value;
}

Journal readJournal(const fs::path& path)
{
  Fd fd(::open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW));
  if (fd.get() < 0) fail("Unable to open promotion journal", path);
  struct stat st {};
  if (::fstat(fd.get(), &st) != 0 || st.st_size < 0 || st.st_size > 16 * 1024 * 1024) {
    throw std::runtime_error("Invalid promotion journal size: " + path.string());
  }
  QByteArray bytes(static_cast<qsizetype>(st.st_size), Qt::Uninitialized);
  qsizetype offset = 0;
  while (offset < bytes.size()) {
    const ssize_t count = ::read(fd.get(), bytes.data() + offset,
                                 static_cast<size_t>(bytes.size() - offset));
    if (count <= 0) fail("Unable to read promotion journal", path);
    offset += count;
  }
  QJsonParseError error;
  const QJsonDocument document = QJsonDocument::fromJson(bytes, &error);
  const QJsonObject root = document.object();
  if (error.error != QJsonParseError::NoError || root.value("version").toInt() != 1 ||
      !root.value("destination").isString() || !root.value("files").isArray()) {
    throw std::runtime_error("Invalid promotion journal: " + path.string());
  }
  Journal journal;
  std::unordered_set<std::string> seenPaths;
  journal.destination = fs::path(root.value("destination").toString().toStdString());
  for (const QJsonValue& item : root.value("files").toArray()) {
    const QJsonObject value = item.toObject();
    const fs::path relative(value.value("path").toString().toStdString());
    const auto digest = parseDigest(value.value("digest").toString());
    if (!safeRelative(relative) || !digest || !value.value("size").isDouble() ||
        !value.value("mode").isDouble()) {
      throw std::runtime_error("Invalid file entry in promotion journal");
    }
    JournalFile entry;
    entry.file.relative_path = relative.generic_string();
    if (!seenPaths.insert(entry.file.relative_path).second) {
      throw std::runtime_error("Duplicate path in promotion journal");
    }
    entry.file.digest = *digest;
    entry.file.size = static_cast<uint64_t>(value.value("size").toInteger());
    entry.file.mode = static_cast<unsigned int>(value.value("mode").toInt()) & 07777;
    if (!value.value("previousDigest").isNull()) {
      entry.previous = parseDigest(value.value("previousDigest").toString());
      if (!entry.previous) throw std::runtime_error("Invalid previous digest in journal");
    }
    journal.files.push_back(std::move(entry));
  }
  return journal;
}

void copyVerified(const fs::path& source, const fs::path& destination,
                  const fs::path& destinationRoot, const JournalFile& entry)
{
  std::error_code ec;
  fs::create_directories(destination.parent_path(), ec);
  if (ec) throw std::runtime_error("Unable to create destination directory: " + ec.message());
  const fs::path resolvedParent = fs::weakly_canonical(destination.parent_path(), ec);
  if (ec) throw std::runtime_error("Unable to resolve promotion destination parent: " + ec.message());
  const fs::path resolvedRoot = fs::weakly_canonical(destinationRoot, ec);
  if (ec) throw std::runtime_error("Unable to resolve promotion destination root: " + ec.message());
  const fs::path contained = resolvedParent.lexically_relative(resolvedRoot);
  if (contained.is_absolute() ||
      (!contained.empty() && *contained.begin() == "..")) {
    throw std::runtime_error("Promotion destination escaped its configured root: " +
                             destination.string());
  }

  Fd input(::open(source.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW));
  if (input.get() < 0) fail("Unable to open promotion source", source);
  struct stat sourceStat {};
  if (::fstat(input.get(), &sourceStat) != 0 || !S_ISREG(sourceStat.st_mode)) {
    throw std::runtime_error("Promotion source is not a regular file: " + source.string());
  }
  if (static_cast<uint64_t>(sourceStat.st_size) != entry.file.size ||
      hashFd(input.get(), source) != entry.file.digest) {
    throw std::runtime_error("Promotion source no longer matches its journal: " + source.string());
  }

  const fs::path temp = destination.parent_path() /
      ("." + destination.filename().string() + ".fluorine-" +
       hexDigest(entry.file.digest).substr(0, 16) + ".tmp");
  struct stat oldTemp {};
  if (::lstat(temp.c_str(), &oldTemp) == 0) {
    if (!S_ISREG(oldTemp.st_mode) || ::unlink(temp.c_str()) != 0) {
      throw std::runtime_error("Unable to clean interrupted promotion temporary file: " +
                               temp.string());
    }
  } else if (errno != ENOENT) {
    fail("Unable to inspect promotion temporary file", temp);
  }
  Fd output(::open(temp.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW,
                   entry.file.mode == 0 ? 0600 : entry.file.mode));
  if (output.get() < 0) fail("Unable to create promotion temporary file", temp);
  try {
    if (::lseek(input.get(), 0, SEEK_SET) < 0) fail("Unable to seek promotion source", source);
    std::array<unsigned char, 1024 * 1024> buffer{};
    for (;;) {
      const ssize_t count = ::read(input.get(), buffer.data(), buffer.size());
      if (count == 0) break;
      if (count < 0) fail("Unable to read promotion source", source);
      ssize_t offset = 0;
      while (offset < count) {
        const ssize_t written = ::write(output.get(), buffer.data() + offset,
                                        static_cast<size_t>(count - offset));
        if (written < 0) fail("Unable to write promotion destination", temp);
        offset += written;
      }
    }
    ::fchmod(output.get(), entry.file.mode == 0 ? 0600 : entry.file.mode);
    const timespec times[2] = {sourceStat.st_atim, sourceStat.st_mtim};
    ::futimens(output.get(), times);
    if (::fsync(output.get()) != 0) fail("Unable to sync promotion destination", temp);
    if (::rename(temp.c_str(), destination.c_str()) != 0) fail("Unable to install promoted file", destination);
  } catch (...) {
    ::unlink(temp.c_str());
    throw;
  }
  fsyncDirectory(destination.parent_path());
  if (hashStableFile(destination) != entry.file.digest) {
    throw std::runtime_error("Promoted file failed BLAKE3 verification: " + destination.string());
  }
}

bool hasPayload(const fs::path& staging)
{
  std::error_code ec;
  if (!fs::exists(staging, ec)) return false;
  for (auto it = fs::recursive_directory_iterator(
           staging, fs::directory_options::skip_permission_denied, ec);
       !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
    if (it->is_regular_file(ec) || it->is_symlink(ec)) return true;
  }
  return false;
}

fs::path archiveConflict(const fs::path& staging, const Journal* journal,
                         const std::string& reason)
{
  const fs::path root = staging.parent_path() / "VFS_recovery";
  std::error_code ec;
  fs::create_directories(root, ec);
  if (ec) throw std::runtime_error("Unable to create VFS recovery root: " + ec.message());
  const auto ticks = std::chrono::system_clock::now().time_since_epoch().count();
  const fs::path recovery = root / ("promotion-" + std::to_string(ticks));
  fs::create_directories(recovery, ec);
  if (ec) throw std::runtime_error("Unable to create VFS recovery directory: " + ec.message());

  // Publish the durable block marker before moving either side. A crash at
  // any later point must still prevent the next launch from overlooking a
  // partially archived conflict.
  const QByteArray note = QByteArray::fromStdString(reason + "\n");
  const fs::path markerPath = recovery / ".fluorine-unresolved";
  Fd marker(::open(markerPath.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC,
                   0600));
  if (marker.get() < 0) fail("Unable to create unresolved recovery marker", markerPath);
  writeAll(marker.get(), note, markerPath);
  if (::fsync(marker.get()) != 0) fail("Unable to sync unresolved recovery marker", markerPath);
  fsyncDirectory(recovery);
  fsyncDirectory(root);

  const fs::path archivedStaging = recovery / "staging";
  fs::rename(staging, archivedStaging, ec);
  if (ec) {
    ec.clear();
    fs::copy(staging, archivedStaging, fs::copy_options::recursive, ec);
    if (!ec) fs::remove_all(staging, ec);
  }
  if (ec) throw std::runtime_error("Unable to preserve staging conflict: " + ec.message());

  if (journal != nullptr) {
    for (const JournalFile& entry : journal->files) {
      const fs::path source = journal->destination / entry.file.relative_path;
      if (!fs::is_regular_file(source, ec)) continue;
      const fs::path snapshot = recovery / "destination" / entry.file.relative_path;
      fs::create_directories(snapshot.parent_path(), ec);
      fs::copy_file(source, snapshot, fs::copy_options::overwrite_existing, ec);
    }
  }
  const fs::path notePath = recovery / "README.txt";
  Fd fd(::open(notePath.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600));
  if (fd.get() >= 0) {
    writeAll(fd.get(), note, notePath);
    ::fsync(fd.get());
  }
  fsyncDirectory(recovery);
  return recovery;
}

std::optional<fs::path> unresolvedRecovery(const fs::path& staging)
{
  const fs::path root = staging.parent_path() / "VFS_recovery";
  std::error_code ec;
  if (!fs::exists(root, ec)) return std::nullopt;
  for (auto it = fs::recursive_directory_iterator(
           root, fs::directory_options::skip_permission_denied, ec);
       !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
    if (it->path().filename() == ".fluorine-unresolved" &&
        it->is_regular_file(ec)) {
      return it->path().parent_path();
    }
  }
  if (ec) {
    throw std::runtime_error("Unable to inspect VFS recovery directory: " + ec.message());
  }
  return std::nullopt;
}

bool parentContainedBy(const fs::path& path, const fs::path& root)
{
  std::error_code parentError;
  std::error_code rootError;
  const fs::path parent = fs::weakly_canonical(path.parent_path(), parentError);
  const fs::path canonicalRoot = fs::weakly_canonical(root, rootError);
  if (parentError || rootError) return false;
  const fs::path relative = parent.lexically_relative(canonicalRoot);
  return !relative.is_absolute() &&
         (relative.empty() || *relative.begin() != "..");
}

std::optional<Journal> buildJournal(const fs::path& staging,
                                    const fs::path& destination,
                                    bool requireMissingOrIdenticalDestination,
                                    std::string* conflictingPath = nullptr)
{
  std::error_code ec;
  Journal journal;
  journal.destination = canonicalDestination(destination);
  const fs::path stagingRoot = fs::weakly_canonical(staging, ec);
  if (ec) throw std::runtime_error("Unable to resolve staging directory: " + ec.message());
  for (auto it = fs::recursive_directory_iterator(
           staging, fs::directory_options::skip_permission_denied, ec);
       !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
    if (it->is_directory(ec)) continue;
    if (!it->is_regular_file(ec) || it->is_symlink(ec)) {
      throw std::runtime_error("Refusing to promote non-regular staging entry: " +
                               it->path().string());
    }
    const fs::path relative = fs::relative(it->path(), stagingRoot, ec);
    if (ec || !safeRelative(relative)) {
      throw std::runtime_error("Unsafe staging path: " + it->path().string());
    }
    struct stat st {};
    JournalFile entry;
    entry.file.relative_path = relative.generic_string();
    entry.file.digest = hashStableFile(it->path(), &st);
    entry.file.size = static_cast<uint64_t>(st.st_size);
    entry.file.mode = static_cast<unsigned int>(st.st_mode) & 07777;
    entry.previous = existingDigest(journal.destination / relative);
    if (requireMissingOrIdenticalDestination && entry.previous &&
        *entry.previous != entry.file.digest) {
      if (conflictingPath != nullptr) *conflictingPath = entry.file.relative_path;
      return std::nullopt;
    }
    journal.files.push_back(std::move(entry));
  }
  if (ec) throw std::runtime_error("Unable to enumerate staging directory: " + ec.message());
  return journal;
}

void validateJournalDestination(Journal& journal,
                                const fs::path& configuredDestination)
{
  std::error_code ec;
  const fs::path configured = canonicalDestination(configuredDestination);
  const fs::path recorded = fs::weakly_canonical(journal.destination, ec);
  if (ec) {
    throw std::runtime_error("Unable to resolve the recorded promotion destination");
  }
  if (configured != recorded) {
    throw std::runtime_error("The promotion destination changed since the journal was written");
  }
  journal.destination = recorded;
}

StagingPromotionResult replay(const fs::path& staging, const Journal& journal,
                              StagingPromotionStatus completedStatus,
                              bool removeStaging = true,
                              const fs::path& existingRecovery = {})
{
  StagingPromotionResult result;
  result.status = completedStatus;
  const auto block = [&](const std::string& reason) {
    result.status = StagingPromotionStatus::Blocked;
    result.message = reason;
    result.recovery_path = existingRecovery.empty()
        ? archiveConflict(staging, &journal, reason)
        : existingRecovery;
  };
  for (const JournalFile& entry : journal.files) {
    const fs::path source = staging / entry.file.relative_path;
    const fs::path destination = journal.destination / entry.file.relative_path;
    if (!parentContainedBy(source, staging) ||
        !parentContainedBy(destination, journal.destination)) {
      const std::string reason = "A journaled VFS path escaped its configured root. The data was preserved and launch was blocked.";
      block(reason);
      return result;
    }
    const auto destinationDigest = existingDigest(destination);
    if (destinationDigest && *destinationDigest == entry.file.digest) {
      result.files.push_back(entry.file);
      continue;
    }
    const auto sourceDigest = existingDigest(source);
    const bool destinationIsPrevious = entry.previous
        ? destinationDigest && *destinationDigest == *entry.previous
        : !destinationDigest;
    if (!sourceDigest || *sourceDigest != entry.file.digest || !destinationIsPrevious) {
      const std::string reason = "Fluorine could not safely replay a staged write for " +
                                 entry.file.relative_path +
                                 ". Both versions were preserved; resolve them before launching.";
      block(reason);
      return result;
    }
    copyVerified(source, destination, journal.destination, entry);
    result.files.push_back(entry.file);
  }
  if (removeStaging) {
    std::error_code ec;
    fs::remove_all(staging, ec);
    if (ec) throw std::runtime_error("Unable to remove verified staging directory: " + ec.message());
    fsyncDirectory(staging.parent_path());
  }
  return result;
}

std::optional<StagingPromotionResult> recoverUnjournaledArchive(
    const fs::path& staging, const fs::path& recovery,
    const fs::path& configuredDestination)
{
  const fs::path marker = recovery / ".fluorine-unresolved";
  if (readSmallFile(marker) != std::string(UnjournaledReason) + "\n") {
    return std::nullopt;
  }

  const fs::path archivedStaging = recovery / "staging";
  std::error_code ec;
  const bool archived = fs::exists(archivedStaging, ec);
  const fs::path source = archived ? archivedStaging : staging;
  if (!fs::exists(source, ec) || !hasPayload(source)) {
    return std::nullopt;
  }

  Journal journal;
  const fs::path journalPath = source / StagingPromotion::JournalName;
  if (fs::is_regular_file(journalPath, ec)) {
    journal = readJournal(journalPath);
    validateJournalDestination(journal, configuredDestination);
  } else {
    std::string conflict;
    const auto candidate = buildJournal(source, configuredDestination, true, &conflict);
    if (!candidate) {
      StagingPromotionResult result;
      result.status = StagingPromotionStatus::Blocked;
      result.recovery_path = recovery;
      result.message = "Fluorine preserved an unjournaled staged write for " + conflict +
                       " because its destination already contains different data. "
                       "Choose which version to keep before launching.";
      return result;
    }
    journal = *candidate;
    if (journal.files.empty()) return std::nullopt;
    writeJournal(source, journal);
  }

  auto result = replay(source, journal, StagingPromotionStatus::Recovered,
                       false, recovery);
  if (result.blocked()) return result;

  // The destination is fully verified. Remove the block marker first so a
  // crash during archive cleanup cannot turn an already completed recovery
  // back into a permanent launch block.
  if (!fs::remove(marker, ec) || ec) {
    throw std::runtime_error("Unable to clear resolved VFS recovery marker: " +
                             ec.message());
  }
  fsyncDirectory(recovery);
  fs::remove_all(source, ec);
  if (ec) throw std::runtime_error("Unable to remove recovered staging data: " + ec.message());
  fs::remove(recovery / "README.txt", ec);
  ec.clear();
  fs::remove(recovery, ec);
  ec.clear();
  fs::remove(recovery.parent_path(), ec);
  return result;
}

}  // namespace

StagingPromotionResult StagingPromotion::recover(
    const fs::path& staging, const fs::path& configuredDestination)
{
  if (const auto unresolved = unresolvedRecovery(staging)) {
    if (const auto recovered = recoverUnjournaledArchive(
            staging, *unresolved, configuredDestination)) {
      return *recovered;
    }
    StagingPromotionResult result;
    result.status = StagingPromotionStatus::Blocked;
    result.recovery_path = *unresolved;
    result.message = "Fluorine has an unresolved VFS recovery. Reconcile the preserved files and remove .fluorine-unresolved before launching.";
    return result;
  }
  std::error_code ec;
  if (!fs::exists(staging, ec)) return {};
  if (!hasPayload(staging)) {
    fs::remove_all(staging, ec);
    return {};
  }

  const fs::path journalPath = staging / JournalName;
  if (!fs::is_regular_file(journalPath, ec)) {
    std::string conflict;
    const auto journal = buildJournal(staging, configuredDestination, true,
                                      &conflict);
    if (journal && !journal->files.empty()) {
      writeJournal(staging, *journal);
      return replay(staging, *journal, StagingPromotionStatus::Recovered);
    }
    StagingPromotionResult result;
    result.status = StagingPromotionStatus::Blocked;
    result.message = conflict.empty()
        ? UnjournaledReason
        : "Fluorine found an unjournaled staged write for " + conflict +
          " whose destination contains different data. Both were preserved and launch was blocked.";
    result.recovery_path = archiveConflict(staging, nullptr, result.message);
    return result;
  }

  Journal journal;
  try {
    journal = readJournal(journalPath);
    validateJournalDestination(journal, configuredDestination);
  } catch (const std::exception& error) {
    StagingPromotionResult result;
    result.status = StagingPromotionStatus::Blocked;
    result.message = std::string("Fluorine found an invalid VFS promotion journal: ") + error.what();
    result.recovery_path = archiveConflict(staging, nullptr, result.message);
    return result;
  }
  return replay(staging, journal, StagingPromotionStatus::Recovered);
}

StagingPromotionResult StagingPromotion::promote(
    const fs::path& staging, const fs::path& destination)
{
  std::error_code ec;
  if (!fs::exists(staging, ec) || !hasPayload(staging)) {
    if (fs::exists(staging, ec)) fs::remove_all(staging, ec);
    return {};
  }
  if (fs::exists(staging / JournalName, ec)) {
    return recover(staging, destination);
  }

  Journal journal = *buildJournal(staging, destination, false);
  if (journal.files.empty()) {
    fs::remove_all(staging, ec);
    return {};
  }
  writeJournal(staging, journal);
  return replay(staging, journal, StagingPromotionStatus::Promoted);
}
