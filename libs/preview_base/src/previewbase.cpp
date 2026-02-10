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

#include "previewbase.h"

#include <QApplication>
#include <QFileInfo>
#include <QImageReader>
#include <QLabel>
#include <QScreen>
#include <QTextEdit>
#include <QtPlugin>

#include <uibase/utility.h>

using namespace MOBase;

PreviewBase::PreviewBase() : m_MOInfo(nullptr) {}

bool PreviewBase::init(IOrganizer* moInfo)
{
  m_MOInfo = moInfo;

  const QStringList& blacklist =
      m_MOInfo->pluginSetting(name(), "blacklisted_extensions")
          .toString()
          .toLower()
          .split(',');

  // set up image reader to be used for all image types qt (the current installation)
  // supports
  auto imageReader =
      std::bind(&PreviewBase::genImagePreview, this, std::placeholders::_1,
                std::placeholders::_2, std::placeholders::_3);

  foreach (const QByteArray& fileType, QImageReader::supportedImageFormats()) {
    auto strFileType = QString(fileType).toLower();

    // skip dds as that one is handled by the dds preview plugin.
    if (strFileType == "dds" || blacklist.contains(strFileType))
      continue;

    m_PreviewGenerators[strFileType] = imageReader;
  }

  const QStringList supportedTextFormats = {"txt", "ini", "json", "log", "cfg", "psc"};

  auto textReader = std::bind(&PreviewBase::genTxtPreview, this, std::placeholders::_1,
                              std::placeholders::_2, std::placeholders::_3);

  foreach (const QString fileType, supportedTextFormats) {

    // skip blacklisted ones
    if (blacklist.contains(fileType))
      continue;

    m_PreviewGenerators[fileType] = textReader;
  }

  return true;
}

QString PreviewBase::name() const
{
  return "Preview Base";
}

QString PreviewBase::localizedName() const
{
  return tr("Preview Base");
}

QString PreviewBase::author() const
{
  return "Tannin";
}

QString PreviewBase::description() const
{
  return tr("Supports previewing various types of data files");
}

MOBase::VersionInfo PreviewBase::version() const
{
  return VersionInfo(1, 1, 0, VersionInfo::RELEASE_FINAL);
}

QList<MOBase::PluginSetting> PreviewBase::settings() const
{
  QList<PluginSetting> result;
  result.push_back(PluginSetting(
      "enabled",
      tr("Enable previewing of basic file types, such as images and text files."),
      QVariant(true)));
  result.push_back(
      PluginSetting("blacklisted_extensions",
                    tr("Specify a list of comma separated extensions (without \".\") "
                       "that should not be previewed by this plugin."),
                    QVariant("")));
  return result;
}

std::set<QString> PreviewBase::supportedExtensions() const
{
  std::set<QString> extensions;
  for (const auto& generator : m_PreviewGenerators) {
    extensions.insert(generator.first);
  }

  return extensions;
}

QWidget* PreviewBase::genFilePreview(const QString& fileName,
                                     const QSize& maxSize) const
{
  return genDataPreview(nullptr, fileName, maxSize);
}

QWidget* PreviewBase::genDataPreview(const QByteArray& fileData,
                                     const QString& fileName,
                                     const QSize& maxSize) const
{
  auto iter = m_PreviewGenerators.find(QFileInfo(fileName).suffix().toLower());
  if (iter != m_PreviewGenerators.end()) {
    return iter->second(fileName, maxSize, fileData);
  } else {
    return nullptr;
  }
}

QWidget* PreviewBase::genImagePreview(const QString& fileName, const QSize&,
                                      const QByteArray& fileData) const
{
  QLabel* label = new QLabel();
  QPixmap pic;
  if (fileData == nullptr) {
    pic = QPixmap(fileName);
  } else {
    pic.loadFromData(fileData);
  }
  QSize screenSize = QApplication::primaryScreen()->geometry().size();
  // ensure the output image is no more than 80% of the screen height.
  // If the aspect ratio is higher than that of the screen this would still allow the
  // image to extend beyond the screen but it ensures you can drag the window and close
  // it
  int maxHeight = static_cast<int>(screenSize.height() * 0.8f);
  if (pic.size().height() > maxHeight) {
    pic = pic.scaledToHeight(maxHeight, Qt::SmoothTransformation);
  }
  label->setPixmap(pic);
  return label;
}

QWidget* PreviewBase::genTxtPreview(const QString& fileName, const QSize&,
                                    const QByteArray& fileData) const
{
  QTextEdit* edit = new QTextEdit();
  if (fileData == nullptr) {
    edit->setText(MOBase::readFileText(fileName));
  } else {
    edit->setText(MOBase::decodeTextData(fileData));
  }
  edit->setReadOnly(true);
  return edit;
}

#if QT_VERSION < QT_VERSION_CHECK(5, 0, 0)
Q_EXPORT_PLUGIN2(previewBase, PreviewBase)
#endif
