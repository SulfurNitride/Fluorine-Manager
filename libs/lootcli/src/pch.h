#ifdef _MSC_VER
#pragma warning(disable : 4251)  // neds to have dll-interface
#pragma warning(disable : 4355)  // this used in initializer list
#pragma warning(disable : 4371)  // layout may have changed
#pragma warning(disable : 4514)  // unreferenced inline function removed
#pragma warning(disable : 4571)  // catch semantics changed
#pragma warning(disable : 4619)  // no warning X
#pragma warning(disable : 4623)  // default constructor deleted
#pragma warning(disable : 4625)  // copy constructor deleted
#pragma warning(disable : 4626)  // copy assignment operator deleted
#pragma warning(disable : 4710)  // function not inlined
#pragma warning(disable : 4820)  // padding
#pragma warning(disable : 4866)  // left-to-right evaluation order
#pragma warning(disable : 4868)  // left-to-right evaluation order
#pragma warning(disable : 5026)  // move constructor deleted
#pragma warning(disable : 5027)  // move assignment operator deleted
#pragma warning(disable : 5045)  // spectre mitigation

#pragma warning(push, 3)
#pragma warning(disable : 4365)  // signed/unsigned mismatch
#pragma warning(disable : 4774)  // bad format string
#pragma warning(disable : 4946)  // reinterpret_cast used between related classes
#pragma warning(disable : 4800)  // implicit conversion
#endif

// std
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <locale>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

// qt
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

// boost
#include <boost/algorithm/string.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/locale.hpp>

// loot
#include <loot/api.h>
#include <loot/enum/game_type.h>

// third-party
#include <toml++/toml.h>

#ifdef _WIN32
// windows
#define WIN32_LEAN_AND_MEAN
#include <Shlobj.h>
#include <Windows.h>
#include <fcntl.h>
#include <io.h>
#include <shellapi.h>
#else
// Linux equivalents
#include <cstdlib>
#include <unistd.h>
#endif

#ifdef _MSC_VER
#undef _SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING
#pragma warning(pop)
#endif
