#ifndef ESPEXCEPTIONS_H
#define ESPEXCEPTIONS_H

#include <stdexcept>

namespace ESP
{

class InvalidRecordException : public std::runtime_error
{
public:
  InvalidRecordException(const std::string& message) : std::runtime_error(message) {}
};

class InvalidFileException : public std::runtime_error
{
public:
  InvalidFileException(const std::string& message) : std::runtime_error(message) {}
};

}  // namespace ESP

#endif  // ESPEXCEPTIONS_H
