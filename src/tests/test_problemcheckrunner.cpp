#include "problemcheckrunner.h"

#include <gtest/gtest.h>

#include <QThread>

#include <deque>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace
{
using Queue = std::deque<ProblemCheckRunner::Callback>;

void runNext(Queue& queue)
{
  ASSERT_FALSE(queue.empty());
  auto callback = std::move(queue.front());
  queue.pop_front();
  callback();
}

void runAll(Queue& queue)
{
  while (!queue.empty()) {
    runNext(queue);
  }
}
}

TEST(ProblemCheckRunnerTest, RunsOneTaskPerOwningThreadTurn)
{
  Queue queue;
  ProblemCheckRunner runner(
      [&queue](auto callback) { queue.push_back(std::move(callback)); });
  QThread* const ownerThread = QThread::currentThread();
  std::vector<std::string> events;

  runner.start(
      {[&]() {
         EXPECT_EQ(QThread::currentThread(), ownerThread);
         events.emplace_back("first");
         queue.push_back([&events]() { events.emplace_back("sentinel"); });
         return 1;
       },
       [&]() {
         EXPECT_EQ(QThread::currentThread(), ownerThread);
         events.emplace_back("second");
         return 2;
       }},
      [&events](std::size_t total) {
        EXPECT_EQ(total, 3);
        events.emplace_back("complete");
      });

  runAll(queue);
  EXPECT_EQ(events,
            (std::vector<std::string>{"first", "sentinel", "second", "complete"}));
  EXPECT_FALSE(runner.running());
}

TEST(ProblemCheckRunnerTest, ExceptionsDoNotStopLaterTasks)
{
  Queue queue;
  ProblemCheckRunner runner(
      [&queue](auto callback) { queue.push_back(std::move(callback)); });
  int failures = 0;
  int total    = -1;

  runner.start({[]() -> std::size_t { throw std::runtime_error("failure"); },
                []() -> std::size_t { return 4; }},
               [&total](std::size_t value) { total = static_cast<int>(value); },
               [&failures]() { ++failures; });
  runAll(queue);

  EXPECT_EQ(failures, 1);
  EXPECT_EQ(total, 4);
}

TEST(ProblemCheckRunnerTest, CancelPreventsRemainingWorkAndPublication)
{
  Queue queue;
  ProblemCheckRunner runner(
      [&queue](auto callback) { queue.push_back(std::move(callback)); });
  int tasksRun = 0;
  bool published = false;

  runner.start({[&]() {
                  ++tasksRun;
                  return 1;
                },
                [&]() {
                  ++tasksRun;
                  return 1;
                }},
               [&published](std::size_t) { published = true; });
  runNext(queue);
  runner.cancel();
  runAll(queue);

  EXPECT_EQ(tasksRun, 1);
  EXPECT_FALSE(published);
  EXPECT_FALSE(runner.running());
}

TEST(ProblemCheckRunnerTest, ReentrantCancelDefersQuiescedCallback)
{
  Queue queue;
  ProblemCheckRunner runner(
      [&queue](auto callback) { queue.push_back(std::move(callback)); });
  std::vector<std::string> events;

  runner.start({[&]() {
                  events.emplace_back("task");
                  runner.cancel(
                      [&events]() { events.emplace_back("quiesced"); });
                  events.emplace_back("task-return");
                  return 1;
                },
                [&]() {
                  events.emplace_back("unexpected");
                  return 1;
                }},
               [&events](std::size_t) { events.emplace_back("published"); });
  runAll(queue);

  EXPECT_EQ(events,
            (std::vector<std::string>{"task", "task-return", "quiesced"}));
  EXPECT_FALSE(runner.running());
}

TEST(ProblemCheckRunnerTest, DestructionInvalidatesQueuedStep)
{
  Queue queue;
  bool taskRan = false;
  {
    ProblemCheckRunner runner(
        [&queue](auto callback) { queue.push_back(std::move(callback)); });
    runner.start({[&taskRan]() {
                    taskRan = true;
                    return 0;
                  }},
                 [](std::size_t) {});
  }

  runAll(queue);
  EXPECT_FALSE(taskRan);
}

TEST(ProblemCheckRunnerTest, DestructionDuringTaskDoesNotResumeMemberAccess)
{
  Queue queue;
  bool published = false;
  auto runner = std::make_unique<ProblemCheckRunner>(
      [&queue](auto callback) { queue.push_back(std::move(callback)); });

  runner->start({[&runner]() {
                   runner.reset();
                   return 1;
                 }},
                [&published](std::size_t) { published = true; });

  runAll(queue);
  EXPECT_EQ(runner, nullptr);
  EXPECT_FALSE(published);
}
