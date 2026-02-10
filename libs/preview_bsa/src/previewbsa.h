/*
Copyright (C) 2014 Sebastian Herbord. All rights reserved.

This file is part of bsa Preview plugin for MO

bsa Preview plugin is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

bsa Preview plugin is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with bsa Preview plugin.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef PREVIEWBSA_H
#define PREVIEWBSA_H

#include <functional>

#include <bsatk/bsatk.h>

#include <uibase/ipluginpreview.h>

class PreviewBsa : public MOBase::IPluginPreview
{

  Q_OBJECT
  Q_INTERFACES(MOBase::IPlugin MOBase::IPluginPreview)
#if QT_VERSION >= QT_VERSION_CHECK(5, 0, 0)
  Q_PLUGIN_METADATA(IID "org.tannin.PreviewBsa" FILE "previewbsa.json")
#endif

public:
  PreviewBsa();

public:
  virtual bool init(MOBase::IOrganizer* moInfo);
  virtual QString name() const;
  virtual QString localizedName() const;
  virtual QString author() const;
  virtual QString description() const;
  virtual MOBase::VersionInfo version() const;
  virtual QList<MOBase::PluginSetting> settings() const;

public:
  virtual std::set<QString> supportedExtensions() const;
  virtual QWidget* genFilePreview(const QString& fileName, const QSize& maxSize) const;

private:
  void readFiles(const BSA::Folder::Ptr folder);
  QWidget* genBsaPreview(const QString& fileName, const QSize& maxSize);
  QString getFormat(ArchiveType type) const;
  BSAULong getVersion(ArchiveType type) const;

private:
  std::map<QString, std::function<QWidget*(const QString&, const QSize&)>>
      m_PreviewGenerators;

private:
  const MOBase::IOrganizer* m_MOInfo;
  QStringList m_Files;
};

#endif  // PREVIEWBSA_H
