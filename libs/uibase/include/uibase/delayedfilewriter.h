#ifndef DELAYEDFILEWRITER_H
#define DELAYEDFILEWRITER_H

#include "dllimport.h"
#include <QString>
#include <QTimer>
#include <functional>

namespace MOBase
{

/**
 * The purpose of this class is to aggregate changes to a file before writing it out
 */
class QDLLEXPORT DelayedFileWriterBase : public QObject
{

  Q_OBJECT

public:
  /**
   * @brief constructor
   * @param fileName
   * @param delay delay (in milliseconds) before we call the actual write function
   */
  DelayedFileWriterBase(int delay = 200);
  ~DelayedFileWriterBase();

public slots:
  /**
   * @brief write with delay
   */
  void write();

  /**
   * @brief cancel a scheduled write (does nothing if no write is scheduled)
   */
  void cancel();

  /**
   * @brief write immediately without waiting for the timer to expire
   * @param ifOnTimer only write if the timer is running
   */
  void writeImmediately(bool ifOnTimer);

private slots:
  void timerExpired();

private:
  virtual void doWrite() = 0;

private:
  int m_TimerDelay;
  QTimer m_Timer;
};

class QDLLEXPORT DelayedFileWriter : public DelayedFileWriterBase
{
public:
  typedef std::function<void()> WriterFunc;

public:
  DelayedFileWriter(WriterFunc func, int delay = 200);

private:
  void doWrite();

private:
  WriterFunc m_Func;
};

}  // namespace MOBase

#endif  // DELAYEDFILEWRITER_H
