#pragma once

#include <QObject>

static inline void deleteQObjectLater(QObject* qObject) { qObject->deleteLater(); }

template <typename T>
using QObject_unique_ptr = std::unique_ptr<T, decltype(&deleteQObjectLater)>;

template <typename T, typename... Args>
QObject_unique_ptr<T> make_unique(Args&&... args)
{
  return QObject_unique_ptr<T>(new T(std::forward<Args>(args)...), &deleteQObjectLater);
}

template <typename T>
QObject_unique_ptr<T> make_nullptr()
{
  return QObject_unique_ptr<T>(nullptr, &deleteQObjectLater);
}
