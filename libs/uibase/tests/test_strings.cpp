#pragma warning(push)
#pragma warning(disable : 4668)
#include <gtest/gtest.h>
#pragma warning(pop)

#include <QCoreApplication>

#include <uibase/strings.h>

#include <format>

using namespace MOBase;

TEST(StringsTest, IEquals)
{
  ASSERT_TRUE(iequals("hello world", "HelLO WOrlD"));
}

TEST(StringsTest, IReplaceAll)
{
  auto ireplace_all = [](std::string_view input, std::string_view search,
                         std::string_view replace) {
    std::string s_input{input};
    MOBase::ireplace_all(s_input, search, replace);
    return s_input;
  };

  ASSERT_EQ("", ireplace_all("", "world", "MO2"));
  ASSERT_EQ("Hello World!", ireplace_all("Hello World!", "Test", "MO2"));
  ASSERT_EQ("replace a stuff with a stuff a",
            ireplace_all("replace some stuff with some stuff some", "some", "a"));
  ASSERT_EQ("replace a stuff with a stuff som",
            ireplace_all("replace some stuff with some stuff som", "some", "a"));
  ASSERT_EQ("1YYY3YYY2", ireplace_all("1aBc3AbC2", "abC", "YYY"));

  ASSERT_EQ(
      "data path: C:/Users/USERNAME/AppData/Local/ModOrganizer/Starfield",
      ireplace_all("data path: C:/Users/lords/AppData/Local/ModOrganizer/Starfield",
                   "/lords", "/USERNAME"));
}

// this is more a tests of the tests
TEST(StringsTest, Translation)
{
  ASSERT_EQ("Traduction en Français",
            QCoreApplication::translate("uibase-tests", "Translate to French"));
}
