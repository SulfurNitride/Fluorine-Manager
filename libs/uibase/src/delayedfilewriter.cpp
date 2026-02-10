#include <uibase/delayedfilewriter.h>
#include <uibase/log.h>

using namespace MOBase;

DelayedFileWriterBase::DelayedFileWriterBase(int delay) : m_TimerDelay(delay), m_Timer()
{
  QObject::connect(&m_Timer, &QTimer::timeout, this, &DelayedFileWriter::timerExpired);
  m_Timer.setSingleShot(true);
}

DelayedFileWriterBase::~DelayedFileWriterBase()
{
  if (m_Timer.isActive()) {
    log::error("delayed file save timer active at shutdown");
  }
}

void DelayedFileWriterBase::write()
{
  m_Timer.start(m_TimerDelay);
}

void DelayedFileWriterBase::cancel()
{
  m_Timer.stop();
}

void DelayedFileWriterBase::writeImmediately(bool ifOnTimer)
{
  if (!ifOnTimer || m_Timer.isActive()) {
    m_Timer.stop();
    doWrite();
  }
}

void DelayedFileWriterBase::timerExpired()
{
  doWrite();
}

DelayedFileWriter::DelayedFileWriter(DelayedFileWriter::WriterFunc func, int delay)
    : DelayedFileWriterBase(delay), m_Func(func)
{}

void DelayedFileWriter::doWrite()
{
  m_Func();
}
