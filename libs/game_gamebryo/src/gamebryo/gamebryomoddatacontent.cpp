#include "gamebryomoddatacontent.h"

#include <igamefeatures.h>
#include <scriptextender.h>

GamebryoModDataContent::GamebryoModDataContent(
    MOBase::IGameFeatures const* gameFeatures)
    : m_GameFeatures(gameFeatures), m_Enabled(CONTENT_MODGROUP + 1, true)
{}

std::vector<GamebryoModDataContent::Content>
GamebryoModDataContent::getAllContents() const
{
  static std::vector<Content> GAMEBRYO_CONTENTS{
      {CONTENT_PLUGIN, QT_TR_NOOP("Plugins (ESP/ESM/ESL)"), ":/MO/gui/content/plugin"},
      {CONTENT_OPTIONAL, QT_TR_NOOP("Optional Plugins"), "", true},
      {CONTENT_INTERFACE, QT_TR_NOOP("Interface"), ":/MO/gui/content/interface"},
      {CONTENT_MESH, QT_TR_NOOP("Meshes"), ":/MO/gui/content/mesh"},
      {CONTENT_BSA, QT_TR_NOOP("Bethesda Archive"), ":/MO/gui/content/bsa"},
      {CONTENT_SCRIPT, QT_TR_NOOP("Scripts (Papyrus)"), ":/MO/gui/content/script"},
      {CONTENT_SKSE, QT_TR_NOOP("Script Extender Plugin"), ":/MO/gui/content/skse"},
      {CONTENT_SKSE_FILES, QT_TR_NOOP("Script Extender Files"), "", true},
      {CONTENT_SKYPROC, QT_TR_NOOP("SkyProc Patcher"), ":/MO/gui/content/skyproc"},
      {CONTENT_SOUND, QT_TR_NOOP("Sound or Music"), ":/MO/gui/content/sound"},
      {CONTENT_TEXTURE, QT_TR_NOOP("Textures"), ":/MO/gui/content/texture"},
      {CONTENT_MCM, QT_TR_NOOP("MCM Configuration"), ":/MO/gui/content/menu"},
      {CONTENT_INI, QT_TR_NOOP("INI Files"), ":/MO/gui/content/inifile"},
      {CONTENT_FACEGEN, QT_TR_NOOP("FaceGen Data"), ":/MO/gui/content/facegen"},
      {CONTENT_MODGROUP, QT_TR_NOOP("ModGroup Files"), ":/MO/gui/content/modgroup"}};

  // Copy the list of enabled contents:
  std::vector<Content> contents;
  std::copy_if(std::begin(GAMEBRYO_CONTENTS), std::end(GAMEBRYO_CONTENTS),
               std::back_inserter(contents), [this](auto e) {
                 return m_Enabled[e.id()];
               });
  return contents;
}

std::vector<int> GamebryoModDataContent::getContentsFor(
    std::shared_ptr<const MOBase::IFileTree> fileTree) const
{
  std::vector<int> contents;

  for (auto e : *fileTree) {
    if (e->isFile()) {
      auto suffix = e->suffix().toLower();
      if (m_Enabled[CONTENT_PLUGIN] &&
          (suffix == "esp" || suffix == "esm" || suffix == "esl")) {
        contents.push_back(CONTENT_PLUGIN);
      } else if (m_Enabled[CONTENT_BSA] && (suffix == "bsa" || suffix == "ba2")) {
        contents.push_back(CONTENT_BSA);
      } else if (m_Enabled[CONTENT_INI] && suffix == "ini" &&
                 e->compare("meta.ini") != 0) {
        contents.push_back(CONTENT_INI);
      } else if (m_Enabled[CONTENT_MODGROUP] && suffix == "modgroups") {
        contents.push_back(CONTENT_MODGROUP);
      }
    } else {
      if (m_Enabled[CONTENT_TEXTURE] &&
          (e->compare("textures") == 0 || e->compare("icons") == 0 ||
           e->compare("bookart") == 0)) {
        contents.push_back(CONTENT_TEXTURE);
      } else if (m_Enabled[CONTENT_MESH] && e->compare("meshes") == 0) {
        contents.push_back(CONTENT_MESH);
      } else if (m_Enabled[CONTENT_INTERFACE] &&
                 (e->compare("interface") == 0 || e->compare("menus") == 0)) {
        contents.push_back(CONTENT_INTERFACE);
      } else if (m_Enabled[CONTENT_SOUND] && e->compare("music") == 0 ||
                 e->compare("sound") == 0) {
        contents.push_back(CONTENT_SOUND);
      } else if (m_Enabled[CONTENT_SCRIPT] && e->compare("scripts") == 0) {
        contents.push_back(CONTENT_SCRIPT);
      } else if (m_Enabled[CONTENT_SKYPROC] && e->compare("SkyProc Patchers") == 0) {
        contents.push_back(CONTENT_SKYPROC);
      } else if (m_Enabled[CONTENT_MCM] && e->compare("MCM") == 0) {
        contents.push_back(CONTENT_MCM);
      } else if (m_Enabled[CONTENT_OPTIONAL] && e->compare("Optional") == 0 &&
                 e->astree()->size() > 0) {
        contents.push_back(CONTENT_OPTIONAL);
      }
    }
  }

  if (m_Enabled[CONTENT_FACEGEN]) {
    auto e1 = fileTree->findDirectory("meshes/actors/character/facegendata");
    if (e1) {
      contents.push_back(CONTENT_FACEGEN);
    } else {
      auto e2 = fileTree->findDirectory("textures/actors/character/facegendata");
      if (e2) {
        contents.push_back(CONTENT_FACEGEN);
      }
    }
  }

  auto extender = m_GameFeatures->gameFeature<MOBase::ScriptExtender>();
  if (extender != nullptr) {
    auto e = fileTree->findDirectory(extender->PluginPath());
    if (e) {
      if (m_Enabled[CONTENT_SKSE_FILES]) {
        contents.push_back(CONTENT_SKSE_FILES);
      }
      if (m_Enabled[CONTENT_SKSE]) {
        for (auto f : *e) {
          if (f->hasSuffix("dll")) {
            contents.push_back(CONTENT_SKSE);
            break;
          }
        }
      }
    }
  }

  return contents;
}
