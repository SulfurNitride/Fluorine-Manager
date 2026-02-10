#ifndef STARFIELD_MODDATACONTENT_H
#define STARFIELD_MODDATACONTENT_H

#include <gamebryomoddatacontent.h>
#include <ifiletree.h>

class StarfieldModDataContent : public GamebryoModDataContent
{
protected:
  enum StarfieldContent
  {
    CONTENT_MATERIAL   = CONTENT_NEXT_VALUE,
    CONTENT_GEOMETRIES = CONTENT_NEXT_VALUE + 1,
    CONTENT_VIDEO      = CONTENT_NEXT_VALUE + 2
  };

public:
  StarfieldModDataContent(MOBase::IGameFeatures const* gameFeatures)
      : GamebryoModDataContent(gameFeatures)
  {
    m_Enabled[CONTENT_SKYPROC] = false;
  }

  std::vector<Content> getAllContents() const override
  {
    static std::vector<Content> STARFIELD_CONTENTS{
        {CONTENT_MATERIAL, QT_TR_NOOP("Materials"), ":/MO/gui/content/material"},
        {CONTENT_GEOMETRIES, QT_TR_NOOP("Geometries"), ":/MO/gui/content/geometries"},
        {CONTENT_VIDEO, QT_TR_NOOP("Video"), ":/MO/gui/content/media"}};
    auto contents = GamebryoModDataContent::getAllContents();
    std::copy(std::begin(STARFIELD_CONTENTS), std::end(STARFIELD_CONTENTS),
              std::back_inserter(contents));
    return contents;
  }

  std::vector<int>
  getContentsFor(std::shared_ptr<const MOBase::IFileTree> fileTree) const override
  {
    auto contents = GamebryoModDataContent::getContentsFor(fileTree);
    for (auto e : *fileTree) {
      if (e->isDir()) {
        if (e->compare("materials") == 0) {
          contents.push_back(CONTENT_MATERIAL);
        } else if (e->compare("geometries") == 0) {
          contents.push_back(CONTENT_GEOMETRIES);
        } else if (e->compare("video") == 0) {
          contents.push_back(CONTENT_VIDEO);
        }
      }
    }
    return contents;
  }
};

#endif  // STARFIELD_MODDATACONTENT_H
