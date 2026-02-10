#pragma once

#include "../hookcontext.h"

typedef std::map<HANDLE, std::wstring> SearchHandleMap;

// maps handles opened for searching to the original search path, which is
// necessary if the handle creation was rerouted
DATA_ID(SearchHandles);
