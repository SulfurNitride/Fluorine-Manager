#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QTranslator>

int main(int argc, char** argv)
{
  QCoreApplication app(argc, argv);
  QTranslator translator;
  if (translator.load("tests_fr", "tests/translations")) {
    app.installTranslator(&translator);
  }
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
