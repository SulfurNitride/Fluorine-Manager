#ifndef PERSISTENTCOOKIEJAR_H
#define PERSISTENTCOOKIEJAR_H

#include <QNetworkCookieJar>

class PersistentCookieJar : public QNetworkCookieJar
{

  Q_OBJECT

public:
  PersistentCookieJar(const QString& fileName, QObject* parent = nullptr);
  ~PersistentCookieJar() override;

  void clear();

private:
  bool save();

  void restore();

private:
  QString m_FileName;
};

#endif  // PERSISTENTCOOKIEJAR_H
