#include <uibase/strings.h>

#include <algorithm>
#include <locale>

namespace MOBase
{

// this is strongly inspired from boost
class is_iequal
{
  std::locale m_loc;

public:
  is_iequal(const std::locale& loc = std::locale()) : m_loc{loc} {}

  template <typename T1, typename T2>
  bool operator()(const T1& Arg1, const T2& Arg2) const
  {
    return std::toupper<T1>(Arg1, m_loc) == std::toupper<T2>(Arg2, m_loc);
  }
};

bool iequals(std::string_view lhs, std::string_view rhs)
{
  return std::equal(lhs.begin(), lhs.end(), rhs.begin(), rhs.end(), is_iequal());
}

void ireplace_all(std::string& input, std::string_view search,
                  std::string_view replace) noexcept
{
  const auto search_length  = static_cast<std::string::difference_type>(search.size());
  const auto replace_length = replace.size();

  std::size_t i = 0;
  while (input.size() - i >= search_length) {
    if (iequals(std::string_view(input).substr(i, search_length), search)) {
      input.replace(i, search_length, replace);
      i += replace_length;
    } else {
      ++i;
    }
  }
}

}  // namespace MOBase
