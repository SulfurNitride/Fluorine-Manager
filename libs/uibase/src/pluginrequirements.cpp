#include <uibase/pluginrequirements.h>

#include <QCoreApplication>

#include <uibase/imoinfo.h>
#include <uibase/iplugindiagnose.h>
#include <uibase/iplugingame.h>

using namespace MOBase;

// Plugin and Game dependencies

PluginDependencyRequirement::PluginDependencyRequirement(QStringList const& pluginNames)
    : m_PluginNames(pluginNames)
{}

std::optional<IPluginRequirement::Problem>
PluginDependencyRequirement::check(IOrganizer* o) const
{
  for (auto const& pluginName : m_PluginNames) {
    if (o->isPluginEnabled(pluginName)) {
      return {};
    }
  }
  return Problem(message());
}

QString PluginDependencyRequirement::message() const
{
  if (m_PluginNames.size() > 1) {
    return QObject::tr("One of the following plugins must be enabled: %1.")
        .arg(m_PluginNames.join(", "));
  } else {
    return QObject::tr("This plugin can only be enabled if the '%1' plugin is "
                       "installed and enabled.")
        .arg(m_PluginNames[0]);
  }
}

GameDependencyRequirement::GameDependencyRequirement(QStringList const& gameNames)
    : m_GameNames(gameNames)
{}

std::optional<IPluginRequirement::Problem>
GameDependencyRequirement::check(IOrganizer* o) const
{
  auto* game = o->managedGame();
  if (!game) {
    return Problem(message());
  }

  QString gameName = game->gameName();
  for (auto const& pluginName : m_GameNames) {
    if (pluginName.compare(gameName, Qt::CaseInsensitive) == 0) {
      return {};
    }
  }
  return Problem(message());
}

QString GameDependencyRequirement::message() const
{
  return QObject::tr("This plugin can only be enabled for the following game(s): %1.",
                     "", static_cast<int>(m_GameNames.size()))
      .arg(m_GameNames.join(", "));
}

// Diagnose requirements

DiagnoseRequirement::DiagnoseRequirement(const IPluginDiagnose* diagnose)
    : m_Diagnose(diagnose)
{}

std::optional<IPluginRequirement::Problem> DiagnoseRequirement::check(IOrganizer*) const
{
  auto activeProblems = m_Diagnose->activeProblems();

  if (activeProblems.empty()) {
    return {};
  }

  QStringList shortDescriptions, longDescriptions;
  for (auto i : activeProblems) {
    shortDescriptions.append(m_Diagnose->shortDescription(i));
    longDescriptions.append(m_Diagnose->fullDescription(i));
  }

  return Problem(shortDescriptions.join("\n"), longDescriptions.join("\n"));
}

// Basic requirements
class BasicPluginRequirement : public IPluginRequirement
{
public:
  BasicPluginRequirement(std::function<bool(IOrganizer*)> const& checker,
                         QString const description)
      : m_Checker(checker), m_Description(description)
  {}

  std::optional<Problem> check(IOrganizer* o) const
  {
    if (m_Checker(o)) {
      return {};
    }
    return Problem(m_Description);
  }

private:
  std::function<bool(IOrganizer*)> m_Checker;
  QString m_Description;
};

// Factory

std::shared_ptr<const IPluginRequirement>
PluginRequirementFactory::pluginDependency(QStringList const& pluginNames)
{
  return std::make_shared<PluginDependencyRequirement>(pluginNames);
}

std::shared_ptr<const IPluginRequirement>
PluginRequirementFactory::gameDependency(QStringList const& pluginGameNames)
{
  return std::make_shared<GameDependencyRequirement>(pluginGameNames);
}

std::shared_ptr<const IPluginRequirement>
PluginRequirementFactory::diagnose(const IPluginDiagnose* diagnose)
{
  return std::make_shared<DiagnoseRequirement>(diagnose);
}

std::shared_ptr<const IPluginRequirement>
PluginRequirementFactory::basic(std::function<bool(IOrganizer*)> const& checker,
                                QString const description)
{
  return std::make_shared<BasicPluginRequirement>(checker, description);
}
