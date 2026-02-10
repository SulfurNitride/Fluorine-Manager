#pragma once

using namespace cli;

#include <QString>

QString toQString(System::String^ string);

System::String^ toDotNetString(const QString& qString);
