#ifndef PSETTINGS_H
#define PSETTINGS_H

#include <QSettings>
#include <QString>
#include <map>

/**
 * This is a small class that can be used to store INI settings in memory since
 * QSettings is a pain to use without an actual file.
 *
 * It is a much simpler structure since it stores everything as string.
 */
struct PSettings
{

  /**
   *
   */
  PSettings() = default;

public:  // Value read/write.
  /**
   * @brief Set the value of the given section/key.
   *
   * @param section The section of the value.
   * @param key The key of the value.
   * @param value The value to set.
   */
  void setValue(QString section, QString key, QString value)
  {
    m_Values[std::make_pair(section, key)] = value;
  }

  /**
   * @brief Return the value at the given section/key.
   *
   * @param section The section of the value.
   * @param key The key of the value.
   *
   * @return the corresponding value, or an empty string if the section/key does not
   * exist.
   */
  QString value(QString section, QString key) const
  {
    auto it = m_Values.find(std::make_pair(section, key));
    return it == m_Values.end() ? QString() : it->second;
  }

  /**
   * @brief Check if the given section/key exists in these settings.
   *
   * @param section The section of the value.
   * @param key The key of the value.
   *
   * @return true if the section/key exist.
   */
  bool hasValue(QString section, QString key) const
  {
    return m_Values.find(std::make_pair(section, key)) != m_Values.end();
  }

public:  // Output:
  /**
   * @brief Convert this PSettings to a string.
   *
   * @return a string representing the content of a valid INI file corresponding
   *     to this PSettings.
   */
  QString toString() const
  {
    QString result = "";
    QString cSection;
    for (auto& p : m_Values) {
      if (cSection != p.first.first) {
        if (!cSection.isEmpty()) {
          result += '\n';
        }
        cSection = p.first.first;
        result += "[" + cSection + "]\n";
      }
      result += p.first.second + "=" + p.second + "\n";
    }
    return result;
  }

  /**
   * @brief Update the given QSettings with all the value in this.
   *
   * @param settings The settings to update.
   */
  void update(QSettings& settings) const
  {
    for (auto& p : m_Values) {
      if (p.first.first == "General") {
        settings.setValue(p.first.second, p.second);
      } else {
        settings.setValue(p.first.first + "/" + p.first.second, p.second);
      }
    }
  }

private:
  // Map from <section, key> to value:
  std::map<std::pair<QString, QString>, QString> m_Values;
};

#endif  // !PSETTINGS_H
