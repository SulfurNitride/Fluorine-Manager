#ifndef PLUGINREQUIREMENTS_H
#define PLUGINREQUIREMENTS_H

#include <functional>
#include <memory>
#include <optional>

#include <QList>
#include <QString>

#include "dllimport.h"

namespace MOBase
{

class IOrganizer;
class IPluginDiagnose;

/**
 * @brief The interface for plugin requirements.
 */
class IPluginRequirement
{
public:
  class Problem
  {
  public:
    /**
     * @return a short description for the problem.
     */
    QString shortDescription() const { return m_ShortDescription; }

    /**
     * @return a long description for the problem.
     */
    QString longDescription() const { return m_LongDescription; }

    /**
     *
     */
    Problem(QString shortDescription, QString longDescription = "")
        : m_ShortDescription(shortDescription),
          m_LongDescription(longDescription.isEmpty() ? shortDescription
                                                      : longDescription)
    {}

  private:
    QString m_ShortDescription, m_LongDescription;
  };

public:
  /**
   * @brief Check if the requirements is met.
   *
   * @param organizer The organizer proxy.
   *
   * @return a problem if the requirement is not met, otherwise an empty optional.
   */
  virtual std::optional<Problem> check(IOrganizer* organizer) const = 0;

  virtual ~IPluginRequirement() {}
};

/**
 * @brief Plugin dependency - The requirement is met if one of the
 *     given plugin is active.
 */
class QDLLEXPORT PluginDependencyRequirement : public IPluginRequirement
{

  friend class PluginRequirementFactory;

public:
  PluginDependencyRequirement(QStringList const& pluginNames);

  virtual std::optional<Problem> check(IOrganizer* organizer) const;

  /**
   * @return the list of plugin names in this dependency requirement.
   */
  QStringList pluginNames() const { return m_PluginNames; }

protected:
  QString message() const;
  QStringList m_PluginNames;
};

/**
 * @brief Game dependency - The requirement is met if the active game
 *     is one of the specified game.
 */
class QDLLEXPORT GameDependencyRequirement : public IPluginRequirement
{

  friend class PluginRequirementFactory;

public:
  GameDependencyRequirement(QStringList const& gameNames);

  virtual std::optional<Problem> check(IOrganizer* organizer) const;

  /**
   * @return the list of game names in this dependency requirement.
   */
  QStringList gameNames() const { return m_GameNames; }

protected:
  QString message() const;
  QStringList m_GameNames;
};

/**
 * @brief Diagnose dependency - This wrap a IPluginDiagnose into a plugin
 *     requirements.
 *
 * If the wrapped diagnose plugin reports a problem, the requirement fails
 * and the associated message is the one from the diagnose plugin (or the
 * list of messages if multiple problems were reported).
 */
class DiagnoseRequirement : public IPluginRequirement
{

  friend class PluginRequirementFactory;

public:
  DiagnoseRequirement(const IPluginDiagnose* diagnose);

  virtual std::optional<Problem> check(IOrganizer* organizer) const;

private:
  const IPluginDiagnose* m_Diagnose;
};

/**
 * Factory for plugin requirements.
 */
class QDLLEXPORT PluginRequirementFactory
{
public:
  /**
   * @brief Create a new plugin dependency. The requirement is met if one of the
   *     given plugin is enabled.
   *
   * If you want all plugins to be active, simply create multiple requirements.
   *
   * @param pluginNames Name of the plugin required.
   */
  static std::shared_ptr<const IPluginRequirement>
  pluginDependency(QStringList const& pluginNames);
  static std::shared_ptr<const IPluginRequirement>
  pluginDependency(QString const& pluginName)
  {
    return pluginDependency(QStringList{pluginName});
  }

  /**
   * @brief Create a new plugin dependency. The requirement is met if the current
   *     game plugin matches one of the given name.
   *
   * @param gameName Name of the game required.
   *
   * @note This differ from makePluginDependency only for the message.
   */
  static std::shared_ptr<const IPluginRequirement>
  gameDependency(QStringList const& gameNames);
  static std::shared_ptr<const IPluginRequirement>
  gameDependency(QString const& gameName)
  {
    return gameDependency(QStringList{gameName});
  }

  /**
   * @brief Create a requirement from the given diagnose plugin.
   *
   * @param diagnose The diagnose plugin.
   */
  static std::shared_ptr<const IPluginRequirement>
  diagnose(const IPluginDiagnose* diagnose);

  /**
   * @brief Create a generic requirement with the given checker and message.
   *
   * @param checker The function to use to check if the requirement is met (should
   *     return true if the requirement is met).
   * @param description The description to show user if the requirement is not met.
   */
  static std::shared_ptr<const IPluginRequirement>
  basic(std::function<bool(IOrganizer*)> const& checker, QString const description);
};

}  // namespace MOBase

#endif
