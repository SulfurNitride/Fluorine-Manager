#include <uibase/ifiletree.h>

#include <algorithm>
#include <ranges>
#include <span>
#include <stack>

#include <QRegularExpression>

// FileTreeEntry:
namespace MOBase
{
FileTreeEntry::FileTreeEntry(std::shared_ptr<const IFileTree> parent, QString name)
    : m_Parent(parent), m_Name(name)
{}

QString FileTreeEntry::suffix() const
{
  const qsizetype idx = m_Name.lastIndexOf(".");
  return (isDir() || idx == -1) ? "" : m_Name.mid(idx + 1);
}

bool FileTreeEntry::hasSuffix(QString suffix) const
{
  return this->suffix().compare(suffix, FileNameComparator::CaseSensitivity) == 0;
}

bool FileTreeEntry::hasSuffix(QStringList suffixes) const
{
  return suffixes.contains(suffix(), FileNameComparator::CaseSensitivity);
}

QString FileTreeEntry::pathFrom(std::shared_ptr<const IFileTree> tree,
                                QString sep) const
{

  // We will construct the path from right to left:
  QString path = name();

  auto p = parent();
  while (p != nullptr && p != tree) {
    // We need to check the parent, otherwize we are going to prepend the name
    // and a / for the base, which we do not want.
    if (p->parent() != nullptr) {
      path = p->name() + sep + path;
    }
    p = p->parent();
  }

  return p == tree ? path : QString();
}

bool FileTreeEntry::detach()
{
  auto p = parent();
  if (p == nullptr) {
    return false;
  }
  return p->erase(shared_from_this()) != p->end();
}

bool FileTreeEntry::moveTo(std::shared_ptr<IFileTree> tree)
{
  return tree->insert(shared_from_this()) != tree->end();
}

std::shared_ptr<FileTreeEntry> FileTreeEntry::clone() const
{
  return createFileEntry(nullptr, name());
}

std::shared_ptr<FileTreeEntry>
FileTreeEntry::createFileEntry(std::shared_ptr<const IFileTree> parent, QString name)
{
  return std::shared_ptr<FileTreeEntry>(new FileTreeEntry(parent, name));
}
}  // namespace MOBase

// IFileTree:
namespace MOBase
{

/**
 * Comparator for file entries.
 */
struct FileEntryComparator
{
  bool operator()(std::shared_ptr<FileTreeEntry> const& a,
                  std::shared_ptr<FileTreeEntry> const& b) const
  {
    if (a->isDir() && !b->isDir()) {
      return true;
    } else if (!a->isDir() && b->isDir()) {
      return false;
    } else {
      return FileNameComparator::compare(a->name(), b->name()) < 0;
    }
  }
};

/**
 * @brief Comparator that can be used to find entry matching the given name and
 *     file type.
 */
struct MatchEntryComparator
{

  MatchEntryComparator(QString const& name, FileTreeEntry::FileTypes matchTypes)
      : m_Name(name), m_MatchTypes(matchTypes)
  {}

  bool operator()(const std::shared_ptr<const FileTreeEntry>& fileEntry) const
  {
    return m_MatchTypes.testFlag(fileEntry->fileType()) &&
           fileEntry->compare(m_Name) == 0;
  }

  bool operator()(const std::shared_ptr<FileTreeEntry>& fileEntry) const
  {
    return m_MatchTypes.testFlag(fileEntry->fileType()) &&
           fileEntry->compare(m_Name) == 0;
  }

private:
  QString const& m_Name;
  FileTreeEntry::FileTypes m_MatchTypes;
};

/**
 *
 */
bool IFileTree::exists(QString path, FileTypes type) const
{
  return find(path, type) != nullptr;
}

/**
 *
 */
std::shared_ptr<FileTreeEntry> IFileTree::find(QString path, FileTypes type)
{
  return fetchEntry(splitPath(path), type);
}
std::shared_ptr<const FileTreeEntry> IFileTree::find(QString path, FileTypes type) const
{
  return fetchEntry(splitPath(path), type);
}

/**
 *
 */
void IFileTree::walk(
    std::function<WalkReturn(QString const&, std::shared_ptr<const FileTreeEntry>)>
        callback,
    QString sep) const
{

  std::stack<std::pair<QString, std::shared_ptr<const FileTreeEntry>>> stack;

  // We start by pushing all the entries in this tree, this avoid having to do extra
  // check later for avoid leading separator:
  for (auto rit = rbegin(); rit != rend(); ++rit) {
    stack.push({"", *rit});
  }

  while (!stack.empty()) {
    auto [path, entry] = stack.top();
    stack.pop();

    auto res = callback(path, entry);
    if (res == WalkReturn::STOP) {
      break;
    }
    if (entry->isDir() && res != WalkReturn::SKIP) {
      auto tree = entry->astree();
      for (auto rit = tree->rbegin(); rit != tree->rend(); ++rit) {
        stack.push({path + tree->name() + sep, *rit});
      }
    }
  }
}

/**
 *
 */
std::shared_ptr<FileTreeEntry> IFileTree::addFile(QString path, bool replaceIfExists)
{
  QStringList parts = splitPath(path);

  // Check if the file already exists:
  auto existingEntry = fetchEntry(parts, IFileTree::FILE_OR_DIRECTORY);
  if (!replaceIfExists && existingEntry != nullptr) {
    return nullptr;
  }

  // Find or create the tree:
  IFileTree* tree;

  if (parts.size() > 1) {

    // Create the tree:
    std::shared_ptr<FileTreeEntry> treeEntry =
        createTree(parts.begin(), parts.end() - 1);

    // Early fail if the tree was not created:
    if (treeEntry == nullptr) {
      return nullptr;
    }

    tree = treeEntry->astree().get();
  } else {
    tree = this;
  }

  std::shared_ptr<FileTreeEntry> entry =
      tree->makeFile(tree->astree(), parts[parts.size() - 1]);

  // If makeFile returns a null pointer, it means we cannot create file:
  if (entry == nullptr) {
    return nullptr;
  }

  // Remove the existing files if there was one:
  if (existingEntry) {
    existingEntry->detach();
  }

  // Insert in the tree:
  tree->entries().insert(
      std::upper_bound(tree->begin(), tree->end(), entry, FileEntryComparator{}),
      entry);

  return entry;
}

/**
 *
 */
std::shared_ptr<IFileTree> IFileTree::addDirectory(QString path)
{
  QStringList parts = splitPath(path);
  return createTree(parts.begin(), parts.end());
}

/**
 *
 */
IFileTree::iterator IFileTree::insert(std::shared_ptr<FileTreeEntry> entry,
                                      InsertPolicy insertPolicy)
{

  // Check that this is not the current tree or a parent tree:
  if (entry->isDir()) {
    std::shared_ptr<IFileTree> tmp = astree();
    while (tmp != nullptr) {
      if (tmp == entry->astree()) {
        return end();
      }
      tmp = tmp->parent();
    }
  }

  // Check if there exists an entry with the same name:
  auto existingIt = std::find_if(
      begin(), end(), MatchEntryComparator{entry->name(), FILE_OR_DIRECTORY});

  // Already in the tree?
  if (existingIt != end() && *existingIt == entry) {
    return existingIt;
  }

  auto insertionIt = end();

  if (existingIt != end()) {
    if (insertPolicy == InsertPolicy::FAIL_IF_EXISTS) {
      return end();
    }

    // We replace if the policy is REPLACE or if the new and old entry are
    // both files:
    if (insertPolicy == InsertPolicy::REPLACE ||
        ((*existingIt)->isFile() && entry->isFile())) {
      if (beforeReplace(this, existingIt->get(), entry.get())) {
        // Detach the old entry from its parent (not using .detach()
        // to remove the entry since we are replacing it):
        (*existingIt)->m_Parent.reset();
        entries().erase(existingIt);

        // insert at the right place
        insertionIt = entries().insert(
            std::lower_bound(begin(), end(), entry, FileEntryComparator{}), entry);
      } else {
        return end();
      }
    } else if ((*existingIt)->isFile() || entry->isFile()) {
      // If we arrive here and one of the entry is a file, we fail:
      return end();
    } else {
      // If we end up here, we know that the policy is MERGE and that both
      // are directory that can be merged:
      mergeTree((*existingIt)->astree(), entry->astree(), nullptr);
      insertionIt = existingIt;
    }
  } else if (beforeInsert(this, entry.get())) {
    insertionIt = entries().insert(
        std::lower_bound(begin(), end(), entry, FileEntryComparator{}), entry);
  }

  // Remove the tree from its parent (parent() can be null if we are inserting
  // a new tree):
  if (entry->parent() != nullptr) {
    entry->parent()->erase(entry);
  }

  // If the entry was actually inserted, we update its parent:
  if (*insertionIt == entry) {
    entry->m_Parent = astree();
  }
  // Otherwize, we reset it (if this was a merge operation):
  else {
    entry->m_Parent.reset();
  }

  return insertionIt;
}

/**
 *
 */
bool IFileTree::move(std::shared_ptr<FileTreeEntry> entry, QString path,
                     InsertPolicy insertPolicy)
{

  // Check that this is not a parent tree:
  if (entry->isDir()) {
    std::shared_ptr<IFileTree> tmp = parent();
    while (tmp != nullptr) {
      if (tmp == entry->astree()) {
        return false;
      }
      tmp = tmp->parent();
    }
  }

  // Insert in folder or replace:
  const bool insertFolder = path.isEmpty() || path.endsWith("/") || path.endsWith("\\");

  // Retrieve the path:
  QStringList parts = splitPath(path);

  // Backup the entry name (in case the insertion fails), and update the
  // name:
  QString entryName = entry->m_Name;
  if (!insertFolder) {
    entry->m_Name = parts.takeLast();
  }

  // Find or create the tree:
  IFileTree* tree;

  if (!parts.isEmpty()) {

    // Create the tree:
    std::shared_ptr<FileTreeEntry> treeEntry = createTree(parts.begin(), parts.end());

    // Early fail if the tree was not created:
    if (treeEntry == nullptr) {
      return false;
    }

    tree = treeEntry->astree().get();
  } else {
    tree = this;
  }

  // We try to insert, and if it fails we need to reset the name:
  auto it = tree->insert(entry, insertPolicy);
  if (it == tree->end()) {
    entry->m_Name = entryName;
    return false;
  }

  return true;
}

/**
 *
 */
std::shared_ptr<FileTreeEntry>
IFileTree::copy(std::shared_ptr<const FileTreeEntry> entry, QString path,
                InsertPolicy insertPolicy)
{
  // Note: If a conflict exists, the tree is cloned before checking the conflict, so
  // this is not the most efficient way but copying tree should be pretty rare (and
  // probably avoided anyway), and this allow us to use `move()` to do all the complex
  // operations.
  auto clone = entry->clone();
  if (move(clone, path, insertPolicy)) {
    return clone;
  }
  return nullptr;
}

/**
 *
 */
std::size_t IFileTree::merge(std::shared_ptr<IFileTree> source,
                             OverwritesType* overwrites)
{

  // Check that this is not a parent tree:
  std::shared_ptr<IFileTree> tmp = astree();
  while (tmp != nullptr) {
    if (tmp == source) {
      return MERGE_FAILED;
    }
    tmp = tmp->parent();
  }

  return mergeTree(astree(), source, overwrites);
}

/**
 *
 */
IFileTree::iterator IFileTree::erase(std::shared_ptr<FileTreeEntry> entry)
{

  if (!beforeRemove(this, entry.get())) {
    return end();
  }

  auto it = std::find(begin(), end(), entry);
  if (it == end()) {
    return it;
  }
  entry->m_Parent.reset();
  return entries().erase(it);
  ;
}

/**
 *
 */
std::pair<IFileTree::iterator, std::shared_ptr<FileTreeEntry>>
IFileTree::erase(QString name)
{
  auto it = std::find_if(begin(), end(), [&name](const auto& entry) {
    return entry->compare(name) == 0;
  });

  if (it == end()) {
    return {it, nullptr};
  }

  if (!beforeRemove(this, it->get())) {
    return {end(), nullptr};
  }

  // Save the entry to return it:
  auto entry = *it;
  entry->m_Parent.reset();

  return {entries().erase(it), entry};
}

/**
 *
 */
bool IFileTree::clear()
{
  // Need to find the iterator up to which we should erase:
  auto& entries_ = entries();
  auto it        = entries_.begin();
  for (; it != entries_.end() && beforeRemove(this, it->get()); ++it) {
    // Detach (but not remove from the vector):
    (*it)->m_Parent.reset();
  }
  entries_.erase(entries_.begin(), it);
  return empty();
}

/**
 *
 */
std::size_t IFileTree::removeAll(QStringList names)
{
  return removeIf([&names](auto& entry) {
    return names.contains(entry->name(), Qt::CaseInsensitive);
  });
}

/**
 *
 */
std::size_t IFileTree::removeIf(
    std::function<bool(std::shared_ptr<FileTreeEntry> const&)> predicate)
{
  std::size_t osize = size();
  auto& en          = entries();
  // Cannot use begin() and end() directly because those are immutable iterators:
  en.erase(std::remove_if(en.begin(), en.end(),
                          [this, &predicate](auto& entry) {
                            return beforeRemove(this, entry.get()) && predicate(entry);
                          }),
           en.end());
  return osize - size();
}

/**
 *
 */
QStringList IFileTree::splitPath(QString path)
{
  // Using raw \\ instead of QDir::separator() since we are replacing by /
  // anyway, and this avoid pulling an extra header (like QDir) only
  // for the separator.
  return path.replace("\\", "/").split("/", Qt::SkipEmptyParts);
}

/**
 *
 */
bool IFileTree::beforeReplace(IFileTree const*, FileTreeEntry const*,
                              FileTreeEntry const*)
{
  return true;
}

/**
 *
 */
bool IFileTree::beforeInsert(IFileTree const*, FileTreeEntry const*)
{
  return true;
}

/**
 *
 */
bool IFileTree::beforeRemove(IFileTree const*, FileTreeEntry const*)
{
  return true;
}

/**
 *
 */
std::size_t IFileTree::mergeTree(std::shared_ptr<IFileTree> destination,
                                 std::shared_ptr<IFileTree> source,
                                 OverwritesType* overwrites)
{

  const auto comp = FileEntryComparator{};

  // Number of overwritten entries:
  std::size_t noverwrites = 0;

  // Note: Using the iterators from the vector directly since the other ones
  // cannot be assigned to.
  auto &dstEntries = destination->entries(), &srcEntries = source->entries();

  // Note: Since entries are not sorted by name but by type (file/directory)
  // then name, there is no fast way to check for conflict.
  for (auto& srcEntry : srcEntries) {

    // Try to find an exact match (name and type) - This iterator also
    // serve to know where the entry should be inserted:
    auto dstIt = std::lower_bound(dstEntries.begin(), dstEntries.end(), srcEntry, comp);

    // Exact match found:
    if (dstIt != dstEntries.end() && (*dstIt)->compare(srcEntry->name()) == 0 &&
        (*dstIt)->isFile() == srcEntry->isFile()) {

      // Both directory, we merge:
      if ((*dstIt)->isDir() && srcEntry->isDir()) {
        noverwrites += mergeTree((*dstIt)->astree(), srcEntry->astree(), overwrites);

        // Detach the entry:
        srcEntry->m_Parent.reset();
      }
      // Otherwize, check if the source can replace the destination:
      else if (beforeReplace(destination.get(), dstIt->get(), srcEntry.get())) {
        // Remove the parent:
        auto dstEntry = *dstIt;
        dstEntry->m_Parent.reset();

        // Update overwrites information:
        noverwrites++;
        if (overwrites != nullptr) {
          overwrites->insert({dstEntry, srcEntry});
        }

        // Replace the destination:
        *dstIt             = srcEntry;
        srcEntry->m_Parent = destination;
      }
      // If not, fails:
      else {
        return MERGE_FAILED;
      }
    } else {
      // If we did not find a match, the only way to check is to look
      // through the vector:
      auto conflictIt = std::find_if(dstEntries.begin(), dstEntries.end(),
                                     [name = srcEntry->name()](auto const& dstEntry) {
                                       return dstEntry->compare(name) == 0;
                                     });

      // Conflict (note that here both entries are of different types, so no need to
      // check if we replace or merge):
      int deleteIndex = -1;
      if (conflictIt != dstEntries.end()) {

        // We check if we can replace the entry:
        if (!beforeReplace(destination.get(), conflictIt->get(), srcEntry.get())) {
          return MERGE_FAILED;
        }

        // We need to store the index because the insert() will mess up the
        // iterators:
        deleteIndex = static_cast<int>(conflictIt - std::begin(dstEntries));
        if (dstIt < conflictIt) {
          deleteIndex += 1;
        }

        // Detach the conflicting entry (we erase it later, after the insertion):
        (*conflictIt)->m_Parent.reset();

        // Update overwrites information:
        noverwrites++;
        if (overwrites != nullptr) {
          overwrites->insert({*conflictIt, srcEntry});
        }
      }
      // No conflict, we still have to check if we can insert:
      else if (!beforeInsert(destination.get(), srcEntry.get())) {
        return MERGE_FAILED;
      }

      // Insert the entry using the previous iterator:
      dstEntries.insert(dstIt, srcEntry);

      // We delete here:
      if (deleteIndex != -1) {
        dstEntries.erase(dstEntries.begin() + deleteIndex);
      }

      // Update the parent:
      srcEntry->m_Parent = destination;
    }
  }

  // Clear the sources:
  srcEntries.clear();

  return noverwrites;
}

/**
 *
 */
std::shared_ptr<IFileTree> IFileTree::createOrphanTree(QString name) const
{
  auto directory = makeDirectory(nullptr, name);
  if (directory != nullptr) {
    directory->m_Populated = true;
  }
  return directory;
}

/**
 *
 */
std::shared_ptr<FileTreeEntry> IFileTree::fetchEntry(QStringList path,
                                                     FileTypes matchTypes)
{
  return std::const_pointer_cast<FileTreeEntry>(
      const_cast<const IFileTree*>(this)->fetchEntry(path, matchTypes));
}
std::shared_ptr<const FileTreeEntry> IFileTree::fetchEntry(QStringList const& path,
                                                           FileTypes matchTypes) const
{
  // Check to ensure that the path contains at least one element:
  if (path.isEmpty()) {
    return nullptr;
  }

  // Early check:
  if (path[path.size() - 1].startsWith("*")) {
    return nullptr;
  }

  const IFileTree* tree = this;
  auto it               = std::begin(path);
  for (; tree != nullptr && it != std::end(path) - 1; ++it) {
    // Special cases:
    if (*it == ".") {
      continue;
    } else if (*it == "..") {
      tree = tree->parent().get();
    } else {
      // Find the entry at the current level:
      auto entryIt = std::find_if(tree->begin(), tree->end(),
                                  MatchEntryComparator{*it, IFileTree::DIRECTORY});

      // Early exists if the entry does not exist or is not a directory:
      if (entryIt == tree->end()) {
        tree = nullptr;
      } else {
        tree = (*entryIt)->astree().get();
      }
    }
  }

  if (tree == nullptr) {
    return nullptr;
  }

  // We have the final tree:
  auto entryIt =
      std::find_if(tree->begin(), tree->end(), MatchEntryComparator{*it, matchTypes});
  auto bIt = tree->end();
  return entryIt == bIt ? nullptr : *entryIt;
}

/**
 * @brief Create a new file under this tree.
 *
 * @param parent The current tree, without const-qualification.
 * @param name Name of the file.
 * @param time Modification time of the file.
 *
 * @return the created file.
 */
std::shared_ptr<FileTreeEntry>
IFileTree::makeFile(std::shared_ptr<const IFileTree> parent, QString name) const
{
  return createFileEntry(parent, name);
}

/**
 *
 */
IFileTree::IFileTree() {}

/**
 *
 */
std::shared_ptr<FileTreeEntry> IFileTree::clone() const
{
  std::shared_ptr<IFileTree> tree = doClone();

  // Don't copy not populated tree, it is not useful:
  if (m_Populated) {
    tree->m_Populated = true;
    auto& tentries    = tree->m_Entries;
    for (auto e : entries()) {
      auto ce      = e->clone();
      ce->m_Parent = tree;
      tentries.push_back(ce);
    }
  }

  return tree;
}

/**
 *
 */
std::shared_ptr<IFileTree> IFileTree::createTree(QStringList::const_iterator begin,
                                                 QStringList::const_iterator end)
{
  // The current tree and entry:
  std::shared_ptr<IFileTree> tree = astree();
  for (auto it = begin; tree != nullptr && it != end; ++it) {
    // Special cases:
    if (*it == ".") {
      continue;
    } else if (*it == "..") {
      // parent() returns nullptr if it does not exist, so no
      // check required:
      tree = parent();
    } else {

      // Check if the entry exists (looking for both files and directories
      // because we don't want to override a file):
      auto entryIt =
          std::find_if(tree->begin(), tree->end(),
                       MatchEntryComparator{*it, IFileTree::FILE_OR_DIRECTORY});

      // Create if it does not:
      if (entryIt == tree->end()) {
        auto newTree = tree->makeDirectory(tree, *it);

        // If makeDirectory returns a null pointer, it means we cannot create tree.
        if (newTree == nullptr) {
          tree = nullptr;
          break;
        }

        // The tree is empty so already populated:
        newTree->m_Populated = true;

        tree->entries().insert(std::upper_bound(tree->begin(), tree->end(), newTree,
                                                FileEntryComparator{}),
                               newTree);
        tree = newTree;
      } else if ((*entryIt)->isDir()) {
        tree = (*entryIt)->astree();
      } else {  // Cannot go further:
        tree = nullptr;
      }
    }
  }

  return tree;
}

/**
 * @brief Retrieve the vector of entries after populating it if required.
 *
 * @return the vector of entries.
 */
std::vector<std::shared_ptr<FileTreeEntry>>& IFileTree::entries()
{
  std::call_once(m_OnceFlag, [this]() {
    populate();
  });
  return m_Entries;
}
const std::vector<std::shared_ptr<FileTreeEntry>>& IFileTree::entries() const
{
  std::call_once(m_OnceFlag, [this]() {
    populate();
  });
  return m_Entries;
}

/**
 * @brief Populate the internal vectors and update the flag.
 */
void IFileTree::populate() const
{
  // Need to check m_Populated again here since the tree can be populated without
  // a call to entries() (e.g., on copy/orphanTree):
  if (!m_Populated) {
    if (!doPopulate(astree(), m_Entries)) {
      std::sort(std::begin(m_Entries), std::end(m_Entries), FileEntryComparator{});
    }
    m_Populated = true;
  }
}

// walk and glob with generator

std::generator<std::shared_ptr<const FileTreeEntry>>
walk(std::shared_ptr<const IFileTree> fileTree)
{
  std::stack<std::shared_ptr<const FileTreeEntry>> stack;

  // we start by pushing all the entries in this tree, this avoid having to do extra
  // check later to avoid leading separator
  for (auto rit = fileTree->rbegin(); rit != fileTree->rend(); ++rit) {
    stack.push(*rit);
  }

  while (!stack.empty()) {
    auto entry = stack.top();
    stack.pop();

    co_yield entry;

    if (entry->isDir()) {
      auto tree = entry->astree();
      for (auto rit = tree->rbegin(); rit != tree->rend(); ++rit) {
        stack.push(*rit);
      }
    }
  }
}

namespace
{
  using glob_stack_t = std::stack<
      std::pair<std::shared_ptr<const FileTreeEntry>, std::span<QRegularExpression>>>;

  // check the given entry against the list of patterns and add new (entry, patterns) to
  // the given stack
  //
  std::generator<std::shared_ptr<const FileTreeEntry>>
  ifiletree_glob_impl_step(glob_stack_t& glob_stack,
                           std::shared_ptr<const FileTreeEntry> entry,
                           std::span<QRegularExpression> const& patterns)
  {
    // no more patterns, nothing to do
    if (patterns.size() == 0) {
      co_return;
    }

    // special handling if the pattern is empty (correspond to a '**' glob)
    if (patterns[0].pattern() == "") {

      // if there are more patterns after '**', we need to check the entry again, e.g.,
      // if the entry name is 'x' and the pattern is '**/x' to match it
      if (patterns.size() != 1) {
        glob_stack.emplace(entry, patterns.subspan(1));
      }

      // if the entry is a file, there is nothing to do with '**'
      if (entry->isFile()) {
        co_return;
      }

      // if this is the end of the patterns list, we need to yield the current entry
      // since it is a directory
      if (patterns.size() == 1) {
        co_yield entry;
      }

      // recurse over childs, but for directories, we need to keep the leading '**' in
      // the list of patterns since '**' can match multiple level of directories
      auto tree = entry->astree();
      for (auto rit = tree->rbegin(); rit != tree->rend(); ++rit) {
        glob_stack.emplace(*rit, (*rit)->isDir() ? patterns : patterns.subspan(1));
      }
    }
    // otherwise (if the first patterns is not '**'), we simply check if we have a match
    else if (patterns[0].match(entry->name()).hasMatch()) {
      // this was the last pattern and we have a match, so we yield the current entry,
      // not that this will yield intermediate matching directory, but this is expected
      if (patterns.size() == 1) {
        co_yield entry;
      }

      // if the entry is not a directory, we have nothing more to do
      if (entry->isFile()) {
        co_return;
      }

      // if all that remain after this pattern is a '**', we need to yield the current
      // entry since '**' can also match an empty succession of directories, e.g. 'a/b'
      // is matched by 'a/b/**'
      if (patterns.size() == 2 && patterns[1].pattern() == "") {
        co_yield entry;
      }

      // we then need to recurse over
      auto tree = entry->astree();
      for (auto rit = tree->rbegin(); rit != tree->rend(); ++rit) {
        glob_stack.emplace(*rit, patterns.subspan(1));
      }
    }
  }

  // main function for IFileTree::glob - this function simply manages the stack of of
  // entry / patterns to handle, and then falls back to ifiletree_glob_impl_step
  //
  std::generator<std::shared_ptr<const FileTreeEntry>>
  ifiletree_glob_impl(std::shared_ptr<const FileTreeEntry> entry,
                      std::span<QRegularExpression> const& patterns)
  {
    glob_stack_t stack;
    stack.emplace(entry, patterns);

    while (!stack.empty()) {
      const auto [childEntry, childPatterns] = stack.top();
      stack.pop();
      co_yield std::ranges::elements_of(
          ifiletree_glob_impl_step(stack, childEntry, childPatterns));
    }
  }

}  // namespace

/**
 *
 */
std::generator<std::shared_ptr<const FileTreeEntry>>
glob(std::shared_ptr<const IFileTree> fileTree, QString pattern,
     GlobPatternType patternType)
{
  // replace \\ by / to simply handling
  pattern = pattern.trimmed().replace("\\", "/");

  // reduce successions of **/** to **, this makes it easier to handle it in the
  // actual implementation
  pattern = pattern.replace(QRegularExpression("(\\*\\*/)*\\*\\*"), "**");

  // we are going to match directly starting from the child of the tree, so we need
  // to handle the root here
  //
  // note: this is the only pattern that can match the tree itself
  if (pattern == "**") {
    co_yield fileTree;
  }

  // split pattern into blocks, we keep
  const auto regexOptions = FileNameComparator::CaseSensitivity == Qt::CaseInsensitive
                                ? QRegularExpression::CaseInsensitiveOption
                                : QRegularExpression::NoPatternOption;
  std::vector<QRegularExpression> patterns;
  for (const auto& part : pattern.split("/")) {
    if (part == "**") {
      // for '**' pattern, push an empty regex
      patterns.emplace_back();
    } else {
      const auto regexpPattern =
          patternType == GlobPatternType::GLOB
              ? QRegularExpression::wildcardToRegularExpression(
                    part, QRegularExpression::NonPathWildcardConversion)
              : part;
      const QRegularExpression regex(regexpPattern, regexOptions);

      if (!regex.isValid()) {
        throw InvalidGlobPatternException(regex.errorString());
      }

      patterns.push_back(regex);
    }
  }

  // fall back to the main glob function for the child of this tree
  for (const auto& child : *fileTree) {
    co_yield std::ranges::elements_of(ifiletree_glob_impl(child, patterns));
  }
}

}  // namespace MOBase
