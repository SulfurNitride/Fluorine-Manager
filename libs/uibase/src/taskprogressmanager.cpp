#include <uibase/taskprogressmanager.h>
#include <uibase/log.h>
#include <uibase/utility.h>
#include <QApplication>
#include <QMainWindow>
#include <QWidget>

namespace MOBase
{

TaskProgressManager& TaskProgressManager::instance()
{
  static TaskProgressManager s_Instance;
  return s_Instance;
}

void TaskProgressManager::forgetMe(quint32 id)
{
  if (m_Taskbar == nullptr) {
    return;
  }
  auto iter = m_Percentages.find(id);
  if (iter != m_Percentages.end()) {
    m_Percentages.erase(iter);
  }
  showProgress();
}

void TaskProgressManager::updateProgress(quint32 id, qint64 value, qint64 max)
{
  QMutexLocker lock(&m_Mutex);
  if (m_Taskbar == nullptr) {
    return;
  }

  if (value == max) {
    auto iter = m_Percentages.find(id);
    if (iter != m_Percentages.end()) {
      m_Percentages.erase(iter);
    }
  } else {
    m_Percentages[id] = std::make_pair(QTime::currentTime(), (value * 100) / max);
  }

  showProgress();
}

quint32 TaskProgressManager::getId()
{
  QMutexLocker lock(&m_Mutex);
  return m_NextId++;
}

void TaskProgressManager::showProgress()
{
#ifdef _WIN32
  if (!m_Percentages.empty()) {
    m_Taskbar->SetProgressState(m_WinId, TBPF_NORMAL);

    QTime now                = QTime::currentTime();
    unsigned long long total = 0;
    unsigned long long count = 0;

    for (auto iter = m_Percentages.begin(); iter != m_Percentages.end();) {
      if (iter->second.first.secsTo(now) < 15) {
        total += static_cast<unsigned long long>(iter->second.second);
        ++iter;
        ++count;
      } else {
        log::debug("no progress in 15 seconds ({})", iter->second.first.secsTo(now));
        iter = m_Percentages.erase(iter);
      }
    }

    m_Taskbar->SetProgressValue(m_WinId, total, count * 100);
  } else {
    m_Taskbar->SetProgressState(m_WinId, TBPF_NOPROGRESS);
  }
#else
  // On Linux, taskbar progress is not supported in this implementation.
  // Could integrate with D-Bus com.canonical.Unity.LauncherEntry or similar.
  (void)m_Percentages;
#endif
}

bool TaskProgressManager::tryCreateTaskbar()
{
#ifdef _WIN32
  // try to find our main window
  for (QWidget* widget : QApplication::topLevelWidgets()) {
    QMainWindow* mainWin = qobject_cast<QMainWindow*>(widget);
    if (mainWin != nullptr) {
      m_WinId = reinterpret_cast<HWND>(mainWin->winId());
    }
  }

  HRESULT result = 0;
  if (m_WinId != nullptr) {
    result = CoCreateInstance(CLSID_TaskbarList, 0, CLSCTX_INPROC_SERVER,
                              IID_PPV_ARGS(&m_Taskbar));
    if (result == S_OK) {
      return true;
    }
  }

  m_Taskbar = nullptr;

  if (m_CreateTries-- > 0) {
    QTimer::singleShot(1000, this, SLOT(tryCreateTaskbar()));
  } else {
    log::warn("failed to create taskbar connection");
  }
  return false;
#else
  // On Linux, no Windows taskbar API
  m_Taskbar = nullptr;
  return false;
#endif
}

TaskProgressManager::TaskProgressManager()
    : m_NextId(1), m_CreateTries(10), m_WinId(nullptr), m_Taskbar(nullptr)
{}

}  // namespace MOBase
