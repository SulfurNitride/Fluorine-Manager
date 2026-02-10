#ifndef MODORGANIZER_ENVDUMP_INCLUDED
#define MODORGANIZER_ENVDUMP_INCLUDED

namespace env
{

enum class CoreDumpTypes
{
  None,
  Mini,
  Data,
  Full
};

CoreDumpTypes coreDumpTypeFromString(const std::string& s);
std::string toString(CoreDumpTypes type);

// creates a minidump file for this process
//
#ifdef _WIN32
bool coredump(const wchar_t* dir, CoreDumpTypes type);
#else
bool coredump(const char* dir, CoreDumpTypes type);
#endif

// finds another process with the same name as this one and creates a minidump
// file for it
//
bool coredumpOther(CoreDumpTypes type);

}  // namespace env

#endif  // MODORGANIZER_ENVDUMP_INCLUDED
