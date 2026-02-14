/*
Copyright (C) 2012 Sebastian Herbord. All rights reserved.

This file is part of Mod Organizer.

Mod Organizer is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Mod Organizer is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Mod Organizer.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "fomodinstallerdialog.h"
#include "ui_fomodinstallerdialog.h"

#include <array>
#include <sstream>
#include <utility>
#include <vector>

#include <QCheckBox>
#include <QCompleter>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QDesktopServices>
#include <QRadioButton>
#include <QScrollArea>
#include <QUrl>
#include <QStringEncoder>

#ifdef _WIN32
#include <Shellapi.h>
#endif

#include <uibase/game_features/igamefeatures.h>
#include <uibase/game_features/scriptextender.h>
#include <uibase/imoinfo.h>
#include <uibase/iplugingame.h>
#include <uibase/log.h>
#include <uibase/report.h>
#include <uibase/scopeguard.h>
#include <uibase/utility.h>

#include "fomodscreenshotdialog.h"
#include "xmlreader.h"

using namespace MOBase;

bool ControlsAscending(QAbstractButton* LHS, QAbstractButton* RHS)
{
  return LHS->text() < RHS->text();
}

bool ControlsDescending(QAbstractButton* LHS, QAbstractButton* RHS)
{
  return LHS->text() > RHS->text();
}

bool PagesAscending(QGroupBox* LHS, QGroupBox* RHS)
{
  return LHS->title() < RHS->title();
}

bool PagesDescending(QGroupBox* LHS, QGroupBox* RHS)
{
  return LHS->title() > RHS->title();
}

FomodInstallerDialog::FomodInstallerDialog(
    InstallerFomod* installer, const GuessedValue<QString>& modName,
    const QString& fomodPath, const QString& fomodDirName,
    const std::function<MOBase::IPluginList::PluginStates(const QString&)>& fileCheck,
    QWidget* parent)
    : QDialog(parent), ui(new Ui::FomodInstallerDialog), m_Installer(installer),
      m_ModName(modName), m_ModID(-1), m_FomodPath(fomodPath),
      m_FomodDirName(fomodDirName), m_Manual(false), m_FileCheck(fileCheck),
      m_FileSystemItemSequence()
{
  ui->setupUi(this);
  setWindowFlags(windowFlags() | Qt::WindowMaximizeButtonHint);
  setWindowTitle(modName);

  updateNameEdit();
  ui->nameCombo->completer()->setCaseSensitivity(Qt::CaseSensitive);
}

FomodInstallerDialog::~FomodInstallerDialog()
{
  delete ui;
}

bool FomodInstallerDialog::hasOptions()
{
  return ui->stepsStack->count() > 0;
}

void FomodInstallerDialog::transformToSmallInstall()
{
  ui->descriptionText->setVisible(false);
  ui->screenshotLabel->setVisible(false);
  ui->stepsStack->setVisible(false);
  adjustSize();
}

void FomodInstallerDialog::updateNameEdit()
{
  ui->nameCombo->clear();
  for (auto iter = m_ModName.variants().begin(); iter != m_ModName.variants().end();
       ++iter) {
    ui->nameCombo->addItem(*iter);
  }

  ui->nameCombo->setCurrentIndex(ui->nameCombo->findText(m_ModName));
}

int FomodInstallerDialog::bomOffset(const QByteArray& buffer)
{
  static const unsigned char BOM_UTF8[]    = {0xEF, 0xBB, 0xBF};
  static const unsigned char BOM_UTF16BE[] = {0xFE, 0xFF};
  static const unsigned char BOM_UTF16LE[] = {0xFF, 0xFE};
  auto startsWithBytes = [&buffer](const unsigned char* bytes, qsizetype size) {
    return buffer.startsWith(
        QByteArrayView(reinterpret_cast<const char*>(bytes), size));
  };

  if (startsWithBytes(BOM_UTF8, sizeof(BOM_UTF8)))
    return 3;
  if (startsWithBytes(BOM_UTF16BE, sizeof(BOM_UTF16BE)) ||
      startsWithBytes(BOM_UTF16LE, sizeof(BOM_UTF16LE)))
    return 2;

  return 0;
}

struct XmlParseError : std::runtime_error
{
  XmlParseError(const QString& message) : std::runtime_error(qUtf8Printable(message)) {}
};

QByteArray skipXmlHeader(QIODevice& file)
{
  static const unsigned char UTF16LE_BOM[] = {0xFF, 0xFE};
  static const unsigned char UTF16BE_BOM[] = {0xFE, 0xFF};
  static const unsigned char UTF8_BOM[]    = {0xEF, 0xBB, 0xBF};
  static const unsigned char UTF16LE[]     = {0x3C, 0x00, 0x3F, 0x00};
  static const unsigned char UTF16BE[]     = {0x00, 0x3C, 0x00, 0x3F};
  static const unsigned char UTF8[]        = {0x3C, 0x3F, 0x78, 0x6D};
  auto startsWithBytes = [](const QByteArray& bytes, const unsigned char* prefix,
                            qsizetype size) {
    return bytes.startsWith(
        QByteArrayView(reinterpret_cast<const char*>(prefix), size));
  };

  file.seek(0);
  QByteArray rawBytes = file.read(4);
  QTextStream stream(&file);
  int bom = 0;
  if (startsWithBytes(rawBytes, UTF16LE_BOM, sizeof(UTF16LE_BOM))) {
    stream.setEncoding(QStringConverter::Encoding::Utf16LE);
    bom = 2;
  } else if (startsWithBytes(rawBytes, UTF16BE_BOM, sizeof(UTF16BE_BOM))) {
    stream.setEncoding(QStringConverter::Encoding::Utf16BE);
    bom = 2;
  } else if (startsWithBytes(rawBytes, UTF8_BOM, sizeof(UTF8_BOM))) {
    stream.setEncoding(QStringConverter::Encoding::Utf8);
    bom = 3;
  } else if (startsWithBytes(rawBytes, UTF16LE, sizeof(UTF16LE))) {
    stream.setEncoding(QStringConverter::Encoding::Utf16LE);
  } else if (startsWithBytes(rawBytes, UTF16BE, sizeof(UTF16BE))) {
    stream.setEncoding(QStringConverter::Encoding::Utf16BE);
  } else if (startsWithBytes(rawBytes, UTF8, sizeof(UTF8))) {
    stream.setEncoding(QStringConverter::Encoding::Utf8);
  }  // otherwise maybe the textstream knows the encoding?

  stream.seek(bom);
  QString header = stream.readLine();
  if (!header.startsWith("<?")) {
    // it was all for nothing, there is no header here...
    stream.seek(bom);
  }
  // this seems to be necessary due to buffering in QTextStream
  file.seek(stream.pos());
  return file.readAll();
}

void FomodInstallerDialog::readXml(QFile& file,
                                   void (FomodInstallerDialog::*callback)(XmlReader&))
{
  // List of encodings to try:
  static const std::vector<QStringConverter::Encoding> encodings{
      QStringConverter::Encoding::Utf16, QStringConverter::Encoding::Utf8,
      QStringConverter::Encoding::Latin1};

  bool success = false;
  std::string errorMessage;
  try {
    XmlReader reader(&file);
    (this->*callback)(reader);
    success = true;
  } catch (const XmlParseError& e) {
    log::warn("The {} in this file is incorrectly encoded ({}). Applying heuristics...",
              file.fileName(), e.what());
  }

  if (!success) {
    // nmm's xml parser is less strict than the one from qt and allows files with
    // wrong encoding in the header. Being strict here would be bad user experience
    // this works around bad headers.
    QByteArray headerlessData = skipXmlHeader(file);

    // try parsing the file with several encodings to support broken files
    for (auto encoding : encodings) {
      log::debug("Trying encoding {} for {}... ", encoding, file.fileName());
      try {
        QStringEncoder encoder(encoding);
        XmlReader reader(
            encoder.encode(QString("<?xml version=\"1.0\" encoding=\"%1\" ?>")
                               .arg(encoder.name())) +
            headerlessData);
        (this->*callback)(reader);
        log::debug("Interpreting {} as {}.", file.fileName(), encoding);
        success = true;
        break;
      } catch (const XmlParseError& e) {
        log::debug("Not {}: {}.", encoding, e.what());
      }
    }
    if (!success) {
      reportError(
          tr("Failed to parse %1. See console for details.").arg(file.fileName()));
    }

    file.close();
  }
}

void FomodInstallerDialog::readInfoXml()
{
  QFile file(QDir::tempPath() + "/" + m_FomodPath + "/" + m_FomodDirName + "/info.xml");

  // We don't need a info.xml file, so we just return if we cannot open it:
  if (!file.open(QIODevice::ReadOnly)) {
    return;
  }
  readXml(file, &FomodInstallerDialog::parseInfo);
}

void FomodInstallerDialog::readModuleConfigXml()
{
  QFile file(QDir::tempPath() + "/" + m_FomodPath + "/" + m_FomodDirName + "/ModuleConfig.xml");
  if (!file.open(QIODevice::ReadOnly)) {
    throw Exception(tr("%1 missing.").arg(file.fileName()));
  }
  readXml(file, &FomodInstallerDialog::parseModuleConfig);
}

void FomodInstallerDialog::initData(IOrganizer* moInfo)
{
  m_MoInfo = moInfo;

  // parse provided package information
  readInfoXml();

  QString screenshotPath =
      QDir::tempPath() + "/" + m_FomodPath + "/" + m_FomodDirName + "/screenshot.png";
  if (!QImage(screenshotPath).isNull()) {
    ui->screenshotLabel->setScalableResource(screenshotPath);
    ui->screenshotExpand->setVisible(false);
  }

  readModuleConfigXml();
}

QString FomodInstallerDialog::getName() const
{
  return ui->nameCombo->currentText();
}

QString FomodInstallerDialog::getVersion() const
{
  return ui->versionLabel->text();
}

int FomodInstallerDialog::getModID() const
{
  return m_ModID;
}

QString FomodInstallerDialog::getURL() const
{
  return m_URL;
}

void FomodInstallerDialog::applyPriority(Leaves& leaves, IFileTree const* tree,
                                         int priority)
{
  for (auto entry : *tree) {
    if (entry->isDir()) {
      applyPriority(leaves, entry->astree().get(), priority);
    } else {
      leaves.insert({entry.get(), {priority, entry->path()}});
    }
  }
}

void FomodInstallerDialog::copyLeaf(std::shared_ptr<FileTreeEntry> sourceEntry,
                                    std::shared_ptr<IFileTree> destinationTree,
                                    QString destinationPath,
                                    IFileTree::OverwritesType& overwrites,
                                    Leaves& leaves, int pri)
{
  // TODO:
  applyPriority(leaves, sourceEntry->parent().get(), pri);

  if (destinationPath.isEmpty() || destinationPath.endsWith("/") ||
      destinationPath.endsWith("\\")) {
    destinationPath += sourceEntry->name();
  }

  auto oldEntry = destinationTree->find(destinationPath);
  if (oldEntry != nullptr) {
    overwrites[oldEntry] = sourceEntry;
  }

  destinationTree->copy(sourceEntry, destinationPath, IFileTree::InsertPolicy::REPLACE);
}

bool FomodInstallerDialog::copyFileIterator(std::shared_ptr<IFileTree> sourceTree,
                                            std::shared_ptr<IFileTree> destinationTree,
                                            const FileDescriptor* descriptor,
                                            Leaves& leaves,
                                            IFileTree::OverwritesType& overwrites)
{
  QString source      = (m_FomodPath.length() != 0)
                            ? QDir(m_FomodPath).filePath(descriptor->m_Source)
                            : descriptor->m_Source;
  int pri             = descriptor->m_Priority;
  QString destination = descriptor->m_Destination;

  if (descriptor->m_IsFolder) {
    std::shared_ptr<IFileTree> sourceNode = sourceTree->findDirectory(source);

    if (sourceNode == nullptr) {
      log::error("Folder '{}' not found.", source);
      return false;
    }

    // Apply the priority on the source tree:
    applyPriority(leaves, sourceNode.get(), pri);

    // addDirectory will create the directory if it does not exist:
    std::shared_ptr<IFileTree> targetNode = destinationTree->addDirectory(destination);

    // Note (Holt59): Before, the directories were processed before the files, and the
    // files were processed in reverse order. The directories before files was mandatory
    // since both were stored differently, but I have no idea why the files were
    // processed in reverse order and it does not make sense since there cannot be two
    // identical file in a tree. Also, the files were copied but the directories were
    // moved, I am pretty sure this made no sense.
    for (auto e : *sourceNode) {
      targetNode->copy(e, "", IFileTree::InsertPolicy::MERGE);
    }

  } else {
    std::shared_ptr<FileTreeEntry> sourceEntry = sourceTree->find(source);

    if (sourceEntry == nullptr) {
      log::error("File '{}' not found.", source);
      return false;
    }

    copyLeaf(sourceEntry, destinationTree, destination, overwrites, leaves, pri);
  }
  return true;
}

std::pair<bool, QString>
FomodInstallerDialog::testCondition(int maxIndex,
                                    const ValueCondition* valCondition) const
{
  return testCondition(maxIndex, valCondition->m_Name, valCondition->m_Value);
}

std::pair<bool, QString>
FomodInstallerDialog::testCondition(int maxIndex,
                                    const ConditionFlag* conditionFlag) const
{
  return testCondition(maxIndex, conditionFlag->m_Name, conditionFlag->m_Value);
}

std::pair<bool, QString>
FomodInstallerDialog::testCondition(int maxIndex, const SubCondition* condition) const
{
  ConditionOperator op = condition->m_Operator;
  for (const Condition* cond : condition->m_Conditions) {
    std::pair<bool, QString> conditionMatches = cond->test(maxIndex, this);
    if (!conditionMatches.first)
      qWarning() << conditionMatches.second;
    if (op == OP_OR && conditionMatches.first) {
      return std::make_pair<bool, QString>(
          true, tr("At least one condition was successful in an 'OR' clause!"));
    }
    if (op == OP_AND && !conditionMatches.first) {
      return conditionMatches;
    }
  }
  // If we get through here, everything matched (AND) or nothing matched (OR)
  if (op == OP_AND)
    return std::make_pair<bool, QString>(
        true, tr("All conditions were successful in an 'AND' clause!"));
  else
    return std::make_pair<bool, QString>(
        false, tr("No conditions were successful in an 'OR' clause!"));
}

QString FomodInstallerDialog::toString(IPluginList::PluginStates state)
{
  if (state.testFlag(IPluginList::STATE_MISSING))
    return "Missing";
  if (state.testFlag(IPluginList::STATE_INACTIVE))
    return "Inactive";
  if (state.testFlag(IPluginList::STATE_ACTIVE))
    return "Active";
  throw Exception(tr("invalid plugin state %1").arg(static_cast<int>(state)));
}

std::pair<bool, QString>
FomodInstallerDialog::testCondition(int, const FileCondition* condition) const
{
  static const std::map<QString, QString> trPluginStates = {
      {"Missing", tr("Missing")},
      {"Inactive", tr("Inactive")},
      {"Active", tr("Active")}};

  QString result = toString(m_FileCheck(condition->m_File));
  if (result == condition->m_State)
    return std::make_pair<bool, QString>(
        true, tr("Success: The file '%1' was marked %2.")
                  .arg(condition->m_File)
                  .arg(trPluginStates.at(condition->m_State).toLower()));
  else
    return std::make_pair<bool, QString>(
        false, tr("Missing requirement: The file '%1' should be %2, but was %3!")
                   .arg(condition->m_File)
                   .arg(trPluginStates.at(condition->m_State).toLower())
                   .arg(trPluginStates.at(result).toLower()));
}

namespace FOMOD
{
class Version
{
public:
  explicit Version(QString const& v);

  friend bool operator<=(Version const&, Version const&);

private:
  std::array<int, 4> m_version;
};

Version::Version(QString const& v)
{
  std::istringstream parser(v.toStdString());
  m_version.fill(0);
  parser >> m_version[0];
  for (int idx = 1; idx < 4; idx++) {
    parser.get();  // Skip period
    parser >> m_version[idx];
  }
}

bool operator<=(Version const& lhs, Version const& rhs)
{
  return lhs.m_version <= rhs.m_version;
}

}  // namespace FOMOD

std::pair<bool, QString>
FomodInstallerDialog::testCondition(int, const VersionCondition* condition) const
{
  QString version;
  MOBase::IPluginGame const* game = m_MoInfo->managedGame();

  QString typeName;
  switch (condition->m_Type) {
  case VersionCondition::v_Game: {
    version  = game->gameVersion();
    typeName = game->gameName();
  } break;

  case VersionCondition::v_FOMM:
    // We should use m_MoInfo->appVersion() but then we wouldn't be able to
    // install anything as MO is at 0.3.11 at the time of writing.
    version  = "0.13.21";
    typeName = "FOMM (FOMOD syntax)";
    break;

  case VersionCondition::v_FOSE: {
    auto extender = m_MoInfo->gameFeatures()->gameFeature<ScriptExtender>();
    if (extender != nullptr) {
      version  = extender->getExtenderVersion();
      typeName = extender->BinaryName();
    } else {
      version  = "not installed";
      typeName = "the script extender";
    }
  } break;
  }
  if (FOMOD::Version(condition->m_RequiredVersion) <= FOMOD::Version(version))
    return std::make_pair<bool, QString>(
        true, tr("Success: The required version of %1 is %2, and was detected as %3.")
                  .arg(typeName)
                  .arg(condition->m_RequiredVersion)
                  .arg(version));
  else
    return std::make_pair<bool, QString>(false,
                                         tr("Missing requirement: The required version "
                                            "of %1 is %2, but was detected as %3.")
                                             .arg(typeName)
                                             .arg(condition->m_RequiredVersion)
                                             .arg(version));
}

bool FomodInstallerDialog::displayMissingFilesDialog(
    std::vector<const FileDescriptor*> missingFiles)
{
  QMessageBox dialog(parentWidget());

  dialog.setIcon(QMessageBox::Icon::Warning);
  dialog.setWindowTitle(tr("Missing files or folders"));
  dialog.addButton(tr("Install anyway"), QMessageBox::AcceptRole);
  dialog.addButton(tr("Cancel"), QMessageBox::RejectRole);

  QString text = tr("The following files or folders  were not found in the archive. "
                    "This is likely due to an incorrect FOMOD installer. "
                    "This mod may not work properly.");
  text.append("\n\n");
  for (auto* fileDescriptor : missingFiles) {
    QString temp = fileDescriptor->m_IsFolder ? tr("Folder '%1'.") : tr("File '%1'.");
    text.append("- " + temp.arg(fileDescriptor->m_Source) + "\n");
  }

  dialog.setTextFormat(Qt::MarkdownText);
  dialog.setText(text);

  return dialog.exec() == QMessageBox::AcceptRole;
}

IPluginInstaller::EInstallResult
FomodInstallerDialog::updateTree(std::shared_ptr<IFileTree>& tree)
{
  FileDescriptorList descriptorList;

  // enable all required files
  for (FileDescriptor* file : m_RequiredFiles) {
    descriptorList.push_back(file);
  }

  // enable all conditional file installs (files programatically selected by conditions
  // instead of a user selection. usually dependencies)
  for (ConditionalInstall& cond : m_ConditionalInstalls) {
    SubCondition* condition         = &cond.m_Condition;
    std::pair<bool, QString> result = condition->test(ui->stepsStack->count(), this);
    if (result.first) {
      for (FileDescriptor* file : cond.m_Files) {
        descriptorList.push_back(file);
      }
    }
  }

  // enable all user-enabled choices
  for (int i = 0; i < ui->stepsStack->count(); ++i) {
    if (testVisible(i)) {
      QList<QAbstractButton*> choices =
          ui->stepsStack->widget(i)->findChildren<QAbstractButton*>("choice");
      for (QAbstractButton* choice : choices) {
        if (choice->isChecked()) {
          QVariantList fileList = choice->property("files").toList();
          for (QVariant fileVariant : fileList) {
            descriptorList.push_back(fileVariant.value<FileDescriptor*>());
          }
        }
      }
    }
  }

  std::stable_sort(descriptorList.begin(), descriptorList.end(), byPriority);

  IFileTree::OverwritesType overwrites;
  Leaves leaves;
  std::shared_ptr<IFileTree> newTree = tree->createOrphanTree();

  std::vector<const FileDescriptor*> failures;

  const QStringList ignoreMissingFolder =
      m_MoInfo
          ->persistent(m_Installer->name(), "ignored_missing_files",
                       QStringList{"no folder"})
          .toStringList();

  for (const FileDescriptor* file : descriptorList) {
    if (!copyFileIterator(tree, newTree, file, leaves, overwrites)) {
      if (!ignoreMissingFolder.contains(file->m_Source,
                                        FileNameComparator::CaseSensitivity)) {
        failures.push_back(file);
      }
    }
  }

  if (!failures.empty()) {
    if (!displayMissingFilesDialog(failures)) {
      return IPluginInstaller::RESULT_CANCELED;
    }
  }

  for (auto overwrite : overwrites) {
    if (leaves[overwrite.first.get()].priority ==
        leaves[overwrite.second.get()].priority) {
      qWarning() << "Overriding " << leaves[overwrite.first.get()].path << " with "
                 << leaves[overwrite.second.get()].path
                 << " which has the same priority";
    }
  }

  // Update the tree:
  tree = newTree;

  return IPluginInstaller::RESULT_SUCCESS;
}

void FomodInstallerDialog::highlightControl(QAbstractButton* button)
{
  QVariant screenshotName = button->property("screenshot");
  if (screenshotName.isValid()) {
    QString screenshotFileName = screenshotName.toString();
    if (!screenshotFileName.isEmpty()) {
      QString temp = QDir::tempPath() + "/" + m_FomodPath + "/" +
                     QDir::fromNativeSeparators(screenshotFileName);
      ui->screenshotLabel->setScalableResource(temp);
      ui->screenshotExpand->setVisible(true);
    } else {
      ui->screenshotLabel->setScalableResource(QString());
      ui->screenshotExpand->setVisible(false);
    }
  }
  ui->descriptionText->setText(button->property("description").toString());
}

bool FomodInstallerDialog::eventFilter(QObject* object, QEvent* event)
{
  QAbstractButton* button = qobject_cast<QAbstractButton*>(object);
  if ((button != nullptr) && (event->type() == QEvent::HoverEnter)) {
    highlightControl(button);
  }
  return QDialog::eventFilter(object, event);
}

QString FomodInstallerDialog::readContent(QXmlStreamReader& reader)
{
  if (reader.readNext() == XmlReader::Characters) {
    return reader.text().toString();
  } else {
    return QString();
  }
}

void FomodInstallerDialog::parseInfo(XmlReader& reader)
{
  while (!reader.atEnd()) {
    switch (reader.readNext()) {
    case QXmlStreamReader::StartElement: {
      if (reader.name().toString() == "Name") {
        m_ModName.update(readContent(reader), GUESS_META);
        updateNameEdit();
      } else if (reader.name().toString() == "Author") {
        ui->authorLabel->setText(readContent(reader));
      } else if (reader.name().toString() == "Version") {
        ui->versionLabel->setText(readContent(reader));
      } else if (reader.name().toString() == "Id") {
        m_ModID = readContent(reader).toInt();
      } else if (reader.name().toString() == "Website") {
        m_URL = readContent(reader);
        ui->websiteLabel->setText(tr("<a href=\"%1\">Link</a>").arg(m_URL));
        ui->websiteLabel->setToolTip(m_URL);
      }
    } break;
    default: {
    } break;
    }
  }
  if (reader.hasError()) {
    throw XmlParseError(
        QString("%1 in line %2").arg(reader.errorString()).arg(reader.lineNumber()));
  }
}

FomodInstallerDialog::ItemOrder
FomodInstallerDialog::getItemOrder(const QString& orderString)
{
  if (orderString == "Ascending") {
    return ORDER_ASCENDING;
  } else if (orderString == "Descending") {
    return ORDER_DESCENDING;
  } else if (orderString == "Explicit") {
    return ORDER_EXPLICIT;
  } else {
    throw Exception(tr("unsupported order type %1").arg(orderString));
  }
}

FomodInstallerDialog::GroupType
FomodInstallerDialog::getGroupType(const QString& typeString)
{
  if (typeString == "SelectAtLeastOne") {
    return TYPE_SELECTATLEASTONE;
  } else if (typeString == "SelectAtMostOne") {
    return TYPE_SELECTATMOSTONE;
  } else if (typeString == "SelectExactlyOne") {
    return TYPE_SELECTEXACTLYONE;
  } else if (typeString == "SelectAny") {
    return TYPE_SELECTANY;
  } else if (typeString == "SelectAll") {
    return TYPE_SELECTALL;
  } else {
    throw Exception(tr("unsupported group type %1").arg(typeString));
  }
}

FomodInstallerDialog::PluginType
FomodInstallerDialog::getPluginType(const QString& typeString)
{
  if (typeString == "Required") {
    return FomodInstallerDialog::TYPE_REQUIRED;
  } else if (typeString == "Optional") {
    return FomodInstallerDialog::TYPE_OPTIONAL;
  } else if (typeString == "Recommended") {
    return FomodInstallerDialog::TYPE_RECOMMENDED;
  } else if (typeString == "NotUsable") {
    return FomodInstallerDialog::TYPE_NOTUSABLE;
  } else if (typeString == "CouldBeUsable") {
    return FomodInstallerDialog::TYPE_COULDBEUSABLE;
  } else {
    qCritical("invalid plugin type %s", qUtf8Printable(typeString));
    return FomodInstallerDialog::TYPE_OPTIONAL;
  }
}

void FomodInstallerDialog::readFileList(XmlReader& reader, FileDescriptorList& fileList)
{
  QString const self(reader.name().toString());
  while (reader.getNextElement(self)) {
    if (reader.name().toString() == "folder" || reader.name().toString() == "file") {
      QXmlStreamAttributes attributes = reader.attributes();
      // This is a horrendous hack. It doesn't make sense to specify an empty source
      // folder name, as it would require you to copy everything including the fomod
      // directory. However, people have been known to write entries like <folder
      // source="" destination=""/> in order to achieve an option that does nothing. Are
      // groups and buttons that hard? An empty source file is very probably a serious
      // error but given people do the above, I'm assuming that they probably assume
      // <file source="" destination=""/> will work the same, so I'm not
      // differentiating. Similarly, I'm not checking for the destination if the source
      // is blank. Why'd you want to copy the fomod directory on an install?
      if (attributes.value("source").isEmpty()) {
        log::debug("Ignoring {} entry with empty source.", reader.name().toString());
      } else {
        FileDescriptor* file           = new FileDescriptor(this);
        file->m_Source                 = attributes.value("source").toString();
        file->m_Destination            = attributes.hasAttribute("destination")
                                             ? attributes.value("destination").toString()
                                             : file->m_Source;
        file->m_Priority               = attributes.hasAttribute("priority")
                                             ? attributes.value("priority").toString().toInt()
                                             : 0;
        file->m_FileSystemItemSequence = ++m_FileSystemItemSequence;
        file->m_IsFolder               = reader.name().toString() == "folder";
        file->m_InstallIfUsable =
            attributes.hasAttribute("installIfUsable")
                ? (attributes.value("installIfUsable").compare((QString) "true") == 0)
                : false;
        file->m_AlwaysInstall =
            attributes.hasAttribute("alwaysInstall")
                ? (attributes.value("alwaysInstall").compare((QString) "true") == 0)
                : false;

        fileList.push_back(file);
      }
      reader.finishedElement();
    } else {
      reader.unexpected();
    }
  }
}

void FomodInstallerDialog::readDependencyPattern(XmlReader& reader,
                                                 DependencyPattern& pattern)
{
  // sequence
  //   dependency
  //   type
  QString self(reader.name().toString());
  while (reader.getNextElement(self)) {
    if (reader.name().toString() == "dependencies") {
      readCompositeDependency(reader, pattern.condition);
    } else if (reader.name().toString() == "type") {
      pattern.type = getPluginType(reader.attributes().value("name").toString());
      reader.finishedElement();
    } else {
      reader.unexpected();
    }
  }
}

void FomodInstallerDialog::readDependencyPatternList(XmlReader& reader,
                                                     DependencyPatternList& patterns)
{
  QString self(reader.name().toString());
  while (reader.getNextElement(self)) {
    if (reader.name().toString() == "pattern") {
      DependencyPattern pattern;
      readDependencyPattern(reader, pattern);
      patterns.push_back(pattern);
    } else {
      reader.unexpected();
    }
  }
}

void FomodInstallerDialog::readDependencyPluginType(XmlReader& reader,
                                                    PluginTypeInfo& info)
{
  // sequence
  //  defaultType
  //  patterns
  QString const self(reader.name().toString());
  while (reader.getNextElement(self)) {
    if (reader.name().toString() == "defaultType") {
      info.m_DefaultType = getPluginType(reader.attributes().value("name").toString());
      reader.finishedElement();
    } else if (reader.name().toString() == "patterns") {
      readDependencyPatternList(reader, info.m_DependencyPatterns);
    } else {
      reader.unexpected();
    }
  }
}

void FomodInstallerDialog::readPluginType(XmlReader& reader, Plugin& plugin)
{
  // Have a choice here of precisely one of 'type' or 'dependencytype', so this is
  // not strictly necessary
  plugin.m_PluginTypeInfo.m_DefaultType = TYPE_OPTIONAL;
  QString const self(reader.name().toString());
  while (reader.getNextElement(self)) {
    if (reader.name().toString() == "type") {
      plugin.m_PluginTypeInfo.m_DefaultType =
          getPluginType(reader.attributes().value("name").toString());
      reader.finishedElement();
    } else if (reader.name().toString() == "dependencyType") {
      readDependencyPluginType(reader, plugin.m_PluginTypeInfo);
    } else {
      reader.unexpected();
    }
  }
}

void FomodInstallerDialog::readConditionFlagList(XmlReader& reader,
                                                 ConditionFlagList& condflags)
{
  QString const self(reader.name().toString());
  while (reader.getNextElement(self)) {
    if (reader.name().toString() == "flag") {
      QString name    = reader.attributes().value("name").toString();
      QString content = reader.getText().trimmed();
      condflags.push_back(ConditionFlag(name, content));
    } else {
      reader.unexpected();
    }
  }
}

bool FomodInstallerDialog::byPriority(const FileDescriptor* LHS,
                                      const FileDescriptor* RHS)
{
  return LHS->m_Priority == RHS->m_Priority
             ? LHS->m_FileSystemItemSequence < RHS->m_FileSystemItemSequence
             : LHS->m_Priority < RHS->m_Priority;
}

FomodInstallerDialog::Plugin FomodInstallerDialog::readPlugin(XmlReader& reader)
{
  Plugin result;
  result.m_Name = reader.attributes().value("name").toString();

  QString const self(reader.name().toString());
  while (reader.getNextElement(self)) {
    if (reader.name().toString() == "description") {
      result.m_Description = reader.getText().trimmed();
    } else if (reader.name().toString() == "image") {
      result.m_ImagePath = reader.attributes().value("path").toString();
      reader.finishedElement();
    } else if (reader.name().toString() == "files") {
      readFileList(reader, result.m_Files);
    } else if (reader.name().toString() == "conditionFlags") {
      readConditionFlagList(reader, result.m_ConditionFlags);
    } else if (reader.name().toString() == "typeDescriptor") {
      readPluginType(reader, result);
    } else {
      reader.unexpected();
    }
  }

  // I (TRT) am not quite sure why this sort is done here. It is done again
  // when the files have been selected before installing them, which seems
  // a more appropriate place.
  std::sort(result.m_Files.begin(), result.m_Files.end(), byPriority);

  return result;
}

FomodInstallerDialog::PluginType
FomodInstallerDialog::getPluginDependencyType(int page,
                                              const PluginTypeInfo& info) const
{
  if (info.m_DependencyPatterns.size() != 0) {
    for (const DependencyPattern& pattern : info.m_DependencyPatterns) {
      if (testCondition(page, &pattern.condition).first) {
        return pattern.type;
      }
    }
  }
  return info.m_DefaultType;
}

void FomodInstallerDialog::readPluginList(XmlReader& reader, QString const& groupName,
                                          GroupType& groupType, QLayout* layout)
{
  ItemOrder pluginOrder =
      reader.attributes().hasAttribute("order")
          ? getItemOrder(reader.attributes().value("order").toString())
          : ORDER_ASCENDING;

  // Read in all the plugins so we can check if the author is using "atmost" or
  // "exactly", and correct as appropriate
  std::vector<Plugin> plugins;
  QString const self(reader.name().toString());
  while (reader.getNextElement(self)) {
    if (reader.name().toString() == "plugin") {
      plugins.push_back(readPlugin(reader));
    } else {
      reader.unexpected();
    }
  }

  std::vector<QAbstractButton*> controls;
  // This is somewhat of a hack. If the author has specified only 1 plugin and the
  // group type is SELECTATLEASTONE or SELECTEXACTLYONE, then that plugin has to
  // be selected. A note: This doesn't check for if somebody has defined a single
  // plugin group with one of the above types, and then made the plugin unselectable.
  // They deserve what they get.
  // Similarly, if they've specfied SELECTATMOSTONE, we might as well give them
  // a checkbox
  if (plugins.size() == 1) {
    switch (groupType) {
    case TYPE_SELECTATLEASTONE: {
      qWarning() << "Plugin " << plugins[0].m_Name
                 << " is the only plugin specified in group " << groupName
                 << " which requires selection of at least one plugin";
      groupType = TYPE_SELECTALL;
    } break;
    case TYPE_SELECTEXACTLYONE: {
      qWarning() << "Plugin " << plugins[0].m_Name
                 << " is the only plugin specified in group " << groupName
                 << " which requires selection of exactly one plugin";
      groupType = TYPE_SELECTALL;
    } break;
    case TYPE_SELECTATMOSTONE: {
      qWarning() << "Plugin " << plugins[0].m_Name
                 << " is the only plugin specified in group " << groupName
                 << " which permits selection of at most one plugin";
      groupType = TYPE_SELECTANY;
    } break;
    }
  }

  for (Plugin const& plugin : plugins) {
    QAbstractButton* newControl = nullptr;
    switch (groupType) {
    case TYPE_SELECTATLEASTONE:
    case TYPE_SELECTANY: {
      newControl = new QCheckBox(plugin.m_Name);
    } break;
    case TYPE_SELECTATMOSTONE:
    case TYPE_SELECTEXACTLYONE: {
      newControl = new QRadioButton(plugin.m_Name);
    } break;
    case TYPE_SELECTALL: {
      newControl = new QCheckBox(plugin.m_Name);
      newControl->setChecked(true);
      newControl->setEnabled(false);
      newControl->setToolTip(tr("All components in this group are required"));
    } break;
    }
    newControl->setObjectName("choice");
    newControl->setAttribute(Qt::WA_Hover);
    QVariant type(QVariant::fromValue(plugin.m_PluginTypeInfo));
    newControl->setProperty("plugintypeinfo", type);
    newControl->setProperty("screenshot", plugin.m_ImagePath);
    newControl->setProperty("description", plugin.m_Description);
    QVariantList fileList;
    // This looks horrible...
    for (FileDescriptor* const& descriptor : plugin.m_Files) {
      fileList.append(QVariant::fromValue(descriptor));
    }
    newControl->setProperty("files", fileList);
    QVariantList conditionFlags;
    for (ConditionFlag const& conditionFlag : plugin.m_ConditionFlags) {
      if (!conditionFlag.m_Name.isEmpty()) {
        conditionFlags.append(QVariant::fromValue(conditionFlag));
      }
    }
    newControl->setProperty("conditionFlags", conditionFlags);
    newControl->installEventFilter(this);
    // We need somehow to check the 'toggled' signal. how do I do that
    // void QAbstractButton::clicked ( bool checked ) [signal]
    connect(newControl, SIGNAL(clicked()), this, SLOT(widgetButtonClicked()));
    controls.push_back(newControl);
  }

  if (pluginOrder == ORDER_ASCENDING) {
    std::sort(controls.begin(), controls.end(), ControlsAscending);
  } else if (pluginOrder == ORDER_DESCENDING) {
    std::sort(controls.begin(), controls.end(), ControlsDescending);
  }

  for (QAbstractButton* const control : controls) {
    layout->addWidget(control);
  }

  if (groupType == TYPE_SELECTATMOSTONE) {
    QRadioButton* newButton = new QRadioButton(tr("None"));
    newButton->setObjectName("none");
    layout->addWidget(newButton);
  }
}

void FomodInstallerDialog::readGroup(XmlReader& reader, QLayout* layout)
{
  QString name   = reader.attributes().value("name").toString();
  GroupType type = getGroupType(reader.attributes().value("type").toString());

  QGroupBox* groupBox = new QGroupBox(name);

  QVBoxLayout* groupLayout = new QVBoxLayout;

  QString const self(reader.name().toString());
  while (reader.getNextElement(self)) {
    if (reader.name().toString() == "plugins") {
      readPluginList(reader, name, type, groupLayout);
    } else {
      reader.unexpected();
    }
  }

  groupLayout->setProperty("groupType", QVariant::fromValue(type));
  groupLayout->setObjectName("grouplayout");
  groupBox->setLayout(groupLayout);
  if (type == TYPE_SELECTATLEASTONE) {
    QLabel* label = new QLabel(tr("Select one or more of these options:"));
    layout->addWidget(label);
  }

  layout->addWidget(groupBox);
}

void FomodInstallerDialog::readGroupList(XmlReader& reader, QLayout* layout)
{
  QString const self(reader.name().toString());
  while (reader.getNextElement(self)) {
    if (reader.name().toString() == "group") {
      readGroup(reader, layout);
    } else {
      reader.unexpected();
    }
  }
}

QGroupBox* FomodInstallerDialog::readInstallStep(XmlReader& reader)
{
  QString name              = reader.attributes().value("name").toString();
  QGroupBox* page           = new QGroupBox(name);
  QVBoxLayout* pageLayout   = new QVBoxLayout;
  QScrollArea* scrollArea   = new QScrollArea;
  QFrame* scrolledArea      = new QFrame;
  QVBoxLayout* scrollLayout = new QVBoxLayout;

  SubCondition subcondition;

  // sequence:
  //   visible (optional)
  //   optionalFileGroups
  QString const self(reader.name().toString());
  while (reader.getNextElement(self)) {
    if (reader.name().toString() == "visible") {
      readCompositeDependency(reader, subcondition);
    } else if (reader.name().toString() == "optionalFileGroups") {
      readGroupList(reader, scrollLayout);
    } else {
      reader.unexpected();
    }
  }

  if (subcondition.m_Conditions.size() != 0) {
    // FIXME Is this actually OK? I'm storing a pointer in the property?
    // Also AFAICS this is subject to memory leaks
    page->setProperty("conditional", QVariant::fromValue(subcondition));
  }

  scrolledArea->setLayout(scrollLayout);
  scrollArea->setWidget(scrolledArea);
  scrollArea->setWidgetResizable(true);
  pageLayout->addWidget(scrollArea);
  page->setLayout(pageLayout);
  return page;
}

void FomodInstallerDialog::readStepList(XmlReader& reader)
{
  ItemOrder stepOrder =
      reader.attributes().hasAttribute("order")
          ? getItemOrder(reader.attributes().value("order").toString())
          : ORDER_ASCENDING;

  std::vector<QGroupBox*> pages;

  // sequence installStep (1 or more)
  QString const self(reader.name().toString());
  while (reader.getNextElement(self)) {
    if (reader.name().toString() == "installStep") {
      pages.push_back(readInstallStep(reader));
    } else {
      reader.unexpected();
    }
  }

  if (stepOrder == ORDER_ASCENDING) {
    std::sort(pages.begin(), pages.end(), PagesAscending);
  } else if (stepOrder == ORDER_DESCENDING) {
    std::sort(pages.begin(), pages.end(), PagesDescending);
  }

  for (std::vector<QGroupBox*>::const_iterator iter = pages.begin();
       iter != pages.end(); ++iter) {
    ui->stepsStack->addWidget(*iter);
  }
}

void FomodInstallerDialog::readCompositeDependency(XmlReader& reader,
                                                   SubCondition& conditional)
{
  conditional.m_Operator = OP_AND;
  if (reader.attributes().hasAttribute("operator")) {
    auto opString = reader.attributes().value("operator").toString();
    if (opString == "Or") {
      conditional.m_Operator = OP_OR;
    } else if (opString != "And") {
      qWarning() << "Expected 'and' or 'or' at line " << reader.lineNumber() << ", got "
                 << opString;
    }  // OP_AND is the default, set at the beginning of the function
  }

  QString const self = reader.name().toString();
  while (reader.getNextElement(self)) {
    auto elString = reader.name().toString();
    if (elString == "fileDependency") {
      conditional.m_Conditions.push_back(
          new FileCondition(reader.attributes().value("file").toString(),
                            reader.attributes().value("state").toString()));
      reader.finishedElement();
    } else if (elString == "flagDependency") {
      conditional.m_Conditions.push_back(
          new ValueCondition(reader.attributes().value("flag").toString(),
                             reader.attributes().value("value").toString()));
      reader.finishedElement();
    } else if (elString == "gameDependency") {
      conditional.m_Conditions.push_back(new VersionCondition(
          VersionCondition::v_Game, reader.attributes().value("version").toString()));
      reader.finishedElement();
    } else if (elString == "fommDependency") {
      conditional.m_Conditions.push_back(new VersionCondition(
          VersionCondition::v_FOMM, reader.attributes().value("version").toString()));
      reader.finishedElement();
    } else if (elString == "foseDependency") {
      conditional.m_Conditions.push_back(new VersionCondition(
          VersionCondition::v_FOSE, reader.attributes().value("version").toString()));
      reader.finishedElement();
    } else if (elString == "dependencies") {
      SubCondition* nested = new SubCondition();
      readCompositeDependency(reader, *nested);
      conditional.m_Conditions.push_back(nested);
    } else {
      reader.unexpected();
    }
  }
  if (conditional.m_Conditions.size() == 0) {
    qWarning() << "Empty conditional found at line " << reader.lineNumber();
  }
}

FomodInstallerDialog::ConditionalInstall
FomodInstallerDialog::readConditionalInstallPattern(XmlReader& reader)
{
  ConditionalInstall result;
  result.m_Condition.m_Operator = OP_AND;
  QString const self(reader.name().toString());
  while (reader.getNextElement(self)) {
    if (reader.name().toString() == "dependencies") {
      readCompositeDependency(reader, result.m_Condition);
    } else if (reader.name().toString() == "files") {
      readFileList(reader, result.m_Files);
    } else {
      reader.unexpected();
    }
  }
  return result;
}

void FomodInstallerDialog::readConditionalFilePatternList(XmlReader& reader)
{
  QString const self(reader.name().toString());
  while (reader.getNextElement(self)) {
    if (reader.name().toString() == "pattern") {
      m_ConditionalInstalls.push_back(readConditionalInstallPattern(reader));
    } else {
      reader.unexpected();
    }
  }
}

void FomodInstallerDialog::readConditionalFileInstallList(XmlReader& reader)
{
  QString const self(reader.name().toString());
  // Technically there should be only one but it's easier to write like this
  while (reader.getNextElement(self)) {
    if (reader.name().toString() == "patterns") {
      readConditionalFilePatternList(reader);
    } else {
      reader.unexpected();
    }
  }
}

void FomodInstallerDialog::readModuleConfiguration(XmlReader& reader)
{
  // sequence:
  //   modulename
  //   optional - moduleImage
  //   optional - moduleDependencies
  //   optional - requiredInstallFiles
  //   optional - installSteps
  //   optional - conditionalFileInstalls
  QString const self(reader.name().toString());
  while (reader.getNextElement(self)) {
    auto elString = reader.name().toString();
    if (elString == "moduleName") {
      QString title = reader.getText();
      qDebug() << "module name : " << title;
    } else if (elString == "moduleImage") {
      // do something useful with the attributes of this
      reader.finishedElement();
    } else if (elString == "moduleDependencies") {
      SubCondition condition;
      readCompositeDependency(reader, condition);
      std::pair<bool, QString> result = testCondition(-1, &condition);
      if (!result.first) {
        // TODO Better messages?
        throw Exception(result.second);
      }
    } else if (elString == "requiredInstallFiles") {
      readFileList(reader, m_RequiredFiles);
    } else if (elString == "installSteps") {
      readStepList(reader);
    } else if (elString == "conditionalFileInstalls") {
      readConditionalFileInstallList(reader);
    } else {
      reader.unexpected();
    }
  }
}

void FomodInstallerDialog::parseModuleConfig(XmlReader& reader)
{
  if (reader.readNext() != XmlReader::StartDocument) {
    throw XmlParseError(
        QString("Expected document start at line %1").arg(reader.lineNumber()));
  }
  processXmlTag(reader, "config", &FomodInstallerDialog::readModuleConfiguration);
  if (reader.readNext() != XmlReader::EndDocument) {
    throw XmlParseError(
        QString("Expected document end at line %1").arg(reader.lineNumber()));
  }
  if (reader.hasError()) {
    throw XmlParseError(
        QString("%1 in line %2").arg(reader.errorString()).arg(reader.lineNumber()));
  }
  // Find the first visible page
  int index = 0;
  while (index < ui->stepsStack->count()) {
    if (testVisible(index)) {
      ui->stepsStack->setCurrentIndex(index);
      displayCurrentPage();
      activateCurrentPage();
      break;
    }
    ++index;
  }
  // No pages are visible? Go to a small install
  if (index >= ui->stepsStack->count()) {
    transformToSmallInstall();
  }
}

void FomodInstallerDialog::processXmlTag(XmlReader& reader, char const* tag,
                                         TagProcessor func)
{
  if (reader.readNext() == XmlReader::StartElement && reader.name().toString() == tag) {
    (this->*func)(reader);
  } else if (!reader.hasError()) {
    reader.raiseError(
        QString("Expected %1, got %2").arg(tag).arg(reader.name().toString()));
  }
}

void FomodInstallerDialog::on_manualBtn_clicked()
{
  m_Manual = true;
  this->reject();
}

void FomodInstallerDialog::on_cancelBtn_clicked()
{
  this->reject();
}

void FomodInstallerDialog::on_websiteLabel_linkActivated(const QString& link)
{
#ifdef _WIN32
  ::ShellExecuteW(nullptr, L"open", ToWString(link).c_str(), nullptr, nullptr,
                  SW_SHOWNORMAL);
#else
  QDesktopServices::openUrl(QUrl(link));
#endif
}

void FomodInstallerDialog::activateCurrentPage()
{
  QList<QAbstractButton*> choices =
      ui->stepsStack->currentWidget()->findChildren<QAbstractButton*>("choice");
  if (choices.count() > 0) {
    highlightControl(choices.at(0));
  }
  m_PageVisible.push_back(true);
  updateNextbtnText();
}

std::pair<bool, QString> FomodInstallerDialog::testCondition(int maxIndex,
                                                             const QString& flag,
                                                             const QString& value) const
{
  // FIXME Review this and see if we can store the visible and evaluated variables for
  // each page and cache like that. This would make me happier (if no one else) about
  // the results of doing 'previous' and changing a flag to 'unset'.

  // iterate through all enabled condition flags on all activated controls on
  // all visible pages if one of them matches the condition, taking the most
  // recent setting.
  for (int i = maxIndex - 1; i >= 0; --i) {
    if (testVisible(i)) {
      QWidget* page                   = ui->stepsStack->widget(i);
      QList<QAbstractButton*> choices = page->findChildren<QAbstractButton*>("choice");
      for (QAbstractButton const* choice : choices) {
        if (choice->isChecked()) {
          QVariant temp = choice->property("conditionFlags");
          if (temp.isValid()) {
            QVariantList conditionFlags = temp.toList();
            for (QVariant const& variant : conditionFlags) {
              ConditionFlag condition = variant.value<ConditionFlag>();
              if (condition.m_Name == flag) {
                if (condition.m_Value == value)
                  return std::make_pair(true, tr("The flag '%1' matched '%2'")
                                                  .arg(condition.m_Name)
                                                  .arg(condition.m_Value));
                else
                  return std::make_pair(false, tr("The flag '%1' did not match '%2'")
                                                   .arg(condition.m_Name)
                                                   .arg(condition.m_Value));
              }
            }
          }
        }
      }
    }
  }
  if (value.isEmpty())
    return std::make_pair(true, tr("The condition was not matched and is empty."));
  return std::make_pair(false, tr("The value exists but was not matched."));
}

bool FomodInstallerDialog::testVisible(int pageIndex) const
{
  if (pageIndex < static_cast<int>(m_PageVisible.size())) {
    return m_PageVisible[pageIndex];
  }
  if (pageIndex >= ui->stepsStack->count()) {
    return false;
  }
  QWidget* page    = ui->stepsStack->widget(pageIndex);
  QVariant subcond = page->property("conditional");
  if (subcond.isValid()) {
    SubCondition subc = subcond.value<SubCondition>();
    return testCondition(pageIndex, &subc).first;
  }
  return true;
}

bool FomodInstallerDialog::nextPage()
{
  int oldIndex = ui->stepsStack->currentIndex();

  int index = oldIndex + 1;
  // find the next "visible" install step
  while (index < ui->stepsStack->count()) {
    if (testVisible(index)) {
      ui->stepsStack->setCurrentIndex(index);
      ui->stepsStack->currentWidget()->setProperty("previous", oldIndex);
      return true;
    }
    m_PageVisible.push_back(false);
    ++index;
  }
  // no more visible pages -> install
  qWarning("Got to install after pressing next!");
  return false;
}

void FomodInstallerDialog::widgetButtonClicked()
{
  // A button has been clicked. At the moment we do nothing with this
  // beyond checking the next button state
  updateNextbtnText();
}

void FomodInstallerDialog::updateNextbtnText()
{
  // First we see if we can actually allow the 'next' button. Specifically, this
  // is a test to ensure that you have selected at least one item in a
  //'select at least one' box.
  int const page = ui->stepsStack->currentIndex();
  QStringList groups_requiring_selection;
  for (QVBoxLayout const* const layout :
       ui->stepsStack->widget(page)->findChildren<QVBoxLayout*>("grouplayout")) {
    GroupType const groupType(layout->property("groupType").value<GroupType>());
    if (groupType == TYPE_SELECTATLEASTONE) {
      // Check at least one of this group is ticked
      bool checked = false;
      for (int i = 0; i != layout->count(); ++i) {
        if (QLayoutItem* item = layout->itemAt(i)) {
          QAbstractButton* const choice =
              dynamic_cast<QAbstractButton*>(item->widget());
          if (choice != nullptr) {
            if (choice->objectName() == "choice" && choice->isChecked()) {
              checked = true;
              break;
            }
          }
        }
      }
      if (!checked) {
        QString group = dynamic_cast<QGroupBox*>(layout->parentWidget())->title();
        qDebug() << "Group " << group << " needs a selection";
        groups_requiring_selection.append(group);
      }
    }
  }

  if (groups_requiring_selection.size() != 0) {
    ui->nextBtn->setText(tr("Disabled"));
    ui->nextBtn->setEnabled(false);
    ui->nextBtn->setToolTip(tr("This button is disabled because the following group(s) "
                               "need a selection: ") +
                            groups_requiring_selection.join(", "));
    return;
  }

  // OK, clear up any warnings
  ui->nextBtn->setToolTip("");

  // Display 'next' or 'install' as appropriate for the next button.
  // note this can change depending on what buttons you click here.

  auto old_PageVisible = m_PageVisible;
  ON_BLOCK_EXIT([&]() {
    m_PageVisible = old_PageVisible;
  });

  bool isLast = true;
  for (int index = page + 1; index != ui->stepsStack->count(); ++index) {
    if (testVisible(index)) {
      isLast = false;
      break;
    }
    m_PageVisible.push_back(false);
  }

  ui->nextBtn->setEnabled(true);
  ui->nextBtn->setText(isLast ? tr("Install") : tr("Next"));
}

void FomodInstallerDialog::displayCurrentPage()
{
  // Iterate over all buttons and set the tool tips as appropriate
  int const page = ui->stepsStack->currentIndex();
  for (QVBoxLayout* layout :
       ui->stepsStack->widget(page)->findChildren<QVBoxLayout*>("grouplayout")) {
    // Create a list of buttons, as in order to attempt to keep users existing choices
    // intact, we may need to cycle over this twice
    QList<QAbstractButton*> controls;
    QAbstractButton* none_button(nullptr);
    for (int i = 0; i != layout->count(); ++i) {
      if (QLayoutItem* const item = layout->itemAt(i)) {
        QAbstractButton* const choice = dynamic_cast<QAbstractButton*>(item->widget());
        if (choice != nullptr) {
          if (choice->objectName() == "choice") {
            controls.push_back(choice);
          } else if (choice->objectName() == "none") {
            none_button = choice;
          }
        }
      }
    }

    // FIXME If we are displaying this for the 2nd time, we should do two passes,
    // as currently if you have decided against a recommended option, gone back,
    // and then gone forward, your selection will be lost.
    // For tick boxes it requires a bit of thought, because the first time we come
    // in here, all tick boxes are clear, which is a valid condition. For radio
    // buttons, that's not a valid condition so we can override. But we should
    // possibly override anyway if the plugin types have changed since last time.
    GroupType groupType(layout->property("groupType").value<GroupType>());
    if (groupType != TYPE_SELECTALL) {
      bool const mustSelectOne =
          groupType == TYPE_SELECTEXACTLYONE || groupType == TYPE_SELECTATLEASTONE;
      bool maySelectMore              = true;
      QAbstractButton* first_optional = nullptr;
      QAbstractButton* first_couldbe  = nullptr;

      for (QAbstractButton* const control : controls) {
        PluginTypeInfo const info =
            control->property("plugintypeinfo").value<PluginTypeInfo>();
        PluginType const type = getPluginDependencyType(page, info);
        control->setEnabled(true);
        switch (type) {
        case TYPE_REQUIRED: {
          if ((groupType == TYPE_SELECTEXACTLYONE) ||
              (groupType == TYPE_SELECTATMOSTONE)) {
            // This only makes sense if the option may be disabled through
            // conditions, so that if the conditions are met, this option is
            // forced, otherwise the user can pick.
            // This means that in this case the option is forced, and no user
            // selection should be possible
            for (QAbstractButton* groupControl : controls) {
              groupControl->setEnabled(false);
            }
          } else {
            control->setEnabled(false);
          }
          control->setChecked(true);
          control->setToolTip(tr("This component is required"));
        } break;
        case TYPE_RECOMMENDED: {
          if (maySelectMore || !mustSelectOne) {
            control->setChecked(true);
          }
          control->setToolTip(tr("It is recommended you enable this component"));
        } break;
        case TYPE_OPTIONAL: {
          if (first_optional == nullptr) {
            first_optional = control;
          }
          control->setToolTip(tr("Optional component"));
        } break;
        case TYPE_NOTUSABLE: {
          control->setChecked(false);
          control->setEnabled(false);
          control->setToolTip(tr("This component is not usable in combination with "
                                 "other installed plugins"));
        } break;
        case TYPE_COULDBEUSABLE: {
          if (first_couldbe == nullptr) {
            first_couldbe = control;
          }
          control->setCheckable(true);
          control->setIcon(QIcon(":/new/guiresources/warning_16"));
          control->setToolTip(tr("You may be experiencing instability in combination "
                                 "with other installed plugins"));
        } break;
        }
        if (control->isChecked()) {
          maySelectMore = false;
        }
      }
      if (maySelectMore) {
        if (none_button != nullptr) {
          none_button->setChecked(true);
        } else if (mustSelectOne) {
          if (first_optional != nullptr) {
            first_optional->setChecked(true);
          } else if (first_couldbe != nullptr) {
            qWarning("User should select at least one plugin but the only ones "
                     "available could cause instability");
            first_couldbe->setChecked(true);
          } else {
            // FIXME Should this generate an error
            qWarning("User should select at least one plugin but none are available");
            controls[0]->setChecked(true);
          }
        }
      }
    }
  }
}

void FomodInstallerDialog::on_nextBtn_clicked()
{
  if (ui->stepsStack->currentIndex() == ui->stepsStack->count() - 1) {
    this->accept();
  } else {
    if (nextPage()) {
      ui->prevBtn->setEnabled(true);
      displayCurrentPage();
      activateCurrentPage();
    } else {
      this->accept();
    }
  }
}

void FomodInstallerDialog::on_prevBtn_clicked()
{
  // FIXME this will go wrong if the first page isn't visible
  if (ui->stepsStack->currentIndex() != 0) {
    int previousIndex = 0;
    QVariant temp     = ui->stepsStack->currentWidget()->property("previous");
    if (temp.isValid()) {
      previousIndex = temp.toInt();
    } else {
      previousIndex = ui->stepsStack->currentIndex() - 1;
    }
    ui->stepsStack->setCurrentIndex(previousIndex);
    m_PageVisible.resize(previousIndex);
    ui->nextBtn->setText(tr("Next"));
  }
  if (ui->stepsStack->currentIndex() == 0) {
    ui->prevBtn->setEnabled(false);
  }
  activateCurrentPage();
}

void FomodInstallerDialog::on_screenshotExpand_clicked()
{
  std::vector<std::pair<QString, QString>> carouselImages;
  int carouselIndex = -1;

  for (auto choice :
       ui->stepsStack->currentWidget()->findChildren<QAbstractButton*>("choice")) {
    QString screenshotFileName = choice->property("screenshot").toString();

    // If a choice has no screenshot, it should not be displayed in the screenshot
    // dialog nor marked as the active carouselIndex
    if (screenshotFileName.isEmpty()) {
      continue;
    }

    QString temp = QDir::tempPath() + "/" + m_FomodPath + "/" +
                   QDir::fromNativeSeparators(screenshotFileName);
    carouselImages.push_back(std::pair<QString, QString>(choice->text(), temp));

    // Focus the screenshot carousel on the user's selected choice (or the first if
    // there are multiple)
    if (carouselIndex == -1 && choice->isChecked()) {
      carouselIndex = ((int)carouselImages.size()) - 1;
    }
  }

  // Focus the screenshot carousel on the first screenshot if the user has not selected
  // a choice with a screenshot (or any choice at all)
  carouselIndex = (carouselIndex < 0) ? 0 : carouselIndex;

  QDialog* dialog = new FomodScreenshotDialog(this, carouselImages, carouselIndex);
  dialog->show();
}
