#pragma once

#include <QDialog>
#include <QString>

#include <utility>
#include <vector>

namespace Ui
{
class FomodScreenshotDialog;
}

class FomodScreenshotDialog : public QDialog
{
  Q_OBJECT

public:
  explicit FomodScreenshotDialog(
      QWidget* parent, std::vector<std::pair<QString, QString>> carouselImages,
      int carouselIndex);
  ~FomodScreenshotDialog();

private slots:
  void on_closeButton_clicked();
  void on_navigateLeft_clicked();
  void on_navigateRight_clicked();

private:
  void selectedScreenshotChanged();
  int getSelectedScreenshot();

  Ui::FomodScreenshotDialog* ui;
  std::vector<std::pair<QString, QString>> m_carouselImages;
};
