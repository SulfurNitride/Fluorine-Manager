/*
Mod Organizer shared UI functionality

Copyright (C) 2012 Sebastian Herbord. All rights reserved.

This library is free software; you can redistribute it and/or
modify it under the terms of the GNU Lesser General Public
License as published by the Free Software Foundation; either
version 3 of the License, or (at your option) any later version.

This library is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public
License along with this library; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
*/

#ifndef GUESSEDVALUE_H
#define GUESSEDVALUE_H

#include <functional>
#include <set>

namespace MOBase
{

/**
 * @brief describes how good the code considers a guess (i.e. for a mod name)
 *        this is used to determine if a name from another source should overwrite or
 * not
 */
enum EGuessQuality
{
  GUESS_INVALID,   /// no valid value has been set yet
  GUESS_FALLBACK,  /// the guess is very basic and should only be used if no other
                   /// source is available
  GUESS_GOOD,      /// considered a good guess
  GUESS_META,      /// the value comes from meta data and is usually what the author
                   /// intended
  GUESS_PRESET,    /// the value comes from a previous install of the same data and
                   /// usually represents what the user chose before
  GUESS_USER       /// the user selection. Always overrules other sources
};

/**
 * Represents a value that may be set from different places. Each time the value is
 * changed a "quality" is specified to say how probable it is the value is the best
 * choice. Only the best choice should be used in the end but alternatives can be
 * queried. This class also allows a filter to be set. If a "guess" doesn't pass the
 * filter, it is ignored.
 */
template <typename T>
class GuessedValue
{
public:
public:
  /**
   * @brief default constructor
   */
  GuessedValue();

  /**
   * @brief constructor with initial value
   *
   * @param reference the initial value to set
   * @param quality quality of the guess
   */
  GuessedValue(const T& reference, EGuessQuality quality = GUESS_USER);

  /**
   * @brief
   *
   * @param reference
   * @return GuessedValue<T>
   */
  GuessedValue<T>& operator=(const GuessedValue<T>& reference);

  /**
   * install a filter function. This filter is applied on every update and can
   * refuse the update altogether or modify the value.
   * @param filterFunction the filter to apply
   */
  /**
   * @brief
   *
   * @param filterFunction
   */
  void setFilter(std::function<bool(T&)> filterFunction);

  /**
   * @brief
   *
   * @return operator const T
   */
  operator const T&() const { return m_Value; }

  /**
   * @brief
   *
   * @return T *operator ->
   */
  T* operator->() { return &m_Value; }
  /**
   * @brief
   *
   * @return const T *operator ->
   */
  /**
   * @brief
   *
   * @return const T *operator ->
   */
  const T* operator->() const { return &m_Value; }

  /**
   * @brief
   *
   * @param value
   * @return GuessedValue<T>
   */
  GuessedValue<T>& update(const T& value);
  /**
   * @brief
   *
   * @param value
   * @param quality
   * @return GuessedValue<T>
   */
  GuessedValue<T>& update(const T& value, EGuessQuality quality);

  /**
   * @brief
   *
   * @return const std::set<T>
   */
  const std::set<T>& variants() const { return m_Variants; }

private:
  T m_Value;               /**< TODO */
  std::set<T> m_Variants;  /**< TODO */
  EGuessQuality m_Quality; /**< TODO */
  std::function<bool(T&)> m_Filter;
};

template <typename T>
/**
 * @brief
 *
 * @param
 * @return bool
 */
bool nullFilter(T&)
{
  return true;
}

/**
 * @brief
 *
 */
template <typename T>
GuessedValue<T>::GuessedValue()
    : m_Value(), m_Quality(GUESS_INVALID), m_Filter(nullFilter<T>)
{}

/**
 * @brief
 *
 * @param reference
 * @param quality
 */
template <typename T>
GuessedValue<T>::GuessedValue(const T& reference, EGuessQuality quality)
    : m_Value(reference), m_Variants{reference}, m_Quality(quality),
      m_Filter(nullFilter<T>)
{}

/**
 * @brief
 *
 * @param reference
 * @return GuessedValue<T> &GuessedValue<T>
 */
template <typename T>
GuessedValue<T>& GuessedValue<T>::operator=(const GuessedValue<T>& reference)
{
  if (this != &reference) {
    if (reference.m_Quality >= m_Quality) {
      m_Value    = reference.m_Value;
      m_Quality  = reference.m_Quality;
      m_Filter   = reference.m_Filter;
      m_Variants = reference.m_Variants;
    }
  }
  return *this;
}

/**
 * @brief
 *
 * @param filterFunction
 */
template <typename T>
void GuessedValue<T>::setFilter(std::function<bool(T&)> filterFunction)
{
  m_Filter = filterFunction;
}

/**
 * @brief
 *
 * @param value
 * @return GuessedValue<T> &GuessedValue<T>
 */
template <typename T>
GuessedValue<T>& GuessedValue<T>::update(const T& value)
{
  T temp = value;
  if (m_Filter(temp)) {
    m_Variants.insert(temp);
    m_Value = temp;
  }
  return *this;
}

/**
 * @brief
 *
 * @param value
 * @param quality
 * @return GuessedValue<T> &GuessedValue<T>
 */
template <typename T>
GuessedValue<T>& GuessedValue<T>::update(const T& value, EGuessQuality quality)
{
  T temp = value;
  if (m_Filter(temp)) {
    m_Variants.insert(temp);
    if (quality >= m_Quality) {
      m_Value   = temp;
      m_Quality = quality;
    }
  }
  return *this;
}

}  // namespace MOBase

#endif  // GUESSEDVALUE_H
