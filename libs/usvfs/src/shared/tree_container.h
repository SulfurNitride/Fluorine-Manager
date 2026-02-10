#pragma once

#include "directory_tree.h"
#include "shared_memory.h"

namespace usvfs::shared
{

// smart pointer to DirectoryTrees (only intended for top-level nodes). This
// will transparently switch to new shared memory regions in case they get
// reallocated
//
//
// reassign() is called from a variety of places when the current chunk of
// shared memory is full or is marked as being outdated; its job is to either
// find another chunk that may have been created by another process or to create
// a brand new one
//
//
// if there is only a single process hooked, it will slowly fill up the shared
// memory when adding files, eventually throw a bi::bad_alloc and end up in
// reassign(); a new block will be allocated, the data copied over, and the old
// block will be deallocated
//
//
// when multiple processes are involved, things are more complicated
//
// two processes A and B will start by using the same shared memory, but they
// have their own pointer to it that's local to the process (the `m_TreeMeta`
// member variable)
//
// so when process A fills up the shared memory and reallocates it, process B
// is still pointing to the old shared memory; only when process B does some
// operation that accesses the file tree will the pointer be checked and
// adjusted to point to the new shared memory
//
// in this example, when process A ran out of memory, it set `outdated` to
// `true` in the shared memory block, allocated a new one and copied the data
// over, but it did not deallocate the block because process B is still
// pointing to it
//
// when process B tries to access the block, it checks `outdated` (see get());
// if it's true, it means that it's pointing to an outdated shared memory block
// and must find the new one that process A created
//
// the new block is not necessarily the very next name, it is possible for
// process A to burn through a series of blocks quickly when adding a bunch of
// new files, and these blocks will all have been deallocated by the time
// process B tries to find the newest one
//
// so when a process sees its block as outdated, it will try to open a bunch of
// names until one exists that is not outdated; if it can't find the block
// (shouldn't happen), it will just create a new one
//
template <typename TreeT>
class TreeContainer
{
public:
  /**
   * @brief Constructor
   * @param SHMName name of the shared memory holding the tree. This should contain the
   * running number
   * @param size initial size in bytes of the container. since the tree is resized by
   * doubling this should be a power of two. 64k is supposed to be the page size on
   * windows so smaller allocations make little sense
   * @note size can't be too small. If initial allocations fail automatic growing won't
   * work
   */
  TreeContainer(const std::string& SHMName, size_t size = 64 * 1024)
      : m_TreeMeta(nullptr), m_SHMName(SHMName)
  {
    std::locale global_loc = std::locale();
    std::locale loc(global_loc, new fs::detail::utf8_codecvt_facet);
    fs::path::imbue(loc);

    // append _1 to the name if it doesn't end with _N already
    std::regex pattern(R"exp((.*_)(\d+))exp");
    std::smatch match;
    std::string shmName(m_SHMName.c_str());
    regex_match(shmName, match, pattern);

    if (match.size() != 3) {
      m_SHMName += "_1";
    }

    // creates a new memory block if this is the first process to run or attach
    // to an already existing one
    createOrOpen(m_SHMName, size);

    spdlog::get("usvfs")->info("attached to {0} with {1} nodes, size {2}", m_SHMName,
                               m_TreeMeta->tree->numNodesRecursive(),
                               byte_string(m_SHM->get_size()));
  }

  TreeContainer(const TreeContainer&)            = delete;
  TreeContainer& operator=(const TreeContainer&) = delete;

  ~TreeContainer()
  {
    if (unassign(m_SHM, m_TreeMeta)) {
      bi::shared_memory_object::remove(m_SHMName.c_str());
    }
  }

  /**
   * @return retrieve an allocator that can be used to create objects in this tree
   */
  VoidAllocatorT allocator() { return VoidAllocatorT(m_SHM->get_segment_manager()); }

  template <typename... Arguments>
  typename TreeT::DataT create(Arguments&&... args)
  {
    return TreeT::DataT(std::forward<Arguments>(args)..., allocator());
  }

  TreeT* operator->() { return get(); }

  /**
   * @return raw pointer to the managed tree
   */
  TreeT* get()
  {
    if (m_TreeMeta->outdated) {
      reassign();
    }

    return m_TreeMeta->tree.get();
  }

  /**
   * @return raw const pointer to the managed tree
   */
  const TreeT* get() const
  {
    if (m_TreeMeta->outdated) {
      // safe const_cast, TreeContainer are never created const
      const_cast<TreeContainer<TreeT>*>(this)->reassign();
    }

    return m_TreeMeta->tree.get();
  }

  const TreeT* operator->() const { return get(); }

  /**
   * @return current name of the managed shared memory
   */
  std::string shmName() const { return m_SHMName; }

  void clear() { m_TreeMeta->tree->clear(); }

  /**
   * @brief add a new file to the tree
   *
   * @param name name of the file, expected to be relative to this directory
   * @param data the file data to attach
   * @param flags flags for this files
   * @param overwrite if true, the new leaf will overwrite an existing one that compares
   *as "equal"
   * @return pointer to the new node or a null ptr
   **/
  template <typename T>
  typename TreeT::NodePtrT addFile(const fs::path& name, const T& data,
                                   TreeFlags flags = 0, bool overwrite = true)
  {
    for (;;) {
      DecomposablePath dp(name.string());

      try {
        return addNode(m_TreeMeta->tree.get(), dp, data, overwrite, flags, allocator());
      } catch (const bi::bad_alloc&) {
      }

      reassign();
    }
  }

  /**
   * @brief add a new directory to the tree
   *
   * @param name name of the file, expected to be relative to this directory
   * @param data the file data to attach
   * @param flags flags for this files
   * @param overwrite if true, the new leaf will overwrite an existing one that compares
   *as "equal"
   * @return pointer to the new node or a null ptr
   **/
  template <typename T>
  typename TreeT::NodePtrT addDirectory(const fs::path& name, const T& data,
                                        TreeFlags flags = 0, bool overwrite = true)
  {
    for (;;) {
      DecomposablePath dp(name.string());

      try {
        return addNode(m_TreeMeta->tree.get(), dp, data, overwrite,
                       flags | FLAG_DIRECTORY, allocator());
      } catch (const bi::bad_alloc&) {
      }

      reassign();
    }
  }

  void getBuffer(void*& buffer, size_t& bufferSize) const
  {
    buffer     = m_SHM->get_address();
    bufferSize = m_SHM->get_size();
  }

private:
  struct TreeMeta
  {
    TreeMeta(const typename TreeT::DataT& data, SegmentManagerT* segmentManager)
        : tree(segmentManager->construct<TreeT>(bi::anonymous_instance)(
              "", true, TreeT::NodePtrT(), data, VoidAllocatorT(segmentManager))),
          referenceCount(0),  // reference count only set on top level node
          outdated(false)
    {}

    OffsetPtrT<TreeT> tree;
    long referenceCount;
    bool outdated;
    bi::interprocess_mutex mutex;
  };

  std::string m_SHMName;
  std::shared_ptr<SharedMemoryT> m_SHM;
  TreeMeta* m_TreeMeta;

  typename TreeT::DataT createEmpty()
  {
    return createDataEmpty<typename TreeT::DataT>(allocator());
  }

  template <typename T>
  TreeT* createSubNode(const VoidAllocatorT& allocator, std::string_view name,
                       unsigned long flags, const T& data)
  {
    auto* manager = allocator.get_segment_manager();

    return manager->construct<TreeT>(bi::anonymous_instance)(
        name, flags, TreeT::NodePtrT(),
        createData<typename TreeT::DataT, T>(data, allocator), manager);
  }

  typename TreeT::NodePtrT createSubPtr(TreeT* subNode)
  {
    SharedMemoryT::segment_manager* manager = m_SHM->get_segment_manager();
    return TreeT::NodePtrT(subNode, allocator(), TreeT::DeleterT(manager));
  }

  template <typename T>
  typename TreeT::NodePtrT addNode(TreeT* base, DecomposablePath& path, const T& data,
                                   bool overwrite, unsigned int flags,
                                   const VoidAllocatorT& allocator)
  {
    if (!path.peekNext()) {
      typename TreeT::NodePtrT newNode = base->node(path.current());

      if (!newNode) {
        // last name component, should be the filename
        TreeT* node       = createSubNode(allocator, path.current(), flags, data);
        newNode           = createSubPtr(node);
        newNode->m_Self   = TreeT::WeakPtrT(newNode);
        newNode->m_Parent = base->m_Self;
        base->set(StringT(path.current(), allocator), newNode);
        return newNode;
      } else if (overwrite) {
        newNode->m_Data  = createData<typename TreeT::DataT, T>(data, allocator);
        newNode->m_Flags = static_cast<usvfs::shared::TreeFlags>(flags);
        return newNode;
      } else {
        // the node is already in the tree, overwrite is false, nothing to do
        return {};
      }
    } else {
      // not last component, continue search in child node
      auto subNode = base->m_Nodes.find(path.current());

      if (subNode == base->m_Nodes.end()) {
        typename TreeT::NodePtrT newNode = createSubPtr(createSubNode(
            allocator, path.current(), FLAG_DIRECTORY | FLAG_DUMMY, createEmpty()));

        subNode =
            base->m_Nodes.emplace(StringT(path.current(), allocator), newNode).first;
        subNode->second->m_Self   = TreeT::WeakPtrT(subNode->second);
        subNode->second->m_Parent = base->m_Self;
      }

      path.next();

      return addNode(subNode->second.get().get(), path, data, overwrite, flags,
                     allocator);
    }
  }

  /**
   * @brief copy content of one tree to a different tree (in a different shared memory
   * segment
   * @param destination
   * @param reference
   * @note at the time this is called, destination needs to refer to the shm of
   * "destination" so that objects can be allocated in the new tree
   */
  void copyTree(TreeT* destination, const TreeT* reference)
  {
    VoidAllocatorT allocator = VoidAllocatorT(m_SHM->get_segment_manager());
    destination->m_Flags     = reference->m_Flags;
    dataAssign(destination->m_Data, reference->m_Data);
    destination->m_Name.assign(reference->m_Name.c_str());

    for (const auto& kv : reference->m_Nodes) {
      TreeT* newNode = createSubNode(allocator, "", true, createEmpty());
      typename TreeT::NodePtrT newNodePtr = createSubPtr(newNode);

      // need to set self BEFORE recursively copying the subtree, otherwise
      // how would we assign parent pointers?
      newNode->m_Self = newNodePtr;

      TreeT* source = reinterpret_cast<TreeT*>(kv.second.get().get());
      copyTree(newNode, source);
      destination->set(newNode->m_Name, newNodePtr);
      newNode->m_Parent = destination->m_Self;
    }
  }

  int increaseRefCount(TreeMeta* treeMeta)
  {
    bi::scoped_lock<bi::interprocess_mutex> lock(treeMeta->mutex);
    return ++treeMeta->referenceCount;
  }

  int decreaseRefCount(TreeMeta* treeMeta)
  {
    bi::scoped_lock<bi::interprocess_mutex> lock(treeMeta->mutex);
    return --treeMeta->referenceCount;
  }

  // see activateSHM() for return value
  //
  std::optional<std::string> createOrOpen(const std::string& SHMName, size_t size)
  {
    SharedMemoryT* newSHM = openSHM(SHMName);

    if (newSHM) {
      spdlog::get("usvfs")->info("{} opened in process {}", SHMName,
                                 ::GetCurrentProcessId());
    } else {
      newSHM = createSHM(SHMName, size);

      if (newSHM) {
        spdlog::get("usvfs")->info("{} created in process {}", SHMName,
                                   ::GetCurrentProcessId());
      }
    }

    if (!newSHM) {
      spdlog::get("usvfs")->error("failed to create or open {} in process {}", SHMName,
                                  ::GetCurrentProcessId());

      throw std::exception("no shm instance");
    }

    return activateSHM(newSHM, SHMName);
  }

  // see activateSHM() for return value
  //
  SharedMemoryT* createSHM(const std::string& SHMName, size_t size)
  {
    try {
      return new SharedMemoryT(bi::create_only, SHMName.c_str(),
                               static_cast<unsigned int>(size));
    } catch (const bi::interprocess_exception&) {
    }

    return nullptr;
  }

  // see activateSHM() for return value
  //
  SharedMemoryT* openSHM(const std::string& SHMName)
  {
    try {
      return new SharedMemoryT(bi::open_only, SHMName.c_str());
    } catch (const bi::interprocess_exception&) {
    }

    return nullptr;
  }

  // makes the given shm current, returns the name of the previous shm block
  // if it is now unused and must be destroyed; if the block is still used by
  // another process, returns empty
  //
  // see reassign()
  //
  std::optional<std::string> activateSHM(SharedMemoryT* shm, const std::string& SHMName)
  {
    std::shared_ptr<SharedMemoryT> oldSHM = m_SHM;

    m_SHM.reset(shm);
    std::pair<TreeMeta*, SharedMemoryT::size_type> res = m_SHM->find<TreeMeta>("Meta");

    if (res.first == nullptr) {
      res.first = m_SHM->construct<TreeMeta>("Meta")(createEmpty(),
                                                     m_SHM->get_segment_manager());
      if (res.first == nullptr) {
        USVFS_THROW_EXCEPTION(bi::bad_alloc());
      }
      if (m_TreeMeta != nullptr) {
        copyTree(res.first->tree.get(), m_TreeMeta->tree.get());
      }
    }

    increaseRefCount(res.first);

    std::optional<std::string> deadSHMName;

    if (oldSHM.get() != nullptr) {
      const bool lastUser = unassign(oldSHM, m_TreeMeta);
      if (lastUser) {
        deadSHMName = m_SHMName;
      }
    }

    m_TreeMeta = res.first;
    m_SHMName  = SHMName;

    return deadSHMName;
  }

  static std::string followupName(const std::string& currentName)
  {
    std::regex pattern(R"exp((.*_)(\d+))exp");
    std::smatch match;
    regex_match(currentName, match, pattern);

    if (match.size() != 3) {
      USVFS_THROW_EXCEPTION(usage_error() << ex_msg("shared memory name invalid"));
    }

    const int count = boost::lexical_cast<int>(match[2]);
    return match[1].str() + std::to_string(count + 1);
  }

  bool unassign(const std::shared_ptr<SharedMemoryT>& shm, TreeMeta* tree)
  {
    if (tree == nullptr) {
      return true;
    }

    if (decreaseRefCount(tree) == 0) {
      shm->get_segment_manager()->destroy_ptr(tree);
      return true;
    } else {
      return false;
    }
  }

  // re-entrancy: every time a process switches to a new shared memory block, it
  // will know whether it was the last process to have a handle to it; when that
  // happens, the block will be destroyed to avoid leaking it
  //
  // destroying these blocks is somewhat dangerous: it ends up in boost, which
  // will try to access the filesystem to see if the name of the shared memory
  // corresponds to a file on the drive, which can call hooked functions and end
  // up right back here
  //
  // (note that in usvfs, only the shared memory for the log file uses a real
  // file on the filesystem, see shmlogger.cpp; all the tree stuff uses
  // anonymous, memory mapped files that live in the Windows pagefile)
  //
  // so the old blocks can be destroyed, but only after all the shenanigans with
  // finding the correct shared memory block are over and `m_TreeMeta` points to
  // a valid block, so all the names of the dead shared memory blocks are kept
  // in a vector and deallocated at the very end
  //
  void reassign()
  {
    // list of all the shared memory blocks that are now unused and can be
    // destroyed
    std::vector<std::string> deadSHMNames;

    if (m_TreeMeta->outdated) {
      // this block was marked as outdated, which should only happen when
      // another process has ran out of memory and started allocating blocks
      // with higher numbers

      spdlog::get("usvfs")->info("tree {0} is outdated, looking for another one",
                                 m_SHMName);

      if (findNewerBlock(deadSHMNames)) {
        // the new block was found and activated
        return;
      }
    } else {
      // this block isn't outdated, so reassign() was called because a bad_alloc
      // exception was thrown
      spdlog::get("usvfs")->info(
          "ran out of memory in tree {0}, will create another one", m_SHMName);
    }

    // either the block is full or it's outdated, but no higher block was found;
    // just create a new one

    createNewBlock(deadSHMNames);

    // remove the old shared memory blocks; this can be recursive and call
    // reassign() again, but it's safe at this point
    for (const std::string& name : deadSHMNames) {
      spdlog::get("usvfs")->info("destroying {0}", name);
      bi::shared_memory_object::remove(name.c_str());
    }
  }

  // tries a series of shm names above the current one in the hope of finding
  // the most recent one
  //
  // if the new block was found and activated correctly, returns true
  //
  // when findNewerBlock() returns false, this container will either still be
  // pointing to the same, outdated block as it started with, or it might have
  // moved up to another outdated block, but it will always be pointing to
  // a valid block in memory
  //
  bool findNewerBlock(std::vector<std::string>& deadSHMNames)
  {
    // how many blocks are checked above the current one; it's unlikely there
    // will ever be more than 15 to 20 blocks allocated, but this is high
    // because it's really the only way to keep processes connected to the same
    // shm block, so processes must not fail to find the next one
    constexpr int Tries = 100;

    std::string nextName = m_SHMName;

    for (int i = 0; i < Tries; ++i) {
      // the shm name is something like "mod_organizer_3", which becomes
      // "mod_organizer_4"
      nextName = followupName(nextName);
      spdlog::get("usvfs")->info("opening {0}", nextName);

      // open the shm, see if it exists
      SharedMemoryT* shm = openSHM(nextName);

      if (!shm) {
        // this is not necessarily an error, another process might have created
        // a bunch of blocks and destroyed them before this process had a chance
        // to see them, so just keep going
        spdlog::get("usvfs")->info("{0} doesn't exist", nextName);
        continue;
      }

      spdlog::get("usvfs")->info("{0} exists, activating", nextName);
      const auto deadSHMName = activateSHM(shm, nextName);

      // if this process was the last user of the previous block, it must be
      // deallocated, but only after this whole thing is finished, because it
      // can end up calling reassign() again
      if (deadSHMName) {
        spdlog::get("usvfs")->info("will destroy {0}", *deadSHMName);
        deadSHMNames.push_back(*deadSHMName);
      }

      // another process might have already created this block, run out of
      // memory and created more, so make sure to only stop when finding a block
      // that's not outdated
      if (m_TreeMeta->outdated) {
        spdlog::get("usvfs")->info("{0} is also outdated", nextName);
      } else {
        // shm opened correctly, activated and not outdated, done
        spdlog::get("usvfs")->info("{0} not outdated, taking it, size now {1}",
                                   nextName, byte_string(m_SHM->get_size()));

        return true;
      }
    }

    // this block is outdated, but no other valid block was found above; this
    // shouldn't happen, but just create a new block to make sure programs can
    // still run

    spdlog::get("usvfs")->error(
        "found no existing tree above {0}, will create a new one", m_SHMName);

    return false;
  }

  // creates a new block and activates it, throws on failure
  //
  void createNewBlock(std::vector<std::string>& deadSHMNames)
  {
    // the current block is now considered stale, so make sure other processes
    // are aware of it and try to find the new block
    //
    // todo: there really should be some synchronization here, another process
    //       can pick up this flag before the next block has been created
    m_TreeMeta->outdated = true;

    // the shm name is something like "mod_organizer_3", which becomes
    // "mod_organizer_4"
    const std::string nextName = followupName(m_SHMName);
    spdlog::get("usvfs")->info("creating {0}", nextName);

    SharedMemoryT* shm = createSHM(nextName, m_SHM->get_size() * 2);

    if (!shm) {
      // this shouldn't happen
      spdlog::get("usvfs")->error("failed to create {0}", nextName);
      throw std::exception("cannot create block");
    }

    spdlog::get("usvfs")->info("{0} created, activating", nextName);
    const auto deadSHMName = activateSHM(shm, nextName);

    // if this process was the last user of the previous block, it must be
    // deallocated, but only after this whole thing is finished, because it
    // can end up calling reassign() again
    if (deadSHMName) {
      spdlog::get("usvfs")->info("will destroy {0}", *deadSHMName);
      deadSHMNames.push_back(*deadSHMName);
    }

    spdlog::get("usvfs")->info("tree {0} size now {1}", m_SHMName,
                               byte_string(m_SHM->get_size()));
  }
};

}  // namespace usvfs::shared
