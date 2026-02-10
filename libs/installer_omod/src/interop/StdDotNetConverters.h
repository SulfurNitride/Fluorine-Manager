#pragma once

using namespace cli;

#include <string>

std::wstring toWString(System::String^ string);

std::string toUTF8String(System::String^ string);

System::String^ toDotNetString(const std::wstring& wString);

std::exception toStdException(System::Exception^ exception);
