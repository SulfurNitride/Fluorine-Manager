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

TEST(VfsRuntimeIndex, CaseOnlyProviderOverridePreservesListedSpelling)
{
  VfsTree tree;
  tree.root.is_directory = true;
  tree.root.insertFile({"Interface", "Compass.swf"},
                       "/base/Interface/Compass.swf", 11,
                       std::chrono::system_clock::time_point{}, "Base", true,
                       0644);
  tree.root.insertFile({"interface", "compass.swf"},
                       "/mods/UI/interface/compass.swf", 22,
                       std::chrono::system_clock::time_point{}, "UI", false,
                       0644);
  tree.file_count = 1;
  tree.dir_count = 2;

  const VfsNode* winner = tree.root.resolve({"INTERFACE", "COMPASS.SWF"});
  ASSERT_NE(winner, nullptr);
  ASSERT_FALSE(winner->is_directory);
  EXPECT_EQ(winner->file_info.real_path, "/mods/UI/interface/compass.swf");
  EXPECT_EQ(winner->file_info.origin, "UI");

  const auto rootEntries = tree.root.listChildren();
  ASSERT_EQ(rootEntries.size(), 1u);
  EXPECT_EQ(rootEntries.front().first, "Interface");
  const auto interfaceEntries = rootEntries.front().second->listChildren();
  ASSERT_EQ(interfaceEntries.size(), 1u);
  EXPECT_EQ(interfaceEntries.front().first, "Compass.swf");

  InodeTable inodes;
  const auto index = VfsRuntimeIndex::build(tree, inodes);
  const auto interface = index->lookup(1, "interface");
  ASSERT_EQ(interface.source, VfsLookupSource::Base);
  ASSERT_TRUE(interface.node.has_value());
  const auto compass = index->lookup(interface.node->ino, "compass.swf");
  ASSERT_EQ(compass.source, VfsLookupSource::Base);
  ASSERT_TRUE(compass.node.has_value());
  EXPECT_EQ(compass.node->virtual_path, "Interface/Compass.swf");
  EXPECT_EQ(compass.node->real_path, "/mods/UI/interface/compass.swf");
}
