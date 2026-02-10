#ifndef ENV_WINDOWS_H
#define ENV_WINDOWS_H

#include <QString>
#include <optional>

namespace env
{

// a variety of information on the operating system
//
class WindowsInfo
{
public:
  struct Version
  {
#ifdef _WIN32
    DWORD major = 0, minor = 0, build = 0;
#else
    uint32_t major = 0, minor = 0, build = 0;
#endif

    QString toString() const
    {
      return QString("%1.%2.%3").arg(major).arg(minor).arg(build);
    }

    friend bool operator==(const Version& a, const Version& b)
    {
      return a.major == b.major && a.minor == b.minor && a.build == b.build;
    }

    friend bool operator!=(const Version& a, const Version& b) { return !(a == b); }
  };

  struct Release
  {
    // the BuildLab entry from the registry (Windows) or distro name (Linux),
    // may be empty
    QString buildLab;

    // release ID such as 1809 (Windows) or VERSION_ID (Linux), may be empty
    QString ID;

    // some sub-build number, may be empty
#ifdef _WIN32
    DWORD UBR;
#else
    uint32_t UBR;
#endif

    Release() : UBR(0) {}
  };

  WindowsInfo();

  // tries to guess whether this process is running in compatibility mode
  //
  bool compatibilityMode() const;

  // returns the OS version
  //
  const Version& reportedVersion() const;

  // tries to guess the real version, can be empty
  //
  const Version& realVersion() const;

  // various information about the current release
  //
  const Release& release() const;

  // whether this process is running as administrator/root, may be empty if the
  // information is not available
  std::optional<bool> isElevated() const;

  // returns a string with all the above information on one line
  //
  QString toString() const;

private:
  Version m_reported, m_real;
  Release m_release;
  std::optional<bool> m_elevated;

#ifdef _WIN32
  // uses RtlGetVersion() to get the version number as reported by Windows
  //
  Version getReportedVersion(HINSTANCE ntdll) const;

  // uses RtlGetNtVersionNumbers() to get the real version number
  //
  Version getRealVersion(HINSTANCE ntdll) const;
#else
  // uses uname() to get the kernel version
  //
  Version getKernelVersion() const;
#endif

  // gets various information from the registry (Windows) or /etc/os-release (Linux)
  //
  Release getRelease() const;

  // gets whether the process is elevated
  //
  std::optional<bool> getElevated() const;
};

}  // namespace env

#endif  // ENV_WINDOWS_H
