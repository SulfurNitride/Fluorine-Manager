#include "scalelabel.h"

#include <QResizeEvent>

static bool isResourceMovie(const QString& path)
{
  for (QByteArray format : QMovie::supportedFormats()) {
    QString fileExtension = "." + QString::fromUtf8(format);
    if (path.endsWith(fileExtension)) {
      return true;
    }
  }

  return false;
}

ScaleLabel::ScaleLabel(QWidget* parent) : QLabel(parent) {}

void ScaleLabel::setScalableResource(const QString& path)
{
  if (auto m = movie()) {
    setMovie(nullptr);
    delete m;
    m_OriginalMovieSize = QSize();
  }
  if (!pixmap().isNull()) {
    setPixmap(QPixmap());
    m_UnscaledImage = QImage();
  }

  if (path.isEmpty()) {
    return;
  }

  if (isResourceMovie(path)) {
    setScalableMovie(path);
  } else {
    setScalableImage(path);
  }
}

void ScaleLabel::setStatic(bool isStatic)
{
  m_isStatic = isStatic;

  if (auto m = movie()) {
    if (isStatic) {
      m->stop();
    } else {
      m->start();
    }
  }
}

void ScaleLabel::setScalableMovie(const QString& path)
{
  QMovie* m = new QMovie(path);
  if (!m->isValid()) {
    qWarning(">%s< is an invalid movie. Reason: %s", qUtf8Printable(path),
             m->lastErrorString().toStdString().c_str());
    delete m;
    return;
  }

  m->setParent(this);
  setMovie(m);
  m->start();
  m->stop();
  m_OriginalMovieSize = m->currentImage().size();

  m->setScaledSize(m_OriginalMovieSize.scaled(size(), Qt::KeepAspectRatio));
  if (!m_isStatic) {
    m->start();
  }
}

void ScaleLabel::setScalableImage(const QString& path)
{
  QImage image(path);
  if (image.isNull()) {
    qWarning(">%s< is a null image", qUtf8Printable(path));
  } else {
    m_UnscaledImage = image;
    setPixmap(QPixmap::fromImage(image).scaled(size(), Qt::KeepAspectRatio));
  }
}

void ScaleLabel::resizeEvent(QResizeEvent* event)
{
  if (auto m = movie()) {
    m->stop();
    m->setScaledSize(m_OriginalMovieSize.scaled(event->size(), Qt::KeepAspectRatio));
    m->start();

    // We can't just skip the start() above since that is what triggers the label to
    // resize the movie The only way to resize the movie but keep it paused is to start
    // and then re-stop it
    if (m_isStatic) {
      m->stop();
    }
  }
  auto p = pixmap();
  if (!p.isNull()) {
    setPixmap(
        QPixmap::fromImage(m_UnscaledImage).scaled(event->size(), Qt::KeepAspectRatio));
  }
}
