#pragma warning(push)
#pragma warning(disable : 4668)
#include <gtest/gtest.h>
#pragma warning(pop)

#include <algorithm>
#include <ranges>
#include <string>
#include <unordered_set>
#include <variant>

#include <uibase/ifiletree.h>

std::ostream& operator<<(std::ostream& os, const QString& str)
{
  return os << str.toStdString();
}

using namespace MOBase;

namespace std
{
// If you can't declare the function in the class it's important that PrintTo()
// is defined in the SAME namespace that defines Point.  C++'s look-up rules
// rely on that.
void PrintTo(std::shared_ptr<const FileTreeEntry> entry, std::ostream* os)
{
  *os << entry->pathFrom(nullptr, "/");
}
}  // namespace std

/**
 *
 */
struct FileListTree : public IFileTree
{
  using File = std::pair<QStringList, bool>;

  std::shared_ptr<IFileTree> makeDirectory(std::shared_ptr<const IFileTree> parent,
                                           QString name,
                                           std::vector<File>&& files) const
  {
    return std::shared_ptr<FileListTree>(
        new FileListTree(parent, name, std::move(files)));
  }

  std::shared_ptr<IFileTree> makeDirectory(std::shared_ptr<const IFileTree> parent,
                                           QString name) const
  {
    return std::shared_ptr<FileListTree>(new FileListTree(parent, name));
  }

  bool populated() const { return m_Populated; }

  virtual bool doPopulate(std::shared_ptr<const IFileTree> parent,
                          std::vector<std::shared_ptr<FileTreeEntry>>& entries) const
  {
    // We know that the files are sorted:
    QString currentName = "";
    std::vector<File> currentFiles;
    for (auto& p : m_Files) {
      if (currentName == "") {
        currentName = p.first[0];
      }

      if (currentName != p.first[0]) {
        entries.push_back(makeDirectory(parent, currentName, std::move(currentFiles)));
        currentFiles.clear();
      }

      currentName = p.first[0];

      if (p.first.size() == 1) {
        if (!p.second) {
          entries.push_back(makeFile(parent, currentName));
          currentName = "";
        }
      } else {
        currentFiles.push_back(
            {QStringList(p.first.begin() + 1, p.first.end()), p.second});
      }
    }

    if (currentName != "") {
      entries.push_back(makeDirectory(parent, currentName, std::move(currentFiles)));
    }

    m_Populated = true;

    return false;
  }

  virtual std::shared_ptr<IFileTree> doClone() const override
  {
    return std::shared_ptr<FileListTree>(new FileListTree(nullptr, name(), m_Files));
  }

public:
  static std::shared_ptr<IFileTree>
  makeTree(std::vector<std::pair<QString, bool>>&& files)
  {
    std::sort(std::begin(files), std::end(files),
              [](const std::pair<QString, bool>& a, const std::pair<QString, bool>& b) {
                return FileNameComparator::compare(a.first, b.first) < 0;
              });

    std::vector<File> pFiles;

    for (auto p : files) {
      pFiles.push_back({p.first.split("/", Qt::SkipEmptyParts), p.second});
    }

    return std::shared_ptr<FileListTree>(
        new FileListTree(nullptr, "", std::move(pFiles)));
  }

protected:
  FileListTree(std::shared_ptr<const IFileTree> parent, QString name)
      : FileTreeEntry(parent, name), IFileTree()
  {}
  FileListTree(std::shared_ptr<const IFileTree> parent, QString name,
               std::vector<File> const& files)
      : FileTreeEntry(parent, name), IFileTree(), m_Files(files)
  {}
  FileListTree(std::shared_ptr<const IFileTree> parent, QString name,
               std::vector<File>&& files)
      : FileTreeEntry(parent, name), IFileTree(), m_Files(std::move(files))
  {}

  mutable bool m_Populated = false;
  std::vector<File> m_Files;
};

/**
 * @brief Check if the given tree has been populated.
 *
 * Since IFileTree does not expose the "populated" flag, this is a convenient
 * method that simply downcast to `FileListTree` and check `populated()` on it.
 *
 * @param tree The tree to check.
 *
 * @return true if the tree has been populated, false otherwize.
 */
bool populated(std::shared_ptr<const IFileTree> tree)
{
  return std::dynamic_pointer_cast<const FileListTree>(tree)->populated();
}

/**
 * @brief Retrieve all the entry in the given tree.
 *
 * @param fileTree The tree to get the entries from.
 *
 * @return a vector containing all the entries in the tree.
 */
std::vector<std::shared_ptr<const FileTreeEntry>>
getAllEntries(std::shared_ptr<const IFileTree> fileTree)
{
  std::vector<std::shared_ptr<const FileTreeEntry>> entries;
  for (auto entry : *fileTree) {
    entries.push_back(entry);
    if (entry->isDir()) {
      auto childEntries = getAllEntries(entry->astree());
      entries.insert(entries.end(), childEntries.begin(), childEntries.end());
    }
  }
  return entries;
}

/**
 * @brief Check that the given file tree match the given entries.
 *
 * This is probably pretty slow but it is only for unit testing. This will check
 * both way: all entries in the vector must be in the tree at the right place, and
 * all entries in the tree must be in the vector.
 *
 * @param fileTree The tree to check.
 * @param entries The entries to check. Filenames must be separated by /. Must contain
 *     all the entry, including intermediate directories, except the root.
 *
 */
void assertTreeEquals(std::shared_ptr<const IFileTree> fileTree,
                      std::vector<std::pair<QString, bool>> const& entries)
{
  // Check that all entries are in the tree:
  for (auto& entry : entries) {
    auto treeEntry = fileTree->find(entry.first);
    ASSERT_NE(treeEntry, nullptr)
        << "Entry " << entry.first << " not found in the tree.";
    ASSERT_EQ(entry.second, treeEntry->isDir())
        << "Entry " << entry.first << " is not of the right type.";
  }

  // Check that all entries in the tree are in the vector:
  auto treeEntries = getAllEntries(fileTree);
  for (auto& entry : treeEntries) {
    auto path = entry->pathFrom(fileTree, "/");
    auto it   = std::find_if(entries.begin(), entries.end(), [&path](auto const& p) {
      return p.first.compare(path, Qt::CaseInsensitive) == 0;
    });
    ASSERT_NE(it, entries.end()) << "Entry '" << path << "' not expected in the tree.";
    ASSERT_EQ(it->second, entry->isDir())
        << "Entry '" << path << "' is not of the right type.";
  }
}

/**
 * @brief Create a mapping from path to file entry for the given tree.
 *
 * @param fileTree The tree to create the mapping from.
 *
 * @return a mapping from path (separated by /) to file entry.
 */
std::map<QString, std::shared_ptr<const FileTreeEntry>>
createMapping(std::shared_ptr<const IFileTree> fileTree)
{
  std::map<QString, std::shared_ptr<const FileTreeEntry>> mapping;
  for (auto entry : *fileTree) {
    mapping[entry->path("/")] = entry;
    if (entry->isDir()) {
      auto tmp = createMapping(entry->astree());
      mapping.insert(std::begin(tmp), std::end(tmp));
    }
  }
  return mapping;
}

TEST(IFileTreeTest, ExtensionComputedCorrectly)
{
  // Fake tree to create entry:
  std::shared_ptr<IFileTree> fileTree = FileListTree::makeTree({});

  auto a = fileTree->addFile("a.txt");
  EXPECT_EQ(a->name(), "a.txt");
  EXPECT_EQ(a->suffix(), "txt");

  fileTree->move(a, "a.c.b");
  EXPECT_EQ(a->name(), "a.c.b");
  EXPECT_EQ(a->suffix(), "b");
}

TEST(IFileTreeTest, TreeIsPopulatedCorrectly)
{
  std::vector<std::pair<QString, bool>> strTree{{"a/", true},       {"b", true},
                                                {"c.x", false},     {"d.y", false},
                                                {"e/q/c.t", false}, {"e/q/p", true}};

  std::shared_ptr<IFileTree> fileTree = FileListTree::makeTree(std::move(strTree));

  ASSERT_NE(fileTree, nullptr);

  ASSERT_TRUE(fileTree->exists("a"));
  ASSERT_TRUE(fileTree->exists("b"));
  ASSERT_TRUE(fileTree->exists("c.x"));
  ASSERT_TRUE(fileTree->exists("d.y"));
  ASSERT_TRUE(fileTree->exists("e"));
  ASSERT_TRUE(fileTree->exists("e/q"));
  ASSERT_TRUE(fileTree->exists("e/q/c.t"));
  ASSERT_TRUE(fileTree->exists("e/q/p"));

  assertTreeEquals(fileTree, {{"a", true},
                              {"b", true},
                              {"c.x", false},
                              {"d.y", false},
                              {"e", true},
                              {"e/q", true},
                              {"e/q/c.t", false},
                              {"e/q/p", true}});

  // Retrieve the entry:
  {
    std::shared_ptr<FileTreeEntry> a = fileTree->find("a"), b = fileTree->find("b"),
                                   cx = fileTree->find("c.x"),
                                   dy = fileTree->find("d.y"), e = fileTree->find("e"),
                                   e_q    = fileTree->find("e/q"),
                                   e_q_ct = fileTree->find("e/q/c.t"),
                                   e_q_p  = fileTree->find("e/q/p");

    EXPECT_NE(a, nullptr);
    EXPECT_TRUE(a->isDir());
    EXPECT_EQ(a->astree(), a);
    EXPECT_EQ(a->name(), "a");
    EXPECT_EQ(a->path("/"), "a");
    EXPECT_NE(b, nullptr);
    EXPECT_TRUE(b->isDir());
    EXPECT_EQ(b->astree(), b);
    EXPECT_EQ(b->name(), "b");
    EXPECT_EQ(b->path("/"), "b");
    EXPECT_NE(cx, nullptr);
    EXPECT_TRUE(cx->isFile());
    EXPECT_EQ(cx->astree(), nullptr);
    EXPECT_EQ(cx->name(), "c.x");
    EXPECT_EQ(cx->path("/"), "c.x");
    EXPECT_NE(dy, nullptr);
    EXPECT_TRUE(dy->isFile());
    EXPECT_EQ(dy->astree(), nullptr);
    EXPECT_EQ(dy->name(), "d.y");
    EXPECT_EQ(dy->path("/"), "d.y");
    EXPECT_NE(e, nullptr);
    EXPECT_TRUE(e->isDir());
    EXPECT_EQ(e->astree(), e);
    EXPECT_EQ(e->name(), "e");
    EXPECT_EQ(e->path("/"), "e");
    EXPECT_NE(e_q, nullptr);
    EXPECT_TRUE(e_q->isDir());
    EXPECT_EQ(e_q->astree(), e_q);
    EXPECT_EQ(e_q->name(), "q");
    EXPECT_EQ(e_q->path("/"), "e/q");
    EXPECT_NE(e_q_ct, nullptr);
    EXPECT_TRUE(e_q_ct->isFile());
    EXPECT_EQ(e_q_ct->astree(), nullptr);
    EXPECT_EQ(e_q_ct->name(), "c.t");
    EXPECT_EQ(e_q_ct->path("/"), "e/q/c.t");
    EXPECT_NE(e_q_p, nullptr);
    EXPECT_TRUE(e_q_p->isDir());
    EXPECT_EQ(e_q_p->astree(), e_q_p);
    EXPECT_EQ(e_q_p->name(), "p");
    EXPECT_EQ(e_q_p->path("/"), "e/q/p");

    // Some relation check:
    EXPECT_EQ(a->parent(), fileTree);
    EXPECT_EQ(b->parent(), fileTree);
    EXPECT_EQ(cx->parent(), fileTree);
    EXPECT_EQ(dy->parent(), fileTree);
    EXPECT_EQ(e->parent(), fileTree);
    EXPECT_EQ(e_q->parent(), e->astree());
    EXPECT_EQ(e_q_ct->parent(), e_q->astree());
    EXPECT_EQ(e_q_p->parent(), e_q->astree());

    // Check that we can reach the children:
    EXPECT_EQ(e->astree()->find("q"), e_q);
    EXPECT_EQ(e->astree()->find("q/c.t"), e_q_ct);
    EXPECT_EQ(e->astree()->find("q/p"), e_q_p);

    // Check the content:
    EXPECT_EQ(a->astree()->size(), std::size_t{0});
    EXPECT_TRUE(a->astree()->empty());
    EXPECT_EQ(a->astree()->begin(), a->astree()->end());
    EXPECT_EQ(b->astree()->size(), std::size_t{0});
    EXPECT_TRUE(b->astree()->empty());
    EXPECT_EQ(b->astree()->begin(), b->astree()->end());
    EXPECT_EQ(cx->astree(), nullptr);
    EXPECT_EQ(dy->astree(), nullptr);
    EXPECT_EQ(e->astree()->size(), std::size_t{1});
    EXPECT_EQ(e->astree()->at(0), e_q);
    EXPECT_EQ(e_q->astree()->size(), std::size_t{2});
    EXPECT_NE(std::find(e_q->astree()->begin(), e_q->astree()->end(), e_q_ct),
              e_q->astree()->end());
    EXPECT_NE(std::find(e_q->astree()->begin(), e_q->astree()->end(), e_q_p),
              e_q->astree()->end());

    EXPECT_EQ(a->pathFrom(fileTree), "a");
    EXPECT_EQ(a->path(), "a");
    EXPECT_EQ(b->pathFrom(fileTree), "b");
    EXPECT_EQ(b->path(), "b");
    EXPECT_EQ(cx->path(), "c.x");
    EXPECT_EQ(dy->path(), "d.y");
    EXPECT_EQ(e->path(), "e");
    EXPECT_EQ(e_q->path(), "e\\q");
    EXPECT_EQ(e_q->pathFrom(e->astree()), "q");
    EXPECT_EQ(e_q_ct->path("/"), "e/q/c.t");
    EXPECT_EQ(e_q_ct->pathFrom(e->astree()), "q\\c.t");
    EXPECT_EQ(e_q_ct->pathFrom(e_q->astree(), "/"), "c.t");
    EXPECT_EQ(e_q_p->path(), "e\\q\\p");
    EXPECT_EQ(e_q_p->path("/"), "e/q/p");
    EXPECT_EQ(e_q_p->pathFrom(e->astree()), "q\\p");
    EXPECT_EQ(e_q_p->pathFrom(e_q->astree()), "p");

    EXPECT_EQ(a->pathFrom(b->astree()), "");
    EXPECT_EQ(b->pathFrom(a->astree()), "");
    EXPECT_EQ(e->pathFrom(e_q->astree()), "");
  }

  {
    std::shared_ptr<FileTreeEntry> a  = fileTree->find("a", FileTreeEntry::DIRECTORY),
                                   b  = fileTree->find("b", FileTreeEntry::DIRECTORY),
                                   cx = fileTree->find("c.x", FileTreeEntry::FILE),
                                   dy = fileTree->find("d.y", FileTreeEntry::FILE),
                                   e  = fileTree->find("e", FileTreeEntry::DIRECTORY),
                                   e_q =
                                       fileTree->find("e/q", FileTreeEntry::DIRECTORY),
                                   e_q_ct =
                                       fileTree->find("e/q/c.t", FileTreeEntry::FILE),
                                   e_q_p = fileTree->find("e/q/p",
                                                          FileTreeEntry::DIRECTORY);

    EXPECT_TRUE((a != nullptr && a->isDir() && a->name() == "a"));
    EXPECT_TRUE((b != nullptr && b->isDir() && b->name() == "b"));
    EXPECT_TRUE((cx != nullptr && cx->isFile() && cx->name() == "c.x"));
    EXPECT_TRUE((dy != nullptr && dy->isFile() && dy->name() == "d.y"));
    EXPECT_TRUE((e != nullptr && e->isDir() && e->name() == "e"));
    EXPECT_TRUE((e_q != nullptr && e_q->isDir() && e_q->name() == "q"));
    EXPECT_TRUE((e_q_ct != nullptr && e_q_ct->isFile() && e_q_ct->name() == "c.t"));
    EXPECT_TRUE((e_q_p != nullptr && e_q_p->isDir() && e_q_p->name() == "p"));

    EXPECT_EQ(fileTree->find("a", FileTreeEntry::FILE), nullptr);
    EXPECT_EQ(fileTree->find("b", FileTreeEntry::FILE), nullptr);
    EXPECT_EQ(fileTree->find("c.x", FileTreeEntry::DIRECTORY), nullptr);
    EXPECT_EQ(fileTree->find("d.y", FileTreeEntry::DIRECTORY), nullptr);
    EXPECT_EQ(fileTree->find("e", FileTreeEntry::FILE), nullptr);
    EXPECT_EQ(fileTree->find("e/q", FileTreeEntry::FILE), nullptr);
    EXPECT_EQ(fileTree->find("e/q/c.t", FileTreeEntry::DIRECTORY), nullptr);
    EXPECT_EQ(fileTree->find("e/q/p", FileTreeEntry::FILE), nullptr);
  }
}

TEST(IFileTreeTest, TreeIsDestructedCorrectly)
{
  std::vector<std::pair<QString, bool>> strTree{{"a/", true},       {"b", true},
                                                {"c.x", false},     {"d.y", false},
                                                {"e/q/c.t", false}, {"e/q/p", true}};

  std::shared_ptr<IFileTree> fileTree = FileListTree::makeTree(std::move(strTree));

  EXPECT_NE(fileTree, nullptr);

  // Retrieve weak ptr for the entry:
  std::weak_ptr<FileTreeEntry> a = fileTree->find("a"), b = fileTree->find("b"),
                               cx = fileTree->find("c.x"), dy = fileTree->find("d.y"),
                               e = fileTree->find("e"), e_q = fileTree->find("e/q"),
                               e_q_ct = fileTree->find("e/q/c.t"),
                               e_q_p  = fileTree->find("e/q/p");

  // And for the trees:
  std::weak_ptr<IFileTree> r_t = fileTree, a_t = a.lock()->astree(),
                           b_t = b.lock()->astree(), e_t = e.lock()->astree(),
                           e_q_t   = e_q.lock()->astree(),
                           e_q_p_t = e_q_p.lock()->astree();

  // Release the base tree:
  fileTree.reset();

  EXPECT_TRUE(a.expired());
  EXPECT_TRUE(b.expired());
  EXPECT_TRUE(cx.expired());
  EXPECT_TRUE(dy.expired());
  EXPECT_TRUE(e.expired());
  EXPECT_TRUE(e_q.expired());
  EXPECT_TRUE(e_q_ct.expired());
  EXPECT_TRUE(e_q_p.expired());

  EXPECT_TRUE(a_t.expired());
  EXPECT_TRUE(b_t.expired());
  EXPECT_TRUE(e_t.expired());
  EXPECT_TRUE(e_q_t.expired());
  EXPECT_TRUE(e_q_p_t.expired());
}

TEST(IFileTreeTest, BasicTreeManipulation)
{
  std::vector<std::pair<QString, bool>> strTree{{"a/", true},       {"b", true},
                                                {"c.x", false},     {"d.y", false},
                                                {"e/q/c.t", false}, {"e/q/p", true}};

  std::shared_ptr<IFileTree> fileTree = FileListTree::makeTree(std::move(strTree));

  EXPECT_NE(fileTree, nullptr);

  // Retrieve the entry:
  std::shared_ptr<FileTreeEntry> a = fileTree->find("a"), b = fileTree->find("b"),
                                 cx = fileTree->find("c.x"), dy = fileTree->find("d.y"),
                                 e = fileTree->find("e"), e_q = fileTree->find("e/q"),
                                 e_q_ct = fileTree->find("e/q/c.t"),
                                 e_q_p  = fileTree->find("e/q/p");

  EXPECT_TRUE(b->moveTo(a->astree()));
  EXPECT_FALSE(fileTree->exists("b"));
  EXPECT_EQ(fileTree->find("a/b"), b);
  EXPECT_TRUE(a->astree()->exists("b"));
  EXPECT_EQ(a->astree()->find("b"), b);
  EXPECT_EQ(a->astree()->size(), std::size_t{1});
  EXPECT_EQ(a->astree()->at(0), b);
}

TEST(IFileTreeTest, IterOperations)
{
  auto tree =
      FileListTree::makeTree({{"a", true}, {"c", true}, {"b", false}, {"d", false}});

  // Order should be a -> c -> b -> d
  std::vector expected{tree->find("a"), tree->find("c"), tree->find("b"),
                       tree->find("d")};
  std::vector entries(std::begin(*tree), std::end(*tree));
  EXPECT_EQ(entries, expected);

  // Order should be reversed:
  expected = std::vector(expected.rbegin(), expected.rend());
  entries  = std::vector(std::rbegin(*tree), std::rend(*tree));
  EXPECT_EQ(entries, expected);

  // We can erasae in the middle:
  for (auto it = tree->begin(); it != tree->end();) {
    if ((*it)->name() == "b") {
      it = tree->erase(*it);
      // Check that the returned iterator is valid (it should be the iterator
      // to d):
      EXPECT_EQ(it, tree->end() - 1);
      EXPECT_EQ(*it, tree->find("d"));
    } else {
      ++it;
    }
  }
  assertTreeEquals(tree, {{"a", true}, {"c", true}, {"d", false}});
}

TEST(IFileTreeTest, AddOperations)
{
  {
    auto fileTree = FileListTree::makeTree(
        {{"a", true}, {"c.x", false}, {"e/q/c.t", false}, {"e/q/p", true}});
    auto map = createMapping(fileTree);

    EXPECT_EQ(fileTree->addFile("a"), nullptr);
    EXPECT_EQ(fileTree->addFile("c.x"), nullptr);
    EXPECT_EQ(fileTree->addFile("e"), nullptr);
    EXPECT_EQ(fileTree->addFile("e/q"), nullptr);
    EXPECT_EQ(fileTree->addFile("e/q/c.t"), nullptr);
    EXPECT_EQ(fileTree->addFile("e/q/p"), nullptr);

    auto a_p = fileTree->addFile("a/p");
    EXPECT_NE(a_p, nullptr);
    EXPECT_EQ(a_p->parent(), map["a"]);

    auto e_q_ct = fileTree->addFile("e/q/c.t", true);
    EXPECT_NE(e_q_ct, nullptr);
    EXPECT_EQ(e_q_ct->parent(), map["e/q"]);
    EXPECT_EQ(map["e/q/c.t"]->parent(), nullptr);
    EXPECT_EQ(map["e/q"]->astree()->size(), std::size_t{2});

    // Directory are replaced with addFile():
    auto e_q = fileTree->addFile("e/q", true);
    EXPECT_NE(e_q, nullptr);
    EXPECT_EQ(e_q->parent(), map["e"]);
    EXPECT_EQ(map["e/q"]->parent(), nullptr);
    EXPECT_EQ(map["e"]->astree()->size(), std::size_t{1});
  }
}

TEST(IFileTreeTest, TreeInsertOperations)
{

  // Test failure:
  {
    auto fileTree = FileListTree::makeTree({{"a/", true},
                                            {"b", true},
                                            {"c.x", false},
                                            {"d.y", false},
                                            {"e/q/c.t", false},
                                            {"e/q/p", true},
                                            {"e/q/z/", true},
                                            {"e/q/z/a.t", false},
                                            {"e/q/z/b", true},
                                            {"f/q/c.t", false},
                                            {"f/q/o", true},
                                            {"f/q/z/b", false},
                                            {"f/q/z/c.t", false}});

    EXPECT_NE(fileTree, nullptr);

    // Retrieve the entry:
    auto map = createMapping(fileTree);
    auto e   = fileTree->findDirectory("e");
    auto f_q = fileTree->findDirectory("f/q");

    auto it = e->insert(f_q, IFileTree::InsertPolicy::FAIL_IF_EXISTS);
    EXPECT_EQ(it, e->end());
    EXPECT_EQ(f_q->parent(), fileTree->find("f"));
  }

  // Test replace:
  {
    auto fileTree = FileListTree::makeTree({{"a/", true},
                                            {"b", true},
                                            {"c.x", false},
                                            {"d.y", false},
                                            {"e/q/c.t", false},
                                            {"e/q/p", true},
                                            {"e/q/z/", true},
                                            {"e/q/z/a.t", false},
                                            {"e/q/z/b", true},
                                            {"f/q/c.t", false},
                                            {"f/q/o", true},
                                            {"f/q/z/b", false},
                                            {"f/q/z/c.t", false}});

    EXPECT_NE(fileTree, nullptr);

    // Retrieve the entry:
    auto map = createMapping(fileTree);
    auto e   = fileTree->findDirectory("e");
    auto f_q = fileTree->findDirectory("f/q");

    auto it = e->insert(f_q, IFileTree::InsertPolicy::REPLACE);
    EXPECT_NE(it, e->end());
    EXPECT_EQ(f_q->parent(), e);
    EXPECT_EQ(map["e/q"]->parent(), nullptr);
    EXPECT_EQ(e->find("q"), map["f/q"]);
    EXPECT_TRUE(fileTree->findDirectory("f")->empty());
    EXPECT_EQ(e->find("q/c.t"), map["f/q/c.t"]);
    EXPECT_EQ(e->find("q/o"), map["f/q/o"]);
    EXPECT_EQ(e->find("q/z"), map["f/q/z"]);
    EXPECT_EQ(e->find("q/z/b"), map["f/q/z/b"]);
    EXPECT_EQ(e->find("q/z/c.t"), map["f/q/z/c.t"]);
  }

  // Test merge:
  {
    auto fileTree = FileListTree::makeTree({{"a/", true},
                                            {"b", true},
                                            {"c.x", false},
                                            {"d.y", false},
                                            {"e/q/c.t", false},
                                            {"e/q/p", true},
                                            {"e/q/z", true},
                                            {"e/q/z/a.t", false},
                                            {"e/q/z/b", true},
                                            {"f/q/c.t", false},
                                            {"f/q/o", true},
                                            {"f/q/z", true},
                                            {"f/q/z/b", false},
                                            {"f/q/z/c.t", false}});

    EXPECT_NE(fileTree, nullptr);

    // Retrieve the entry:
    auto map = createMapping(fileTree);
    auto e   = fileTree->findDirectory("e");
    auto f_q = fileTree->findDirectory("f/q");

    auto it = e->insert(f_q, IFileTree::InsertPolicy::MERGE);
    assertTreeEquals(e, {{"q", true},
                         {"q/o", true},
                         {"q/p", true},
                         {"q/z", true},
                         {"q/c.t", false},
                         {"q/z/a.t", false},
                         {"q/z/c.t", false},
                         {"q/z/b", false}});
    EXPECT_EQ(e->find("q/z/b"), map["f/q/z/b"]);
    EXPECT_EQ(fileTree->findDirectory("f")->size(), std::size_t{0});
    EXPECT_EQ(map["f/q"]->parent(), nullptr);
    EXPECT_EQ(map["f/q/z"]->parent(), nullptr);
  }
}

TEST(IFileTreeTest, TreeMoveAndCopyOperations)
{
  {
    auto tree1 = FileListTree::makeTree(
        {{"a/b/m.y", false}, {"a/b/c", true}, {"b/", true}, {"c", false}});
    auto a = tree1->findDirectory("a");
    EXPECT_FALSE(populated(a));

    tree1->move(tree1->find("a"), "a1");

    // Moving the tree should not have populated it:
    EXPECT_EQ(tree1->find("a"), nullptr);
    EXPECT_EQ(tree1->find("a1"), a);
    EXPECT_FALSE(populated(a));

    tree1->copy(tree1->find("a1"), "a2");

    // Copying the tree should not have populated it:
    EXPECT_FALSE(populated(a));
    EXPECT_FALSE(populated(tree1->findDirectory("a2")));
    EXPECT_EQ(tree1->find("a1"), a);
    EXPECT_NE(tree1->find("a1"), tree1->find("a2"));

    assertTreeEquals(tree1, {
                                {"a1", true},
                                {"a1/b", true},
                                {"a1/b/c", true},
                                {"a1/b/m.y", false},
                                {"a2", true},
                                {"a2/b", true},
                                {"a2/b/c", true},
                                {"a2/b/m.y", false},
                                {"b", true},
                                {"c", false},
                            });

    // Everything should be populated now:
    EXPECT_TRUE(populated(tree1->findDirectory("a1")));
    EXPECT_TRUE(populated(tree1->findDirectory("a2")));

    QString a1("a1/"), a2("a2/");
    for (auto p : {"b", "b/c", "b/m.y"}) {
      EXPECT_NE(tree1->find(a1 + p), tree1->find(a2 + p))
          << "Entry '" << (a1 + p) << "' and '" << (a2 + p) << "' should be different.";
    }
  }
}

TEST(IFileTreeTest, TreeMergeOperations)
{

  {
    auto fileTree = FileListTree::makeTree({{"a/", true},
                                            {"b", true},
                                            {"c.x", false},
                                            {"d.y", false},
                                            {"e/q/c.t", false},
                                            {"e/q/p", true}});

    EXPECT_NE(fileTree, nullptr);

    // Retrieve the entry:
    auto map = createMapping(fileTree);
    auto e   = fileTree->findDirectory("e");
    auto e_q = fileTree->findDirectory("e/q");

    // Merge e in the root:
    IFileTree::OverwritesType overwrites;
    auto noverwrites = fileTree->merge(e, &overwrites);

    EXPECT_EQ(noverwrites, std::size_t{0});
    EXPECT_TRUE(overwrites.empty());
    EXPECT_EQ(e->size(), std::size_t{0});
    assertTreeEquals(fileTree, {{"a", true},
                                {"b", true},
                                {"c.x", false},
                                {"d.y", false},
                                {"e", true},
                                {"q", true},
                                {"q/c.t", false},
                                {"q/p", true}});

    auto p = fileTree->addFile("p");
    EXPECT_NE(p, nullptr);

    // Not: e/q is not q
    overwrites.clear();
    noverwrites = fileTree->merge(e_q, &overwrites);
    EXPECT_EQ(noverwrites, std::size_t{1});
    EXPECT_EQ(overwrites.size(), std::size_t{1});
    EXPECT_EQ(overwrites[p], map["e/q/p"]);
    assertTreeEquals(fileTree, {{"a", true},
                                {"b", true},
                                {"c.x", false},
                                {"d.y", false},
                                {"e", true},
                                {"q", true},
                                {"c.t", false},
                                {"p", true}});
    // Note: the "p" at the root should be the one under q initially.
    EXPECT_EQ(fileTree->find("p"), map["e/q/p"]);
  }

  // Merge failure:
  {
    auto tree1 = FileListTree::makeTree({{"a/", true},
                                         {"b", true},
                                         {"c.x", false},
                                         {"d.y", false},
                                         {"e/q/c.t", false},
                                         {"e/q/p", true}});

    std::size_t noverwrites = tree1->findDirectory("e")->merge(tree1);
    EXPECT_EQ(noverwrites, IFileTree::MERGE_FAILED);

    noverwrites = tree1->findDirectory("e/q")->merge(tree1);
    EXPECT_EQ(noverwrites, IFileTree::MERGE_FAILED);

    noverwrites = tree1->merge(tree1);
    EXPECT_EQ(noverwrites, IFileTree::MERGE_FAILED);
  }

  //
  {
    auto tree1 = FileListTree::makeTree({{"a/b/c/m.y", false},
                                         {"a/b/c/n", true},
                                         {"a/b/x.t", false},
                                         {"a/b/y.t", false},
                                         {"b/", true},
                                         {"c", false}});
    auto map1  = createMapping(tree1);

    auto tree2 = FileListTree::makeTree({{"a/b/c/m.y", false},
                                         {"a/b/c/n", false},  // n is a file here
                                         {"a/b/y.t", false},
                                         {"b/v", false},
                                         {"b/e", true}});
    auto map2  = createMapping(tree2);

    IFileTree::OverwritesType overwrites;
    std::size_t noverwrites = tree1->merge(tree2, &overwrites);

    EXPECT_EQ(noverwrites, std::size_t{3});
    EXPECT_EQ(noverwrites, overwrites.size());
    EXPECT_EQ(overwrites[map1["a/b/c/m.y"]], map2["a/b/c/m.y"]);
    EXPECT_EQ(overwrites[map1["a/b/c/n"]], map2["a/b/c/n"]);
    EXPECT_EQ(overwrites[map1["a/b/y.t"]], map2["a/b/y.t"]);

    assertTreeEquals(tree1, {{"a", true},
                             {"b", true},
                             {"c", false},
                             {"a/b", true},
                             {"a/b/c", true},
                             {"a/b/c/m.y", false},
                             {"a/b/c/n", false},
                             {"a/b/x.t", false},
                             {"a/b/y.t", false},
                             {"b/v", false},
                             {"b/e", true}});

    // Merged directories should be the one from the original tree:
    EXPECT_EQ(tree1->find("a"), map1["a"]);
    EXPECT_EQ(tree1->find("a/b"), map1["a/b"]);
    EXPECT_EQ(tree1->find("a/b/c"), map1["a/b/c"]);
    EXPECT_EQ(tree1->find("b"), map1["b"]);

    // Overriden:
    EXPECT_EQ(tree1->find("a/b/c/m.y"), map2["a/b/c/m.y"]);
    EXPECT_EQ(tree1->find("a/b/c/n"), map2["a/b/c/n"]);
    EXPECT_EQ(tree1->find("a/b/y.t"), map2["a/b/y.t"]);
  }
}

TEST(IFileTreeTest, TreeWalkOperations)
{

  auto fileTree = FileListTree::makeTree({{"a/", true},
                                          {"b", true},
                                          {"b/u", false},
                                          {"b/v", false},
                                          {"c.x", false},
                                          {"d.y", false},
                                          {"e/q/c.t", false},
                                          {"e/q/p", true}});

  auto map = createMapping(fileTree);

  // Note: Testing specific order here, while in reality user should not rely
  // on it (and it is not specified, on purpose). Only guarantee is that a folder
  // is visited before its children.
  {
    // Populate the vector:
    std::vector<std::pair<QString, std::shared_ptr<const FileTreeEntry>>> entries;
    fileTree->walk(
        [&entries](auto path, auto entry) {
          entries.push_back({path, entry});
          return IFileTree::WalkReturn::CONTINUE;
        },
        "/");

    decltype(entries) expected{{"", map["a"]},         {"", map["b"]},
                               {"b/", map["b/u"]},     {"b/", map["b/v"]},
                               {"", map["e"]},         {"e/", map["e/q"]},
                               {"e/q/", map["e/q/p"]}, {"e/q/", map["e/q/c.t"]},
                               {"", map["c.x"]},       {"", map["d.y"]}};
    EXPECT_EQ(entries, expected);

    entries.clear();
    fileTree->walk(
        [&entries](auto path, auto entry) {
          if (entry->name() == "e") {
            return IFileTree::WalkReturn::STOP;
          }
          entries.push_back({path, entry});
          return IFileTree::WalkReturn::CONTINUE;
        },
        "/");

    // Note: This assumes a given order, while in reality it is not specified.
    expected = {
        {"", map["a"]},
        {"", map["b"]},
        {"b/", map["b/u"]},
        {"b/", map["b/v"]},
    };
    EXPECT_EQ(entries, expected);

    entries.clear();
    fileTree->walk(
        [&entries](auto path, auto entry) {
          if (entry->name() == "e") {
            return IFileTree::WalkReturn::SKIP;
          }
          entries.push_back({path, entry});
          return IFileTree::WalkReturn::CONTINUE;
        },
        "/");

    // Note: This assumes a given order, while in reality it is not specified.
    expected = {{"", map["a"]},     {"", map["b"]},   {"b/", map["b/u"]},
                {"b/", map["b/v"]}, {"", map["c.x"]}, {"", map["d.y"]}};
    EXPECT_EQ(entries, expected);
  }

  // same as above but with generator version
  {
    // Populate the vector:
    auto entries = walk(fileTree) | std::ranges::to<std::vector>();
    decltype(entries) expected{map["a"],   map["b"],   map["b/u"],   map["b/v"],
                               map["e"],   map["e/q"], map["e/q/p"], map["e/q/c.t"],
                               map["c.x"], map["d.y"]};
    EXPECT_EQ(entries, expected);

    entries.clear();
    for (const auto entry : walk(fileTree)) {
      if (entry->name() == "e") {
        break;  // Stop on e
      }
      entries.push_back(entry);
    }

    // Note: This assumes a given order, while in reality it is not specified.
    expected = {
        map["a"],
        map["b"],
        map["b/u"],
        map["b/v"],
    };
    EXPECT_EQ(entries, expected);

    // note: third test with SKIP is not possible with generator version
  }
}

TEST(IFileTreeTest, TreeGlobOperations)
{
  using entrySet = std::unordered_set<std::shared_ptr<const FileTreeEntry>>;

  const auto REGEX = GlobPatternType::REGEX;

  {
    auto fileTree = FileListTree::makeTree({{"a/", true},
                                            {"a/g.t", false},
                                            {"b", true},
                                            {"b/u", false},
                                            {"b/v", false},
                                            {"c.x", false},
                                            {"d.y", false},
                                            {"e/q/c.t", false},
                                            {"e/q/m.x", false},
                                            {"e/q/p", true}});

    auto map = createMapping(fileTree);

    entrySet entries, expected;

    entries  = glob(fileTree, "*") | std::ranges::to<std::unordered_set>();
    expected = {map["a"], map["b"], map["c.x"], map["d.y"], map["e"]};
    EXPECT_EQ(entries, expected);

    entries  = glob(fileTree, ".*", REGEX) | std::ranges::to<std::unordered_set>();
    expected = {map["a"], map["b"], map["c.x"], map["d.y"], map["e"]};
    EXPECT_EQ(entries, expected);

    entries  = glob(fileTree, "**") | std::ranges::to<std::unordered_set>();
    expected = {fileTree, map["a"], map["b"], map["e"], map["e/q"], map["e/q/p"]};
    EXPECT_EQ(entries, expected);

    entries  = glob(fileTree, "**", REGEX) | std::ranges::to<std::unordered_set>();
    expected = {fileTree, map["a"], map["b"], map["e"], map["e/q"], map["e/q/p"]};
    EXPECT_EQ(entries, expected);

    entries  = glob(fileTree, "*.x") | std::ranges::to<std::unordered_set>();
    expected = {map["c.x"]};
    EXPECT_EQ(entries, expected);

    entries  = glob(fileTree, ".*[.]x", REGEX) | std::ranges::to<std::unordered_set>();
    expected = {map["c.x"]};
    EXPECT_EQ(entries, expected);

    entries  = glob(fileTree, "**/*.x") | std::ranges::to<std::unordered_set>();
    expected = {map["c.x"], map["e/q/m.x"]};
    EXPECT_EQ(entries, expected);

    entries =
        glob(fileTree, "**/.*[.]x", REGEX) | std::ranges::to<std::unordered_set>();
    expected = {map["c.x"], map["e/q/m.x"]};
    EXPECT_EQ(entries, expected);

    entries  = glob(fileTree, "*.t") | std::ranges::to<std::unordered_set>();
    expected = {};
    EXPECT_EQ(entries, expected);

    entries  = glob(fileTree, "**/*.t") | std::ranges::to<std::unordered_set>();
    expected = {map["a/g.t"], map["e/q/c.t"]};
    EXPECT_EQ(entries, expected);

    entries  = glob(fileTree, "a/*") | std::ranges::to<std::unordered_set>();
    expected = {map["a/g.t"]};
    EXPECT_EQ(entries, expected);

    entries  = glob(fileTree, "a/.*", REGEX) | std::ranges::to<std::unordered_set>();
    expected = {map["a/g.t"]};
    EXPECT_EQ(entries, expected);

    entries  = glob(fileTree, "**/*.[xt]") | std::ranges::to<std::unordered_set>();
    expected = {map["c.x"], map["e/q/m.x"], map["a/g.t"], map["e/q/c.t"]};
    EXPECT_EQ(entries, expected);

    entries =
        glob(fileTree, "**/.*[.][xt]", REGEX) | std::ranges::to<std::unordered_set>();
    expected = {map["c.x"], map["e/q/m.x"], map["a/g.t"], map["e/q/c.t"]};
    EXPECT_EQ(entries, expected);
  }

  {
    auto fileTree = FileListTree::makeTree({{"aq.js", false},
                                            {"bb", true},
                                            {"cm.tx", false},
                                            {"dp.js", false},
                                            {"ev", false},
                                            {"go.ya", false},
                                            {"gw.md", false},
                                            {"hh", false},
                                            {"hl", true},
                                            {"in", true},
                                            {"mz", true},
                                            {"sc", true},
                                            {"bb/ce.cp", false},
                                            {"bb/cm.tx", false},
                                            {"bb/gw", true},
                                            {"bb/iw.cp", false},
                                            {"bb/js", true},
                                            {"bb/px.cp", false},
                                            {"hl/ds.in", false},
                                            {"in/nu", true},
                                            {"mz/tu.js", false},
                                            {"sc/cm.tx", false},
                                            {"sc/cw.ts", false},
                                            {"sc/cz.rc", false},
                                            {"sc/dr.cp", false},
                                            {"sc/hh.cp", false},
                                            {"sc/kn.ui", false},
                                            {"sc/lr.cp", false},
                                            {"sc/nd.o", false},
                                            {"sc/nv.o", false},
                                            {"sc/rv.ui", false},
                                            {"sc/tv.h", false},
                                            {"bb/gw/cp.qm", false},
                                            {"bb/gw/hq.qm", false},
                                            {"bb/gw/pu.ts", false},
                                            {"bb/gw/tu.ts", false},
                                            {"bb/js/cm.tx", false},
                                            {"bb/js/co.cp", false},
                                            {"in/nu/el.h", false},
                                            {"in/nu/fj.h", false},
                                            {"in/nu/lw", true},
                                            {"in/nu/xx", true},
                                            {"in/nu/lw/cp.h", false},
                                            {"in/nu/lw/go.h", false},
                                            {"in/nu/xx/ap.h", false},
                                            {"in/nu/xx/qz.h", false}});

    auto map = createMapping(fileTree);

    entrySet entries, expected;

    entries  = glob(fileTree, "*.h") | std::ranges::to<std::unordered_set>();
    expected = {};
    EXPECT_EQ(entries, expected);

    entries  = glob(fileTree, "*") | std::ranges::to<std::unordered_set>();
    expected = {map.at("aq.js"), map.at("bb"),    map.at("cm.tx"), map.at("dp.js"),
                map.at("ev"),    map.at("go.ya"), map.at("gw.md"), map.at("hh"),
                map.at("hl"),    map.at("in"),    map.at("mz"),    map.at("sc")};
    EXPECT_EQ(entries, expected);

    entries  = glob(fileTree, "*/*") | std::ranges::to<std::unordered_set>();
    expected = {
        map.at("bb/ce.cp"), map.at("bb/cm.tx"), map.at("bb/gw"),    map.at("bb/iw.cp"),
        map.at("bb/js"),    map.at("bb/px.cp"), map.at("hl/ds.in"), map.at("in/nu"),
        map.at("mz/tu.js"), map.at("sc/cm.tx"), map.at("sc/cw.ts"), map.at("sc/cz.rc"),
        map.at("sc/dr.cp"), map.at("sc/hh.cp"), map.at("sc/kn.ui"), map.at("sc/lr.cp"),
        map.at("sc/nd.o"),  map.at("sc/nv.o"),  map.at("sc/rv.ui"), map.at("sc/tv.h")};
    EXPECT_EQ(entries, expected);

    entries  = glob(fileTree, "*/*/*") | std::ranges::to<std::unordered_set>();
    expected = {map.at("bb/gw/cp.qm"), map.at("bb/gw/hq.qm"), map.at("bb/gw/pu.ts"),
                map.at("bb/gw/tu.ts"), map.at("bb/js/cm.tx"), map.at("bb/js/co.cp"),
                map.at("in/nu/el.h"),  map.at("in/nu/fj.h"),  map.at("in/nu/lw"),
                map.at("in/nu/xx")};
    EXPECT_EQ(entries, expected);

    entries  = glob(fileTree, "**") | std::ranges::to<std::unordered_set>();
    expected = {fileTree,           map.at("bb"), map.at("bb/gw"), map.at("bb/js"),
                map.at("hl"),       map.at("in"), map.at("in/nu"), map.at("in/nu/lw"),
                map.at("in/nu/xx"), map.at("mz"), map.at("sc")};
    EXPECT_EQ(entries, expected);

    entries  = glob(fileTree, "**/*") | std::ranges::to<std::unordered_set>();
    expected = {map.at("aq.js"),
                map.at("bb"),
                map.at("cm.tx"),
                map.at("dp.js"),
                map.at("ev"),
                map.at("go.ya"),
                map.at("gw.md"),
                map.at("hh"),
                map.at("hl"),
                map.at("in"),
                map.at("mz"),
                map.at("sc"),
                map.at("bb/ce.cp"),
                map.at("bb/cm.tx"),
                map.at("bb/gw"),
                map.at("bb/iw.cp"),
                map.at("bb/js"),
                map.at("bb/px.cp"),
                map.at("bb/gw/cp.qm"),
                map.at("bb/gw/hq.qm"),
                map.at("bb/gw/pu.ts"),
                map.at("bb/gw/tu.ts"),
                map.at("bb/js/cm.tx"),
                map.at("bb/js/co.cp"),
                map.at("hl/ds.in"),
                map.at("in/nu"),
                map.at("in/nu/el.h"),
                map.at("in/nu/fj.h"),
                map.at("in/nu/lw"),
                map.at("in/nu/xx"),
                map.at("in/nu/lw/cp.h"),
                map.at("in/nu/lw/go.h"),
                map.at("in/nu/xx/ap.h"),
                map.at("in/nu/xx/qz.h"),
                map.at("mz/tu.js"),
                map.at("sc/cm.tx"),
                map.at("sc/cw.ts"),
                map.at("sc/cz.rc"),
                map.at("sc/dr.cp"),
                map.at("sc/hh.cp"),
                map.at("sc/kn.ui"),
                map.at("sc/lr.cp"),
                map.at("sc/nd.o"),
                map.at("sc/nv.o"),
                map.at("sc/rv.ui"),
                map.at("sc/tv.h")};
    EXPECT_EQ(entries, expected);

    entries  = glob(fileTree, "**/cm.tx") | std::ranges::to<std::unordered_set>();
    expected = {map.at("cm.tx"), map.at("bb/cm.tx"), map.at("bb/js/cm.tx"),
                map.at("sc/cm.tx")};
    EXPECT_EQ(entries, expected);

    entries  = glob(fileTree, "**/sc/**/cm.tx") | std::ranges::to<std::unordered_set>();
    expected = {map.at("sc/cm.tx")};
    EXPECT_EQ(entries, expected);

    entries  = glob(fileTree, "**/sc") | std::ranges::to<std::unordered_set>();
    expected = {map.at("sc")};
    EXPECT_EQ(entries, expected);

    entries  = glob(fileTree, "in/**") | std::ranges::to<std::unordered_set>();
    expected = {map.at("in"), map.at("in/nu"), map.at("in/nu/lw"), map.at("in/nu/xx")};
    EXPECT_EQ(entries, expected);

    entries  = glob(fileTree, "in/**/**") | std::ranges::to<std::unordered_set>();
    expected = {map.at("in"), map.at("in/nu"), map.at("in/nu/lw"), map.at("in/nu/xx")};
    EXPECT_EQ(entries, expected);

    entries  = glob(fileTree, "in/*/*") | std::ranges::to<std::unordered_set>();
    expected = {map.at("in/nu/el.h"), map.at("in/nu/fj.h"), map.at("in/nu/lw"),
                map.at("in/nu/xx")};
    EXPECT_EQ(entries, expected);

    entries  = glob(fileTree, "in/*/*.h") | std::ranges::to<std::unordered_set>();
    expected = {map.at("in/nu/el.h"), map.at("in/nu/fj.h")};
    EXPECT_EQ(entries, expected);

    entries  = glob(fileTree, "sc/**/*.cp") | std::ranges::to<std::unordered_set>();
    expected = {map.at("sc/dr.cp"), map.at("sc/hh.cp"), map.at("sc/lr.cp")};
    EXPECT_EQ(entries, expected);

    entries  = glob(fileTree, "sc/**/n*.o") | std::ranges::to<std::unordered_set>();
    expected = {map.at("sc/nd.o"), map.at("sc/nv.o")};
    EXPECT_EQ(entries, expected);
  }
}
