/*
Copyright (C) 2014 Sebastian Herbord. All rights reserved.

This file is part of Base Preview plugin for MO

Base Preview plugin is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Base Preview plugin is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Base Preview plugin.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef PREVIEWBASE_H
#define PREVIEWBASE_H

#include <functional>

#include <uibase/ipluginpreview.h>

class PreviewBase : public MOBase::IPluginPreview
{

  Q_OBJECT
  Q_INTERFACES(MOBase::IPlugin MOBase::IPluginPreview)
  Q_PLUGIN_METADATA(IID "org.tannin.PreviewBase")

public:
  PreviewBase();

public:
  virtual bool init(MOBase::IOrganizer* moInfo) override;
  virtual QString name() const override;
  virtual QString localizedName() const override;
  virtual QString author() const override;
  virtual QString description() const override;
  virtual MOBase::VersionInfo version() const override;
  virtual QList<MOBase::PluginSetting> settings() const override;

public:
  virtual std::set<QString> supportedExtensions() const;
  virtual bool supportsArchives() const override { return true; }
  virtual QWidget* genFilePreview(const QString& fileName, const QSize& maxSize) const;
  virtual QWidget* genDataPreview(const QByteArray& fileData, const QString& fileName,
                                  const QSize& maxSize) const override;

private:
  QWidget* genImagePreview(const QString& fileName, const QSize& maxSize,
                           const QByteArray& fileData) const;
  QWidget* genTxtPreview(const QString& fileName, const QSize& maxSize,
                         const QByteArray& fileData) const;

private:
  std::map<QString,
           std::function<QWidget*(const QString&, const QSize&, const QByteArray&)>>
      m_PreviewGenerators;

private:
  const MOBase::IOrganizer* m_MOInfo;
};

#endif  // PREVIEWBASE_H
