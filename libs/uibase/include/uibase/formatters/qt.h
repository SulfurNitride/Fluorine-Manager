#pragma once

#include <format>

#include <QColor>
#include <QFlag>
#include <QFlags>
#include <QRect>
#include <QSize>
#include <QString>
#include <QVariant>

template <class CharT>
struct std::formatter<QSize, CharT> : std::formatter<std::basic_string<CharT>, CharT>
{
  template <class FmtContext>
  FmtContext::iterator format(QSize s, FmtContext& ctx) const
  {
    return std::format_to(ctx.out(), "QSize({}, {})", s.width(), s.height());
  }
};

template <class CharT>
struct std::formatter<QRect, CharT> : std::formatter<std::basic_string<CharT>, CharT>
{
  template <class FmtContext>
  FmtContext::iterator format(QRect r, FmtContext& ctx) const
  {
    return std::format_to(ctx.out(), "QRect({},{}-{},{})", r.left(), r.top(), r.right(),
                          r.bottom());
  }
};

template <class CharT>
struct std::formatter<QColor, CharT> : std::formatter<std::basic_string<CharT>, CharT>
{
  template <class FmtContext>
  FmtContext::iterator format(QColor c, FmtContext& ctx) const
  {
    return std::format_to(ctx.out(), "QColor({}, {}, {}, {})", c.red(), c.green(),
                          c.blue(), c.alpha());
  }
};

template <class CharT>
struct std::formatter<QByteArray, CharT>
    : std::formatter<std::basic_string<CharT>, CharT>
{
  template <class FmtContext>
  FmtContext::iterator format(QByteArray v, FmtContext& ctx) const
  {
    return std::format_to(ctx.out(), "QByteArray({} bytes)", v.size());
  }
};

template <class CharT>
struct std::formatter<QVariant, CharT> : std::formatter<std::basic_string<CharT>, CharT>
{
  template <class FmtContext>
  FmtContext::iterator format(QVariant v, FmtContext& ctx) const
  {
    return std::format_to(
        ctx.out(), "QVariant(type={}, value={})", v.typeName(),
        (v.typeId() == QMetaType::Type::QByteArray ? "(binary)" : v.toString()));
  }
};

template <class CharT>
struct std::formatter<QFlag, CharT> : std::formatter<int, CharT>
{
  template <class FmtContext>
  FmtContext::iterator format(QFlag v, FmtContext& ctx) const
  {
    return std::formatter<int, CharT>::format(static_cast<int>(v), ctx);
  }
};

template <class T, class CharT>
struct std::formatter<QFlags<T>, CharT> : std::formatter<int, CharT>
{
  template <class FmtContext>
  FmtContext::iterator format(QFlags<T> v, FmtContext& ctx) const
  {
    // TODO: display flags has aa | bb | cc?
    return std::formatter<int, CharT>::format(v.toInt(), ctx);
  }
};
