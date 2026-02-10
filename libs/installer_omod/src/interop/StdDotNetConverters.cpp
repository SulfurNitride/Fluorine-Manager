#include "stdDotNetConverters.h"

#include <stdexcept>

#include <msclr\marshal_cppstd.h>

std::wstring toWString(System::String^ string)
{
  if (string)
    return msclr::interop::marshal_as<std::wstring>(string);
  return L"null .NET string";
}

System::String^ toDotNetString(const std::wstring& wString)
{
  return msclr::interop::marshal_as<System::String^>(wString);
}

std::string toUTF8String(System::String^ string)
{
  if (string)
  {
    array<unsigned char>^ utf8 = System::Text::Encoding::UTF8->GetBytes(string);
    std::string stdstring;
    stdstring.resize(utf8->Length);
    System::Runtime::InteropServices::Marshal::Copy(utf8, 0, System::IntPtr(stdstring.data()), utf8->Length);
    return stdstring;
  }
  return "null .NET string";
}

std::exception toStdException(System::Exception^ exception)
{
  return std::runtime_error("Unhanded .NET Exception: " + toUTF8String(exception->ToString()));
}
