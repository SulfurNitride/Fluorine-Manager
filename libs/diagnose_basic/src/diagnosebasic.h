/*
Copyright (C) 2013 Sebastian Herbord. All rights reserved.

This file is part of basic diagnosis plugin for MO

This plugin is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This plugin is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this plugin.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef DIAGNOSEBASIC_H
#define DIAGNOSEBASIC_H

#include <QRegularExpression>
#include <QSet>
#include <QString>

#include <uibase/imodlist.h>
#include <uibase/imoinfo.h>
#include <uibase/iplugin.h>
#include <uibase/iplugindiagnose.h>

class DiagnoseBasic : public QObject,
                      public MOBase::IPlugin,
                      public MOBase::IPluginDiagnose
{
  Q_OBJECT
  Q_INTERFACES(MOBase::IPlugin MOBase::IPluginDiagnose)
#if QT_VERSION >= QT_VERSION_CHECK(5, 0, 0)
  Q_PLUGIN_METADATA(IID "org.tannin.DiagnoseBasic")
#endif

public:
  DiagnoseBasic();

public:  // IPlugin
  virtual bool init(MOBase::IOrganizer* moInfo);
  virtual QString name() const;
  virtual QString author() const;
  virtual QString description() const;
  virtual MOBase::VersionInfo version() const;
  virtual bool isActive() const;
  virtual QList<MOBase::PluginSetting> settings() const;

public:  // IPluginDiagnose
  virtual std::vector<unsigned int> activeProblems() const;
  virtual QString shortDescription(unsigned int key) const;
  virtual QString fullDescription(unsigned int key) const;
  virtual bool hasGuidedFix(unsigned int key) const;
  virtual void startGuidedFix(unsigned int key) const;

private:
  bool errorReported() const;
  bool overwriteFiles() const;
  bool invalidFontConfig() const;
  bool nitpickInstalled() const;
  bool assetOrder() const;
  bool missingMasters() const;
  bool alternateGame() const;
  bool fileAttributes(const QString& executable) const;

private:
  static const unsigned int PROBLEM_ERRORLOG         = 1;
  static const unsigned int PROBLEM_OVERWRITE        = 2;
  static const unsigned int PROBLEM_INVALIDFONT      = 3;
  static const unsigned int PROBLEM_NITPICKINSTALLED = 4;
  static const unsigned int PROBLEM_PROFILETWEAKS    = 7;
  static const unsigned int PROBLEM_MISSINGMASTERS   = 8;
  static const unsigned int PROBLEM_ALTERNATE        = 9;

  static const unsigned int NUM_CONTEXT_ROWS = 5;

  static const QRegularExpression RE_LOG_FILE;

private:
  struct ListElement
  {
    QString espName;
    QString modName;
    int pluginPriority;
    int modPriority;
    int sortGroup;
    bool avoidMove;
    QSet<QString> relevantScripts;
  };

  struct Move
  {
    ListElement item;
    ListElement reference;
    enum EType
    {
      BEFORE,
      AFTER
    } type;
    Move(const ListElement& initItem, const ListElement& initReference, EType initType)
        : item(initItem), reference(initReference), type(initType)
    {}
  };

  struct Sorter
  {
    struct
    {
      int operator()(const ListElement& lhs, const ListElement& rhs)
      {
        return lhs.modPriority < rhs.modPriority;
      }
    } minMod;

    std::vector<Move> moves;

    void operator()(std::vector<ListElement> modList);

  private:
    void sortGroup(std::vector<ListElement> modList);
  };

  friend bool operator<(const Move& lhs, const Move& rhs);

private:
  void topoSort(std::vector<ListElement>& list) const;
  bool checkEmpty(const QString& path) const;

private:
  MOBase::IOrganizer* m_MOInfo;
  mutable QString m_ErrorMessage;
  mutable QString m_NewestModlistBackup;
  mutable std::set<QString> m_MissingMasters;
  mutable std::map<QString, std::set<QString>> m_PluginChildren;
};

#endif  // DIAGNOSEBASIC_H
