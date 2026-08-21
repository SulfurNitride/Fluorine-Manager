#include "vfs/runtimeindex.h"

#include <gtest/gtest.h>

#include <chrono>

namespace
{
VfsTree sampleTree()
{
  VfsTree tree;
  tree.root.is_directory = true;
  tree.root.insertFile({"Data", "Textures", "Stone.dds"}, "/mods/stone.dds",
                       17, std::chrono::system_clock::time_point{}, "Textures",
                       false, 0644);
  tree.root.insertFile({"Root.esm"}, "/mods/Root.esm", 23,
                       std::chrono::system_clock::time_point{}, "Root", false,
                       0644);
  tree.file_count = 2;
  tree.dir_count = 3;
  return tree;
}
}  // namespace

TEST(VfsRuntimeIndex, ResolvesEveryBaseEntryWithoutWarmup)
{
  InodeTable inodes;
  const auto index = VfsRuntimeIndex::build(sampleTree(), inodes);

  const auto data = index->lookup(1, "dAtA");
  ASSERT_EQ(data.source, VfsLookupSource::Base);
  ASSERT_TRUE(data.node.has_value());
  const auto textures = index->lookup(data.node->ino, "TEXTURES");
  ASSERT_EQ(textures.source, VfsLookupSource::Base);
  const auto stone = index->lookup(textures.node->ino, "stone.DDS");
  ASSERT_EQ(stone.source, VfsLookupSource::Base);
  EXPECT_EQ(stone.node->real_path, "/mods/stone.dds");
  EXPECT_EQ(stone.node->size, 17);
}

TEST(VfsRuntimeIndex, NegativeCacheCannotHideBaseEntry)
{
  InodeTable inodes;
  const auto index = VfsRuntimeIndex::build(sampleTree(), inodes);
  index->recordNegative(1, "Root.esm", std::chrono::hours(1));
  EXPECT_EQ(index->lookup(1, "root.esm").source, VfsLookupSource::Base);

  EXPECT_EQ(index->lookup(1, "Missing.esm").source, VfsLookupSource::Missing);
  index->recordNegative(1, "Missing.esm", std::chrono::hours(1));
  EXPECT_EQ(index->lookup(1, "missing.esm").source,
            VfsLookupSource::Negative);
}

TEST(VfsRuntimeIndex, OverlayAndTombstoneOverrideBaseWithoutEviction)
{
  InodeTable inodes;
  const auto index = VfsRuntimeIndex::build(sampleTree(), inodes);
  const uint64_t ino = inodes.get("Root.esm");
  auto replacement = VfsRuntimeIndex::makeFileNode(
      ino, "Root.esm", "/staging/Root.esm", false, 99,
      std::chrono::system_clock::time_point{}, 0600);

  index->publish(1, "Root.esm", replacement);
  auto found = index->lookup(1, "root.esm");
  ASSERT_EQ(found.source, VfsLookupSource::Overlay);
  EXPECT_EQ(found.node->real_path, "/staging/Root.esm");
  EXPECT_EQ(found.node->size, 99);

  index->tombstone(1, "Root.esm", ino);
  EXPECT_EQ(index->lookup(1, "root.esm").source,
            VfsLookupSource::Tombstone);
  EXPECT_FALSE(index->node(ino).has_value());

  index->publish(1, "ROOT.ESM", replacement);
  EXPECT_EQ(index->lookup(1, "root.esm").source,
            VfsLookupSource::Overlay);
  EXPECT_EQ(index->baseLookupCount(), 4u);
}

TEST(VfsRuntimeIndex, RootCreateDoesNotRemoveBaseSiblings)
{
  InodeTable inodes;
  const auto index = VfsRuntimeIndex::build(sampleTree(), inodes);
  const std::size_t baseCount = index->baseLookupCount();
  const uint64_t createdIno = inodes.getOrCreate("Runtime.log");
  auto created = VfsRuntimeIndex::makeFileNode(
      createdIno, "Runtime.log", "/staging/Runtime.log", false, 0,
      std::chrono::system_clock::time_point{}, 0644);
  index->publish(1, "Runtime.log", created);

  EXPECT_EQ(index->baseLookupCount(), baseCount);
  EXPECT_EQ(index->lookup(1, "Root.esm").source, VfsLookupSource::Base);
  EXPECT_EQ(index->lookup(1, "runtime.log").source,
            VfsLookupSource::Overlay);
}
