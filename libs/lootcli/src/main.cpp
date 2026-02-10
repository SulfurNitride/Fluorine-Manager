#include "lootthread.h"
#include <lootcli/lootcli.h>

using namespace std;

template <typename T>
T getParameter(const std::vector<std::string>& arguments, const std::string& key)
{
  auto iter = std::find(arguments.begin(), arguments.end(), std::string("--") + key);
  if ((iter != arguments.end()) && ((iter + 1) != arguments.end())) {
    return boost::lexical_cast<T>(*(iter + 1));
  } else {
    throw std::runtime_error(std::string("argument missing " + key));
  }
}

template <>
bool getParameter<bool>(const std::vector<std::string>& arguments,
                        const std::string& key)
{
  auto iter = std::find(arguments.begin(), arguments.end(), std::string("--") + key);
  if (iter != arguments.end()) {
    return true;
  } else {
    return false;
  }
}

template <typename T>
T getOptionalParameter(const std::vector<std::string>& arguments,
                       const std::string& key, T def)
{
  try {
    return getParameter<T>(arguments, key);
  } catch (std::runtime_error&) {
    return def;
  }
}

loot::LogLevel getLogLevel(const std::vector<std::string>& arguments)
{
  const auto s     = getOptionalParameter<std::string>(arguments, "logLevel", "");
  const auto level = lootcli::logLevelFromString(s);

  return lootcli::toLootLogLevel(level);
}

#ifdef _WIN32
int wWinMain(HINSTANCE, HINSTANCE, LPTSTR, int)
{
  _setmode(_fileno(stdout), _O_BINARY);
  setlocale(LC_ALL, "en.UTF-8");

  std::vector<std::string> arguments;
  int argc;
  LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);

  if (argv) {
    for (int i = 0; i < argc; ++i) {
      size_t num_converted;
      std::vector<char> arg(wcslen(argv[i]) * sizeof(wchar_t) + 1);

      wcstombs_s(&num_converted, &(arg[0]), arg.size(), argv[i], arg.size() - 1);

      arguments.push_back(&(arg[0]));
    }
  }
#else
int main(int argc, char* argv[])
{
  setlocale(LC_ALL, "en_US.UTF-8");

  std::vector<std::string> arguments;
  for (int i = 0; i < argc; ++i) {
    arguments.push_back(argv[i]);
  }
#endif

  // design rationale: this was designed to have the actual loot stuff run in a separate
  // thread. That turned out to be unnecessary atm.

  try {
    lootcli::LOOTWorker worker;

    worker.setUpdateMasterlist(!getParameter<bool>(arguments, "skipUpdateMasterlist"));
    worker.setGame(getParameter<std::string>(arguments, "game"));
    worker.setGamePath(getParameter<std::string>(arguments, "gamePath"));
    worker.setPluginListPath(getParameter<std::string>(arguments, "pluginListPath"));
    worker.setOutput(getParameter<std::string>(arguments, "out"));
    worker.setLogLevel(getLogLevel(arguments));

    const auto lang = getOptionalParameter<std::string>(arguments, "language", "");
    if (!lang.empty()) {
      worker.setLanguageCode(lang);
    }

    return worker.run();
  } catch (const std::exception& e) {
    std::cerr << "Error: " << e.what();
    return 1;
  }
}
