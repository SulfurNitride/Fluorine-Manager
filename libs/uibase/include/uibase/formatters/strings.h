
#pragma once

#include <format>
#include <string>
#include <string_view>

#include <QString>
#include <QStringView>

namespace MOBase::details
{
template <class CharT>
inline std::basic_string<CharT> toStdBasicString(QString const& qstring);

template <>
inline std::basic_string<char> toStdBasicString(QString const& qstring)
{
  return qstring.toStdString();
}
template <>
inline std::basic_string<wchar_t> toStdBasicString(QString const& qstring)
{
  return qstring.toStdWString();
}
template <>
inline std::basic_string<char16_t> toStdBasicString(QString const& qstring)
{
  return qstring.toStdU16String();
}
template <>
inline std::basic_string<char32_t> toStdBasicString(QString const& qstring)
{
  return qstring.toStdU32String();
}

inline QString fromStdBasicString(std::string const& value)
{
  return QString::fromStdString(value);
}
inline QString fromStdBasicString(std::wstring const& value)
{
  return QString::fromStdWString(value);
}
inline QString fromStdBasicString(std::u16string const& value)
{
  return QString::fromStdU16String(value);
}
inline QString fromStdBasicString(std::u32string const& value)
{
  return QString::fromStdU32String(value);
}
}  // namespace MOBase::details

template <class CharT>
struct std::formatter<QString, CharT> : std::formatter<std::basic_string<CharT>, CharT>
{
  template <class FmtContext>
  FmtContext::iterator format(QString s, FmtContext& ctx) const
  {
    return std::formatter<std::basic_string<CharT>, CharT>::format(
        MOBase::details::toStdBasicString<CharT>(s), ctx);
  }
};

template <class CharT1, class CharT2>
  requires(!std::is_same_v<CharT1, CharT2>)
struct std::formatter<std::basic_string<CharT1>, CharT2>
    : std::formatter<std::basic_string<CharT2>, CharT2>
{
  template <class FmtContext>
  FmtContext::iterator format(std::basic_string<CharT1> s, FmtContext& ctx) const
  {
    return std::formatter<std::basic_string<CharT2>, CharT2>::format(
        MOBase::details::toStdBasicString<CharT2>(
            MOBase::details::fromStdBasicString(s)),
        ctx);
  }
};

template <class CharT>
struct std::formatter<QStringView, CharT> : std::formatter<QString, CharT>
{
  template <class FmtContext>
  FmtContext::iterator format(QStringView s, FmtContext& ctx) const
  {
    return std::formatter<QString, CharT>::format(s.toString(), ctx);
  }
};
