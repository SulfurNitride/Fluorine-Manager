#ifndef DUMMY_TREE_H
#define DUMMY_TREE_H

#include <uibase/ifiletree.h>

// filetree implementation for testing purpose
//
class DummyFileTree : public MOBase::IFileTree {
public:
    DummyFileTree(std::shared_ptr<const IFileTree> parent, QString name)
        : FileTreeEntry(parent, name), IFileTree()
    {
    }

protected:
    std::shared_ptr<IFileTree> makeDirectory(std::shared_ptr<const IFileTree> parent,
                                             QString name) const override
    {
        return std::make_shared<DummyFileTree>(parent, name);
    }

    bool doPopulate(std::shared_ptr<const IFileTree>,
                    std::vector<std::shared_ptr<FileTreeEntry>>&) const override
    {
        return true;
    }

    std::shared_ptr<IFileTree> doClone() const override
    {
        return std::make_shared<DummyFileTree>(nullptr, name());
    }
};

#endif
