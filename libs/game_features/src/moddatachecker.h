#ifndef MODDATACHECKER_H
#define MODDATACHECKER_H

#include <memory>

namespace MOBase {

  class IFileTree;

};

class ModDataChecker {
public:

  /**
   *
   */
  enum class CheckReturn {
    INVALID,
    FIXABLE,
    VALID
  };

  /**
   * @brief Check that the given filetree represent a valid mod layout, or can be easily
   *   fixed.
   *
   * This method is mainly used during installation (to find which installer should
   * be used or to recurse into multi-level archives), or to quickly indicates to a 
   * user if a mod looks valid.
   *
   * This method does not have to be exact, it only has to indicate if the given tree
   * looks like a valid mod or not by quickly checking the structure (heavy operations
   * should be avoided).
   *
   * If the tree can be fixed by the `fix()` method, this method should return `FIXABLE`.
   * `FIXABLE` should only be returned when it is guaranteed that `fix()` can fix the tree.
   *
   * @param tree The tree starting at the root of the "data" folder.
   *
   * @return whether the tree is invalid, fixable or valid.
   */
  virtual CheckReturn dataLooksValid(std::shared_ptr<const MOBase::IFileTree> fileTree) const = 0;

  /**
   * @brief Try to fix the given tree.
   *
   * This method is used during installation to try to fix invalid archives and will only be
   * called if dataLooksValid returned `FIXABLE`.
   *
   * @param tree The tree to try to fix. Can be modified during the process.
   *
   * @return the fixed tree, or a null pointer if the tree could not be fixed.
   */
  virtual std::shared_ptr<MOBase::IFileTree> fix(std::shared_ptr<MOBase::IFileTree> fileTree) const {
    return nullptr;
  }

public:

  /**
   *
   */
  virtual ~ModDataChecker() { }

};

#endif