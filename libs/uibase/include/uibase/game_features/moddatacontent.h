#ifndef UIBASE_GAMEFEATURES_MODDATACONTENT_H
#define UIBASE_GAMEFEATURES_MODDATACONTENT_H

#include <algorithm>
#include <memory>
#include <vector>

#include <QString>

#include "./game_feature.h"

namespace MOBase
{
class IFileTree;

/**
 * The ModDataContent feature is used (when available) to indicate to users the content
 * of mods in the "Content" column.
 *
 * The feature exposes a list of possible content types, each associated with an ID, a
 * name and an icon. The icon is the path to either:
 *   - A Qt resource or;
 *   - A file on the disk.
 *
 * In order to facilitate the implementation, MO2 already provides a set of icons that
 * can be used. Those icons are all under :/MO/gui/content (e.g. :/MO/gui/content/plugin
 * or :/MO/gui/content/music).
 *
 * The list of available icons is:
 *  - plugin:
 * https://github.com/ModOrganizer2/modorganizer/blob/master/src/resources/contents/jigsaw-piece.png
 *  - skyproc:
 * https://github.com/ModOrganizer2/modorganizer/blob/master/src/resources/contents/hand-of-god.png
 *  - texture:
 * https://github.com/ModOrganizer2/modorganizer/blob/master/src/resources/contents/empty-chessboard.png
 *  - music:
 * https://github.com/ModOrganizer2/modorganizer/blob/master/src/resources/contents/double-quaver.png
 *  - sound:
 * https://github.com/ModOrganizer2/modorganizer/blob/master/src/resources/contents/lyre.png
 *  - interface:
 * https://github.com/ModOrganizer2/modorganizer/blob/master/src/resources/contents/usable.png
 *  - skse:
 * https://github.com/ModOrganizer2/modorganizer/blob/master/src/resources/contents/checkbox-tree.png
 *  - script:
 * https://github.com/ModOrganizer2/modorganizer/blob/master/src/resources/contents/tinker.png
 *  - mesh:
 * https://github.com/ModOrganizer2/modorganizer/blob/master/src/resources/contents/breastplate.png
 *  - string:
 * https://github.com/ModOrganizer2/modorganizer/blob/master/src/resources/contents/conversation.png
 *  - bsa:
 * https://github.com/ModOrganizer2/modorganizer/blob/master/src/resources/contents/locked-chest.png
 *  - menu:
 * https://github.com/ModOrganizer2/modorganizer/blob/master/src/resources/contents/config.png
 *  - inifile:
 * https://github.com/ModOrganizer2/modorganizer/blob/master/src/resources/contents/feather-and-scroll.png
 *  - modgroup:
 * https://github.com/ModOrganizer2/modorganizer/blob/master/src/resources/contents/xedit.png
 */
class ModDataContent : public details::GameFeatureCRTP<ModDataContent>
{
public:
  struct Content
  {

    /**
     * @param id ID of this content.
     * @param name Name of this content.
     * @param icon Path to the icon for this content. Can be either a path
     *     to an image on the disk, or to a resource. Can be an empty string if
     * filterOnly is true.
     * @param filterOnly Indicates if the content should only be show in the filter
     *     criteria and not in the actual Content column.
     */
    Content(int id, QString name, QString icon, bool filterOnly = false)
        : m_Id{id}, m_Name{name}, m_Icon{icon}, m_FilterOnly{filterOnly}
    {}

    /**
     * @return the ID of this content.
     */
    int id() const { return m_Id; }

    /**
     * @return the name of this content.
     */
    QString name() const { return m_Name; }

    /**
     * @return the path to the icon of this content (can be a Qt resource path).
     */
    QString icon() const { return m_Icon; }

    /**
     * @return true if this content is only meant to be used as a filter criteria.
     */
    bool isOnlyForFilter() const { return m_FilterOnly; }

  private:
    int m_Id;
    QString m_Name;
    QString m_Icon;
    bool m_FilterOnly;
  };

  /**
   * @return the list of all possible contents for the corresponding game.
   */
  virtual std::vector<Content> getAllContents() const = 0;

  /**
   * @brief Retrieve the list of contents in the given tree.
   *
   * @param fileTree The tree corresponding to the mod to retrieve contents for.
   *
   * @return the IDs of the content in the given tree.
   */
  virtual std::vector<int>
  getContentsFor(std::shared_ptr<const MOBase::IFileTree> fileTree) const = 0;
};
}  // namespace MOBase

#endif
