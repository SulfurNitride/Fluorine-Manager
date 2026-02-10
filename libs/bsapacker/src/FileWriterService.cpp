#include "FileWriterService.h"

#include <fstream>

namespace BsaPacker
{
	bool FileWriterService::Write(const std::string& path, const char* payload, const uint32_t size) const
	{
		std::ofstream file;
		file.open(path, std::ios::out | std::ios::binary);
		if (file.is_open()) {
			file.write(reinterpret_cast<const char*>(payload), size);
			file.close();
			return true;
		}
		return false;
	}
}
