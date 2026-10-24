#include "shared/applicationexit.h"

#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QTimer>

namespace
{
int runLoopEndingWith(QCoreApplication& app, int code)
{
  QTimer::singleShot(0, &app, [&app, code] {
    MOShared::exitEventLoop(&app, code);
  });
  return app.exec();
}
}  // namespace

TEST(ApplicationExit, RestartsDoNotEmitAboutToQuitButTheFinalExitDoes)
{
  int argc = 1;
  char name[] = "test_applicationexit";
  char* argv[] = {name, nullptr};
  QCoreApplication app(argc, argv);

  int aboutToQuit = 0;
  QObject::connect(&app, &QCoreApplication::aboutToQuit, &app, [&] {
    ++aboutToQuit;
  });

  EXPECT_EQ(runLoopEndingWith(app, RestartExitCode), RestartExitCode);
  EXPECT_EQ(runLoopEndingWith(app, RestartExitCode), RestartExitCode);
  EXPECT_EQ(aboutToQuit, 0);

  EXPECT_EQ(runLoopEndingWith(app, 0), 0);
  EXPECT_EQ(aboutToQuit, 1);
}
