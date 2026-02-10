/*
Userspace Virtual Filesystem

Copyright (C) 2015 Sebastian Herbord. All rights reserved.

This file is part of usvfs.

usvfs is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

usvfs is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with usvfs. If not, see <http://www.gnu.org/licenses/>.
*/
#pragma once

#include "exceptionex.h"
#include "logging.h"
#include "shared_memory.h"
#include "stringutils.h"
#include "wildcard.h"

// simplify unit tests by allowing access to private members
#ifndef PRIVATE
#define PRIVATE private
#endif  // PRIVATE

namespace usvfs::shared
{

template <typename T, typename U>
struct SHMDataCreator
{
  static T create(const U& source, const VoidAllocatorT& allocator)
  {
    return T(source, allocator);
  }
};

template <typename T, typename U>
T createData(const U& source, const VoidAllocatorT& allocator)
{
  return SHMDataCreator<T, U>::create(source, allocator);
}

template <typename T>
T createDataEmpty(const VoidAllocatorT& allocator);

template <typename T>
void dataAssign(T& destination, const T& source);

// crappy little workaround for fs::path iterating over path separators
fs::path::iterator nextIter(const fs::path::iterator& iter,
                            const fs::path::iterator& end);

void advanceIter(fs::path::iterator& iter, const fs::path::iterator& end);

// decomposes a path into its components
//
class DecomposablePath
{
public:
  explicit DecomposablePath(std::string s) : m_s(std::move(s)), m_begin(0), m_end(0)
  {
    m_end = nextSeparator(m_begin);
  }

  // move to the next component, returns false when there are no more components
  //
  bool next()
  {
    for (;;) {
      if (m_end >= m_s.size()) {
        // done
        m_begin = m_end;
        return false;
      }

      // move begin to one past the last separator found
      m_begin = m_end + 1;

      // find the next separator
      m_end = nextSeparator(m_begin);

      // check for components that should be ignored:
      //  - empty, happens when the path ends with a separator
      //  - slashes, happens with consecutive separators
      //  - dot, unnecessary
      const auto c = current();
      if (!c.empty() && c != "\\" && c != "/" && c != ".") {
        return true;
      }
    }
  }

  // checks if next() would return false
  //
  bool peekNext() const
  {
    auto copy = *this;
    return copy.next();
  }

  // the current component, empty when next() returned false
  //
  std::string_view current() const { return {m_s.data() + m_begin, m_end - m_begin}; }

private:
  const std::string m_s;
  std::size_t m_begin, m_end;

  // finds the next path separator
  //
  std::size_t nextSeparator(std::size_t from) const
  {
    while (from < m_s.size()) {
      if (m_s[from] == '/' || m_s[from] == '\\') {
        break;
      }

      ++from;
    }

    return from;
  }
};

namespace bi  = boost::interprocess;
namespace bmi = boost::multi_index;

typedef uint8_t TreeFlags;

static const TreeFlags FLAG_DIRECTORY     = 0x01;
static const TreeFlags FLAG_DUMMY         = 0x02;
static const TreeFlags FLAG_FIRSTUSERFLAG = 0x10;

struct MissingThrowT
{};
static const MissingThrowT MissingThrow = MissingThrowT();

template <typename NodeDataT>
class TreeContainer;

template <typename T1, typename T2, typename Alloc>
struct mutable_pair
{
  typedef T1 first_type;
  typedef T2 second_type;

  mutable_pair(Alloc alloc) : first(T1(alloc)), second(T2(alloc)) {}

  mutable_pair(const T1& f, const T2& s) : first(f), second(s) {}

  mutable_pair(const std::pair<T1, T2>& p) : first(p.first), second(p.second) {}

  T1 first;
  mutable T2 second;
};

template <typename Key, typename T, typename Compare, typename Allocator,
          typename Element = mutable_pair<Key, T, Allocator>>
using mimap = bmi::multi_index_container<
    Element,
    bmi::indexed_by<
        bmi::ordered_unique<bmi::member<Element, Key, &Element::first>, Compare>>,
    typename Allocator::template rebind<Element>::other>;

/**
 * a representation of a directory tree in memory.
 * This class is designed to be stored in shared memory.
 */
template <typename NodeDataT>
class DirectoryTree
{
  template <typename T>
  friend class TreeContainer;

public:
  struct CILess
  {
    template <typename U, typename V>
    bool operator()(const U& lhs, const V& rhs) const
    {
      const size_t lhsLength = getLength(lhs);
      const size_t rhsLength = getLength(rhs);

      const auto r =
          _strnicmp(getCharPtr(lhs), getCharPtr(rhs), std::min(lhsLength, rhsLength));

      if (r == 0) {
        return lhsLength < rhsLength;
      }

      return (r < 0);
    }

  private:
    const char* getCharPtr(const StringT& s) const { return s.c_str(); }

    const char* getCharPtr(const std::string& s) const { return s.c_str(); }

    const char* getCharPtr(const char* s) const { return s; }

    const char* getCharPtr(std::string_view s) const { return s.data(); }

    size_t getLength(const StringT& s) const { return s.size(); }

    size_t getLength(const std::string& s) const { return s.size(); }

    size_t getLength(const char* s) const { return strlen(s); }

    size_t getLength(std::string_view s) const { return s.size(); }
  };

  typedef DirectoryTree<NodeDataT> NodeT;
  typedef bi::deleter<NodeT, SegmentManagerT> DeleterT;
  typedef NodeDataT DataT;

  typedef bi::shared_ptr<NodeT, VoidAllocatorT, DeleterT> NodePtrT;
  typedef bi::weak_ptr<NodeT, VoidAllocatorT, DeleterT> WeakPtrT;

  typedef bi::allocator<std::pair<const StringT, NodePtrT>, SegmentManagerT>
      NodeEntryAllocatorT;

  typedef mimap<StringT, NodePtrT, CILess, NodeEntryAllocatorT> NodeMapT;
  typedef typename NodeMapT::iterator file_iterator;
  typedef typename NodeMapT::const_iterator const_file_iterator;

  typedef std::function<void(const NodePtrT&)> VisitorFunction;

  DirectoryTree()                       = delete;
  DirectoryTree(const NodeT& reference) = delete;
  DirectoryTree(NodeT&& reference)      = delete;
  NodeT& operator=(NodeT reference)     = delete;

  /**
   * @brief construct a new node to be inserted in an existing tree
   **/
  DirectoryTree(std::string_view name, TreeFlags flags, const NodePtrT& parent,
                const NodeDataT& data, const VoidAllocatorT& allocator)
      : m_Parent(parent), m_Name(name.begin(), name.end(), allocator), m_Data(data),
        m_Nodes(allocator), m_Flags(flags)
  {}

  ~DirectoryTree() { m_Nodes.clear(); }

  /**
   * @return parent node
   */
  NodePtrT parent() const { return m_Parent.lock(); }

  /**
   * @return the full path to the node
   */
  fs::path path() const
  {
    if (m_Parent.lock().get() == nullptr) {
      if (m_Name.size() == 0) {
        return fs::path();
      } else {
        return fs::path(m_Name.c_str()) / "\\";
      }
    } else {
      return m_Parent.lock()->path() / m_Name.c_str();
    }
  }

  /**
   * @return data connected to this node
   **/
  const NodeDataT& data() const { return m_Data; }

  /**
   * @return name of this node
   */
  std::string name() const { return m_Name.c_str(); }

  /**
   * @brief setFlag change a flag for this node
   * @param enabled new state for the specified flag
   */
  void setFlag(TreeFlags flag, bool enabled = true)
  {
    m_Flags = enabled ? m_Flags | flag : m_Flags & ~flag;
  }

  /**
   * @return true if the specified flag is set, false otherwise
   */
  bool hasFlag(TreeFlags flag) const { return (m_Flags & flag) != 0; }

  /**
   * @return true if this node is a directory, false if it's a regular file
   */
  bool isDirectory() const { return hasFlag(FLAG_DIRECTORY); }

  /**
   * @return the number of subnodes (directly) below this one
   */
  size_t numNodes() const { return m_Nodes.size(); }

  /**
   * @return number of nodes in this (sub-)tree including this one
   */
  size_t numNodesRecursive() const
  {
    size_t result = numNodes() + 1;

    for (const auto& node : m_Nodes) {
      result += node.second->numNodesRecursive();
    }

    return result;
  }

  /**
   * @brief find a node by its path
   * @param path the path to look up
   * @return a pointer to the node or a null ptr
   */
  NodePtrT findNode(const fs::path& path)
  {
    fs::path::iterator iter = path.begin();
    return findNode(path, iter);
  }

  /**
   * @brief find a node by its path
   * @param path the path to look up
   * @return a pointer to the node or a null ptr
   */
  const NodePtrT findNode(const fs::path& path) const
  {
    fs::path::iterator iter = path.begin();
    return findNode(path, iter);
  }

  /**
   * @brief visit the nodes along the specified path (in order) calling the visitor for
   * each
   * @param path the path to visit
   * @param visitor a function called for each node
   */
  void visitPath(const fs::path& path, const VisitorFunction& visitor) const
  {
    fs::path::iterator iter = path.begin();
    visitPath(path, iter, visitor);
  }

  /**
   * @brief retrieve a node by the specified name
   * @param name name of the node
   * @return the node found or an empty pointer if no such node was found
   */
  NodePtrT node(std::string_view name, MissingThrowT) const
  {
    auto iter = m_Nodes.find(name);

    if (iter != m_Nodes.end()) {
      return iter->second;
    } else {
      USVFS_THROW_EXCEPTION(node_missing_error());
    }
  }

  /**
   * @brief retrieve a node by the specified name
   * @param name name of the node
   * @return the node found or an empty pointer if no such node was found
   */
  NodePtrT node(std::string_view name)
  {
    auto iter = m_Nodes.find(name);

    if (iter != m_Nodes.end()) {
      return iter->second;
    } else {
      return NodePtrT();
    }
  }

  /**
   * @brief retrieve a node by the specified name
   * @param name name of the node
   * @return the node found or an empty pointer if no such node was found
   */
  const NodePtrT node(std::string_view name, MissingThrowT)
  {
    auto iter = m_Nodes.find(name);

    if (iter != m_Nodes.end()) {
      return iter->second;
    } else {
      USVFS_THROW_EXCEPTION(node_missing_error());
    }
  }

  /**
   * @brief retrieve a node by the specified name
   * @param name name of the node
   * @return the node found or an empty pointer if no such node was found
   */
  const NodePtrT node(std::string_view name) const
  {
    auto iter = m_Nodes.find(name);

    if (iter != m_Nodes.end()) {
      return iter->second;
    } else {
      return NodePtrT();
    }
  }

  /**
   * @brief test if a node by the specified name exists
   * @param name name of the node
   * @return true if the node exists, false otherwise
   */
  bool exists(std::string_view name) const
  {
    return m_Nodes.find(name) != m_Nodes.end();
  }

  /**
   * @brief find all matches for a pattern
   * @param pattern the pattern to look for
   * @return a vector of the found nodes
   */
  std::vector<NodePtrT> find(const std::string& pattern) const
  {
    // determine if there is a prefix in the pattern that indicates a specific
    // directory.
    size_t fixedPart = pattern.find_first_of("*?");

    if (fixedPart == 0)
      fixedPart = std::string::npos;
    if (fixedPart != std::string::npos)
      fixedPart = pattern.find_last_of(R"(\/)", fixedPart);

    std::vector<NodePtrT> result;

    if (fixedPart != std::string::npos) {
      // if there is a prefix, search for the node representing that path and
      // search only on that
      NodePtrT node = findNode(fs::path(pattern.substr(0, fixedPart)));
      if (node.get() != nullptr) {
        node->findLocal(result, pattern.substr(fixedPart + 1));
      }
    } else {
      findLocal(result, pattern);
    }

    return result;
  }

  /**
   * @return an iterator to the first leaf
   **/
  file_iterator filesBegin() { return m_Nodes.begin(); }

  /**
   * @return a const iterator to the first leaf
   **/
  const_file_iterator filesBegin() const { return m_Nodes.begin(); }

  /**
   * @return an iterator one past the last leaf
   **/
  file_iterator filesEnd() { return m_Nodes.end(); }

  /**
   * @return a const iterator one past the last leaf
   **/
  const_file_iterator filesEnd() const { return m_Nodes.end(); }

  /**
   * @brief erase the leaf at the specified iterator
   * @return an iterator to the following file
   **/
  file_iterator erase(file_iterator iter) { return m_Nodes.erase(iter); }

  /**
   * @brief clear all nodes
   */
  void clear() { m_Nodes.clear(); }

  void removeFromTree()
  {
    if (auto par = parent()) {
      spdlog::get("usvfs")->info("remove from tree {}", m_Name.c_str());
      auto self = par->m_Nodes.find(m_Name.c_str());
      if (self != par->m_Nodes.end()) {
        par->erase(self);
      } else {
        // trying to remove a node that does not exist, most likely because it was
        // already removed in a lower level call. this is known to happen when MoveFile
        // has the MOVEFILE_COPY_ALLOWED flag and moving a mapped file.
        spdlog::get("usvfs")->warn("Failed to remove inexisting node from tree: {}",
                                   m_Name.c_str());
      }
    }
  }

  PRIVATE : void set(StringT key, const NodePtrT& value)
  {
    auto res = m_Nodes.emplace(std::move(key), value);
    if (!res.second) {
      res.first->second = value;
    }
  }

  WeakPtrT findRoot() const
  {
    if (m_Parent.lock().get() == nullptr) {
      return m_Self;
    } else {
      return m_Parent.lock()->findRoot();
    }
  }

  NodePtrT findNode(const fs::path& name, fs::path::iterator& iter)
  {
    std::string l = iter->string();
    auto subNode  = m_Nodes.find(iter->string());
    advanceIter(iter, name.end());

    if (iter == name.end()) {
      // last name component, should be a local node
      if (subNode != m_Nodes.end()) {
        return subNode->second;
      } else {
        return NodePtrT();
      }
    } else {
      if (subNode != m_Nodes.end()) {
        return subNode->second->findNode(name, iter);
      } else {
        return NodePtrT();
      }
    }
  }

  const NodePtrT findNode(const fs::path& name, fs::path::iterator& iter) const
  {
    auto subNode = m_Nodes.find(iter->string());
    advanceIter(iter, name.end());

    if (iter == name.end()) {
      // last name component, should be a local node
      if (subNode != m_Nodes.end()) {
        return subNode->second;
      } else {
        return NodePtrT();
      }
    } else {
      if (subNode != m_Nodes.end()) {
        return subNode->second->findNode(name, iter);
      } else {
        return NodePtrT();
      }
    }
  }

  void visitPath(const fs::path& path, fs::path::iterator& iter,
                 const VisitorFunction& visitor) const
  {
    auto subNode = m_Nodes.find(iter->string());

    if (subNode != m_Nodes.end()) {
      visitor(subNode->second);
      advanceIter(iter, path.end());
      if (iter != path.end()) {
        subNode->second->visitPath(path, iter, visitor);
      }
    }
  }

  void findLocal(std::vector<NodePtrT>& output, const std::string& pattern) const
  {
    for (auto iter = m_Nodes.begin(); iter != m_Nodes.end(); ++iter) {
      LPCSTR remainder = nullptr;

      if (pattern.size() > 1 && (pattern[0] == '*') &&
          ((pattern[1] == '/') || (pattern[1] == '\\')) &&
          iter->second->isDirectory()) {
        // the star may represent a directory (one directory level, not
        // multiple!), search in subdirectory
        iter->second->findLocal(output, pattern.substr(1));
      } else if ((remainder = wildcard::PartialMatch(iter->second->name().c_str(),
                                                     pattern.c_str())) != nullptr) {
        if ((*remainder == '\0') || (strcmp(remainder, "*") == 0)) {
          NodePtrT node = iter->second;
          output.push_back(node);
        }

        if (iter->second->isDirectory()) {
          iter->second->findLocal(output, remainder);
        }
      }
    }
  }

  PRIVATE : TreeFlags m_Flags;

  WeakPtrT m_Parent;
  WeakPtrT m_Self;

  StringT m_Name;
  NodeDataT m_Data;

  NodeMapT m_Nodes;
};

template <typename NodeDataT>
void dumpTree(std::ostream& stream, const DirectoryTree<NodeDataT>& tree, int level = 0)
{
  stream << std::string(level, ' ') << tree.name() << " -> " << tree.data() << "\n";
  for (auto iter = tree.filesBegin(); iter != tree.filesEnd(); ++iter) {
    dumpTree<NodeDataT>(stream, *iter->second, level + 1);
  }
}

}  // namespace usvfs::shared
