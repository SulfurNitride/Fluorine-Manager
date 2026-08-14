#include "fluorineupdater.h"

#include <uibase/log.h>

#include <QCoreApplication>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <deque>
#include <memory>
#include <utility>

namespace
{
class ControlledReply : public QNetworkReply
{
public:
  enum class AbortBehavior
  {
    SynchronousFinish,
    DelayedFinish,
  };

  explicit ControlledReply(AbortBehavior behavior, int* abortCount,
                           QObject* parent)
      : QNetworkReply(parent), m_abortBehavior(behavior),
        m_abortCount(abortCount)
  {
    open(QIODevice::ReadOnly | QIODevice::Unbuffered);
  }

  void abort() override
  {
    m_aborted = true;
    if (m_abortCount != nullptr) {
      ++*m_abortCount;
    }
    setError(QNetworkReply::OperationCanceledError,
             QStringLiteral("aborted"));
    if (m_abortBehavior == AbortBehavior::SynchronousFinish) {
      setFinished(true);
      emit finished();
    }
  }

  void finishSuccess(const QByteArray& body)
  {
    m_body   = body;
    m_offset = 0;
    setFinished(true);
    emit readyRead();
    emit finished();
  }

  void finishError()
  {
    setError(QNetworkReply::ConnectionRefusedError,
             QStringLiteral("refused"));
    setFinished(true);
    emit finished();
  }

  void emitDelayedAbort()
  {
    setFinished(true);
    emit finished();
  }

  bool wasAborted() const { return m_aborted; }

  qint64 bytesAvailable() const override
  {
    return (m_body.size() - m_offset) + QNetworkReply::bytesAvailable();
  }

protected:
  qint64 readData(char* data, qint64 maxSize) override
  {
    const qint64 remaining = m_body.size() - m_offset;
    if (remaining <= 0) {
      return -1;
    }
    const qint64 count = std::min(maxSize, remaining);
    std::memcpy(data, m_body.constData() + m_offset,
                static_cast<std::size_t>(count));
    m_offset += count;
    return count;
  }

private:
  AbortBehavior m_abortBehavior;
  QByteArray m_body;
  qint64 m_offset = 0;
  bool m_aborted  = false;
  int* m_abortCount = nullptr;
};

class TestUpdater : public FluorineUpdater
{
public:
  struct Plan
  {
    ControlledReply::AbortBehavior abortBehavior =
        ControlledReply::AbortBehavior::SynchronousFinish;
    QByteArray immediateBody;
    int* abortCount = nullptr;
  };

  explicit TestUpdater(QObject* parent = nullptr) : FluorineUpdater(parent) {}

  void enqueue(Plan plan) { m_plans.push_back(std::move(plan)); }

  ControlledReply* reply(int index) const { return m_replies.at(index); }
  const QNetworkRequest& request(int index) const { return m_requests.at(index); }

protected:
  QNetworkReply* createRequest(const QNetworkRequest& request) override
  {
    EXPECT_FALSE(m_plans.empty());
    if (m_plans.empty()) {
      return nullptr;
    }

    Plan plan = std::move(m_plans.front());
    m_plans.pop_front();
    auto* reply =
        new ControlledReply(plan.abortBehavior, plan.abortCount, this);
    m_requests.push_back(request);
    m_replies.push_back(reply);
    if (!plan.immediateBody.isEmpty()) {
      reply->finishSuccess(plan.immediateBody);
    }
    return reply;
  }

private:
  std::deque<Plan> m_plans;
  QList<QNetworkRequest> m_requests;
  QList<ControlledReply*> m_replies;
};

QByteArray stableRelease()
{
  return QByteArrayLiteral(
      R"({"tag_name":"v999.0.0","name":"Future release","html_url":"https://example.invalid/release","assets":[{"name":"fluorine.tar.gz","browser_download_url":"https://example.invalid/fluorine.tar.gz"}]})");
}

struct Signals
{
  explicit Signals(FluorineUpdater& updater)
  {
    QObject::connect(
        &updater, &FluorineUpdater::updateAvailable,
        [&](const FluorineUpdater::ReleaseInfo& info) {
          ++available;
          lastInfo = info;
        });
    QObject::connect(&updater, &FluorineUpdater::upToDate,
                     [&](const FluorineUpdater::ReleaseInfo&) { ++current; });
    QObject::connect(&updater, &FluorineUpdater::checkFailed,
                     [&](const QString&) { ++failed; });
  }

  int available = 0;
  int current   = 0;
  int failed    = 0;
  FluorineUpdater::ReleaseInfo lastInfo;
};

TEST(FluorineUpdater, SynchronousAbortCannotConsumeReplacement)
{
  TestUpdater updater;
  Signals observed(updater);
  updater.enqueue({ControlledReply::AbortBehavior::SynchronousFinish, {}});
  updater.enqueue({ControlledReply::AbortBehavior::SynchronousFinish, {}});

  updater.checkForUpdates(FluorineUpdater::Channel::Nightly);
  ControlledReply* oldReply = updater.reply(0);
  updater.checkForUpdates(FluorineUpdater::Channel::Stable);

  EXPECT_TRUE(oldReply->wasAborted());
  EXPECT_EQ(observed.available + observed.current + observed.failed, 0);
  updater.reply(1)->finishSuccess(stableRelease());
  EXPECT_EQ(observed.available, 1);
  EXPECT_EQ(observed.current, 0);
  EXPECT_EQ(observed.failed, 0);
  EXPECT_EQ(observed.lastInfo.channel, FluorineUpdater::Channel::Stable);
  EXPECT_EQ(updater.request(1).transferTimeout(), 30000);
}

TEST(FluorineUpdater, DelayedSupersededCompletionIsIgnored)
{
  TestUpdater updater;
  Signals observed(updater);
  updater.enqueue({ControlledReply::AbortBehavior::DelayedFinish, {}});
  updater.enqueue({ControlledReply::AbortBehavior::SynchronousFinish, {}});

  updater.checkForUpdates(FluorineUpdater::Channel::Nightly);
  ControlledReply* oldReply = updater.reply(0);
  updater.checkForUpdates(FluorineUpdater::Channel::Stable);
  oldReply->emitDelayedAbort();

  EXPECT_EQ(observed.available + observed.current + observed.failed, 0);
  updater.reply(1)->finishSuccess(stableRelease());
  EXPECT_EQ(observed.available, 1);
  EXPECT_EQ(observed.failed, 0);
  EXPECT_EQ(observed.lastInfo.channel, FluorineUpdater::Channel::Stable);
}

TEST(FluorineUpdater, AlreadyFinishedReplyIsConsumedOnce)
{
  TestUpdater updater;
  Signals observed(updater);
  updater.enqueue({ControlledReply::AbortBehavior::SynchronousFinish,
                   stableRelease()});

  updater.checkForUpdates(FluorineUpdater::Channel::Stable);

  EXPECT_EQ(observed.available, 1);
  EXPECT_EQ(observed.current, 0);
  EXPECT_EQ(observed.failed, 0);
}

TEST(FluorineUpdater, CancelRetiresPendingReplySilently)
{
  auto updater = std::make_unique<TestUpdater>();
  Signals observed(*updater);
  updater->enqueue({ControlledReply::AbortBehavior::SynchronousFinish, {}});
  updater->checkForUpdates(FluorineUpdater::Channel::Stable);
  QPointer<ControlledReply> reply(updater->reply(0));

  updater->cancel();
  EXPECT_TRUE(reply);
  EXPECT_TRUE(reply->wasAborted());
  EXPECT_EQ(observed.available + observed.current + observed.failed, 0);

  updater.reset();
  EXPECT_FALSE(reply);
}

TEST(FluorineUpdater, DestructionAbortsAndRetiresActiveReply)
{
  int abortCount = 0;
  QPointer<ControlledReply> reply;
  {
    auto updater = std::make_unique<TestUpdater>();
    Signals observed(*updater);
    updater->enqueue({ControlledReply::AbortBehavior::SynchronousFinish, {},
                      &abortCount});
    updater->checkForUpdates(FluorineUpdater::Channel::Stable);
    reply = updater->reply(0);

    updater.reset();
    EXPECT_EQ(observed.available + observed.current + observed.failed, 0);
  }

  EXPECT_EQ(abortCount, 1);
  EXPECT_FALSE(reply);
}

TEST(FluorineUpdater, CurrentNetworkErrorEmitsOneFailure)
{
  TestUpdater updater;
  Signals observed(updater);
  updater.enqueue({ControlledReply::AbortBehavior::SynchronousFinish, {}});
  updater.checkForUpdates(FluorineUpdater::Channel::Stable);

  updater.reply(0)->finishError();

  EXPECT_EQ(observed.available, 0);
  EXPECT_EQ(observed.current, 0);
  EXPECT_EQ(observed.failed, 1);
}
}  // namespace

int main(int argc, char** argv)
{
  QCoreApplication application(argc, argv);
  MOBase::log::LoggerConfiguration logging;
  logging.name     = "test_fluorineupdater";
  logging.maxLevel = MOBase::log::Warning;
  MOBase::log::createDefault(std::move(logging));
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
