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
#include "fomodscreenshotdialog.h"
#include "ui_fomodscreenshotdialog.h"

#include <QDirIterator>
#include <QFrame>
#include <QProxyStyle>
#include <QPushButton>
#include <QScreen>
#include <QTableWidget>
#include <QWindow>

#include "scalelabel.h"

constexpr int kScreenshotTileWidth   = 100;
constexpr int kScreenshotTileHeight  = 80;
constexpr int kScreenshotTileSpacing = 16;

// Disables the native dotted selection border around focused elements. Unfortunately,
// disabling it via stylesheets is broken in the latest version of Qt
// https://stackoverflow.com/questions/9795791/removing-dotted-border-without-setting-nofocus-in-windows-pyqt
class NoFocusProxyStyle : public QProxyStyle
{
public:
  NoFocusProxyStyle(QStyle* baseStyle = nullptr) : QProxyStyle(baseStyle) {}

  void drawPrimitive(PrimitiveElement element, const QStyleOption* option,
                     QPainter* painter, const QWidget* widget) const
  {
    if (element == QStyle::PE_FrameFocusRect) {
      return;
    }
    QProxyStyle::drawPrimitive(element, option, painter, widget);
  }
};

FomodScreenshotDialog::FomodScreenshotDialog(
    QWidget* parent, std::vector<std::pair<QString, QString>> carouselImages,
    int carouselIndex)
    : QDialog(parent, Qt::FramelessWindowHint), ui(new Ui::FomodScreenshotDialog),
      m_carouselImages(carouselImages)
{
  Q_INIT_RESOURCE(resources);

  ui->setupUi(this);
  setAttribute(Qt::WA_TranslucentBackground);

  // Manually maximize the dialog since showMaximized() clips over the taskbar
  QScreen* screen         = this->screen();
  QRect availableGeometry = screen->availableGeometry();
  setFixedSize(availableGeometry.width(), availableGeometry.height());
  move(availableGeometry.x(), availableGeometry.y());

  QTableWidget* carouselList = ui->carouselList;
  carouselList->setStyle(new NoFocusProxyStyle);

  carouselList->setRowCount(1);
  carouselList->setRowHeight(0, kScreenshotTileHeight);
  carouselList->setColumnCount(0);
  for (auto carouselImage : m_carouselImages) {
    QFrame* container   = new QFrame(carouselList);
    QVBoxLayout* layout = new QVBoxLayout(container);
    layout->setContentsMargins(0, 0, 0, 0);
    container->setLayout(layout);

    ScaleLabel* scaleLabel = new ScaleLabel(container);
    scaleLabel->setScalableResource(carouselImage.second);
    scaleLabel->setFixedSize(kScreenshotTileWidth, kScreenshotTileHeight);
    scaleLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    scaleLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    scaleLabel->setCursor(Qt::PointingHandCursor);
    scaleLabel->setStatic(true);

    layout->addWidget(scaleLabel);
    layout->setAlignment(scaleLabel, Qt::AlignLeft | Qt::AlignVCenter);

    int column = carouselList->columnCount();
    carouselList->setColumnCount(column + 1);
    carouselList->setColumnWidth(
        column, kScreenshotTileWidth +
                    (column + 1 == carouselImages.size() ? 0 : kScreenshotTileSpacing));

    QTableWidgetItem* item = new QTableWidgetItem("");
    carouselList->setItem(0, column, item);
    carouselList->setCellWidget(0, column, container);
  }

  connect(carouselList, &QTableWidget::itemSelectionChanged, this,
          &FomodScreenshotDialog::selectedScreenshotChanged);
  ui->carouselList->selectColumn(carouselIndex);
}

FomodScreenshotDialog::~FomodScreenshotDialog()
{
  delete ui;
}

void FomodScreenshotDialog::on_closeButton_clicked()
{
  close();
}

void FomodScreenshotDialog::on_navigateLeft_clicked()
{
  int selectedColumn = getSelectedScreenshot();
  if (selectedColumn == 0) {
    return;
  }

  ui->carouselList->selectColumn(selectedColumn - 1);
}

void FomodScreenshotDialog::on_navigateRight_clicked()
{
  int selectedColumn = getSelectedScreenshot();
  if (selectedColumn == m_carouselImages.size() - 1) {
    return;
  }

  ui->carouselList->selectColumn(selectedColumn + 1);
}

void FomodScreenshotDialog::selectedScreenshotChanged()
{
  // In case the user ctrl+clicks current selection to result in an empty selection
  if (ui->carouselList->selectedItems().isEmpty()) {
    ui->carouselList->selectColumn(0);
    return;
  }

  int selectedColumn = getSelectedScreenshot();

  ui->imageTitleLabel->setText(m_carouselImages.at(selectedColumn).first);
  ui->image->setScalableResource(m_carouselImages.at(selectedColumn).second);
  ui->slideshowPosition->setText(QString("%1/%2").arg(
      QString::number(selectedColumn + 1), QString::number(m_carouselImages.size())));

  for (int column = 0; column < ui->carouselList->columnCount(); column++) {
    QWidget* widget        = ui->carouselList->cellWidget(0, column);
    ScaleLabel* scaleLabel = widget->findChild<ScaleLabel*>();
    if (column == selectedColumn) {
      scaleLabel->setStyleSheet(scaleLabel->styleSheet() +
                                "QLabel { border:2px solid white; }");
    } else {
      scaleLabel->setStyleSheet(scaleLabel->styleSheet() + "QLabel { border:none; }");
    }
  }
}

int FomodScreenshotDialog::getSelectedScreenshot()
{
  return ui->carouselList->selectedItems().front()->column();
}
