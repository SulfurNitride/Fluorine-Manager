#pragma once

#include <optional>

#include <QLabel>
#include <QVector>
#include <QWidget>

std::optional<QVector<int>> DialogSelect(QWidget* parent, const QString& title, const QVector<QString>& items,
                                         const QVector<QString>& descriptions, const QVector<QString>& pixmaps,
                                         bool multiSelect);


// For some reason, this can be resized bigger but not smaller.
class FixedAspectRatioImageLabel : public QLabel
{
  Q_OBJECT

  Q_PROPERTY(QPixmap unscaledPixmap READ unscaledPixmap WRITE setUnscaledPixmap)

public:
  FixedAspectRatioImageLabel() = default;

  FixedAspectRatioImageLabel(QWidget* parent);

  void setUnscaledPixmap(const QPixmap& pixmap);

  const QPixmap& unscaledPixmap() const;

  QSize sizeHint() const override;

  bool hasHeightForWidth() const override;

  int heightForWidth(int width) const override;

  int widthForHeight(int height) const;

protected:
  void resizeEvent(QResizeEvent* resizeEvent) override;

private:
  void rescalePixmap(const QSize& size);

  QPixmap mUnscaledPixmap;
  QLabel copySizeLabel;
};
