#ifndef SCALELABEL_H
#define SCALELABEL_H

#include <QImage>
#include <QLabel>
#include <QMovie>

class ScaleLabel : public QLabel
{
  Q_OBJECT
public:
  explicit ScaleLabel(QWidget* parent = nullptr);

  void setScalableResource(const QString& path);
  void setStatic(bool isStatic);
signals:

public slots:
protected:
  virtual void resizeEvent(QResizeEvent* event);

private:
  void setScalableMovie(const QString& path);
  void setScalableImage(const QString& path);

  QImage m_UnscaledImage;
  QSize m_OriginalMovieSize;
  bool m_isStatic = false;
};

#endif  // SCALELABEL_H
