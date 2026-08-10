#ifndef LAUNCHLIFECYCLE_H
#define LAUNCHLIFECYCLE_H

#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QMetaObject>
#include <QPointer>
#include <QString>

#include <atomic>
#include <exception>
#include <type_traits>
#include <utility>

// A prepared request is safe only when the directory entry is absent. QFile's
// false return is benign if another cleanup already removed it; conversely, a
// successful return is rechecked so a concurrently recreated artifact is not
// certified as gone.
inline bool removePreparedLaunchArtifact(const QString &path) noexcept {
  try {
    const auto exists = [&path]() {
      const QFileInfo info(path);
      return info.exists() || info.isSymLink();
    };
    if (path.isEmpty() || !exists()) {
      return true;
    }
    QFile::remove(path);
    return !exists();
  } catch (...) {
    return false;
  }
}

// A QEventLoop::quit() issued before the first exec() is not remembered by Qt.
// Keep the completion as independent state so synchronous ProcessRunner paths
// can skip exec() when a nested event loop completed the refresh reentrantly.
class RefreshWaitLatch {
public:
  explicit RefreshWaitLatch(QEventLoop &loop) : m_Loop(&loop) {}

  void complete() noexcept { finish(Outcome::Complete); }
  void fail() noexcept { finish(Outcome::Failed); }

  bool completeBeforeWait() const noexcept {
    return m_Outcome.load(std::memory_order_acquire) != Outcome::Pending;
  }

  bool failed() const noexcept {
    return m_Outcome.load(std::memory_order_acquire) == Outcome::Failed;
  }

private:
  enum class Outcome {
    Pending,
    Complete,
    Failed,
  };

  void finish(Outcome outcome) noexcept {
    Outcome expected = Outcome::Pending;
    if (!m_Outcome.compare_exchange_strong(expected, outcome,
                                           std::memory_order_release,
                                           std::memory_order_relaxed)) {
      return;
    }
    const QPointer<QEventLoop> loop = m_Loop;
    if (!loop) {
      return;
    }

    // Direct quit wakes an already-running loop. The queued quit closes the
    // check-to-exec race: if completion occurs just before exec(), the event
    // remains pending and is consumed as soon as that loop begins processing.
    loop->quit();
    try {
      QMetaObject::invokeMethod(
          loop,
          [loop]() noexcept {
            if (loop) {
              loop->quit();
            }
          },
          Qt::QueuedConnection);
    } catch (...) {
      // The completion bit still handles the ordinary pre-check case. Under an
      // allocation failure ownership remains retained by the surrounding
      // cleanup state rather than throwing through a Qt callback.
    }
  }
  QPointer<QEventLoop> m_Loop;
  std::atomic<Outcome> m_Outcome{Outcome::Pending};
};

// Scope guard used for the reservation-to-receipt window. Its destructor is
// noexcept and invokes rollback exactly once unless successful receipt binding
// explicitly dismisses it.
template <typename Rollback> class PreparedLaunchRollback {
public:
  explicit PreparedLaunchRollback(Rollback rollback)
      : m_Rollback(std::move(rollback)) {}

  PreparedLaunchRollback(const PreparedLaunchRollback &) = delete;
  PreparedLaunchRollback &operator=(const PreparedLaunchRollback &) = delete;

  PreparedLaunchRollback(PreparedLaunchRollback &&other) noexcept(
      std::is_nothrow_move_constructible_v<Rollback>)
      : m_Rollback(std::move(other.m_Rollback)), m_Armed(other.m_Armed) {
    other.m_Armed = false;
  }

  ~PreparedLaunchRollback() noexcept {
    if (!m_Armed) {
      return;
    }
    try {
      m_Rollback();
    } catch (...) {
    }
  }

  void dismiss() noexcept { m_Armed = false; }

private:
  Rollback m_Rollback;
  bool m_Armed{true};
};

template <typename Rollback>
PreparedLaunchRollback<std::decay_t<Rollback>>
makePreparedLaunchRollback(Rollback &&rollback) {
  return PreparedLaunchRollback<std::decay_t<Rollback>>(
      std::forward<Rollback>(rollback));
}

// Optional post-run synchronization must not be allowed to suppress the
// directory refresh promised to a synchronous caller. Keep the two failures
// distinct so callers can report a refresh-start failure as an error instead
// of invoking the success completion without having scheduled any work.
struct PostRefreshResult {
  bool refreshScheduled{false};
  std::exception_ptr postFailure;
  std::exception_ptr refreshFailure;
};

template <typename Post, typename Refresh>
PostRefreshResult runPostThenRefresh(Post &&post, Refresh &&refresh) noexcept {
  PostRefreshResult result;
  try {
    std::forward<Post>(post)();
  } catch (...) {
    result.postFailure = std::current_exception();
  }

  try {
    result.refreshScheduled = std::forward<Refresh>(refresh)();
  } catch (...) {
    result.refreshFailure = std::current_exception();
  }
  return result;
}

#endif // LAUNCHLIFECYCLE_H
