// clang-format off

#ifndef CSHARP_UTILS_H
#define CSHARP_UTILS_H

#include <type_traits>
#include <string>

#include <QString>

#include <msclr\marshal_cppstd.h>

#using <System.dll>

namespace CSharp {

  /**
   * Handy functions.
   */
  inline std::string to_string(System::String^ value) {
    return msclr::interop::marshal_as<std::string>(value);
  }
  inline std::wstring to_wstring(System::String^ value) {
    return msclr::interop::marshal_as<std::wstring>(value);
  }
  inline QString to_qstring(System::String^ value) {
    msclr::interop::marshal_context ctx;
    return QString::fromWCharArray(ctx.marshal_as<const wchar_t*>(value));
  }

  template <class Str>
  inline System::String^ from_string(Str const& string) {
    return msclr::interop::marshal_as<System::String^>(string);
  }
  inline System::String^ from_string(QString const& string) {
    return msclr::interop::marshal_as<System::String^>(string.toStdWString().c_str());
  }

}

#endif
