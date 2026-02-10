#ifndef IFILEWRITERSERVICE_H
#define IFILEWRITERSERVICE_H

#include <string>

namespace BsaPacker
{
	class IFileWriterService
	{
	public:
		virtual ~IFileWriterService() = default;
		virtual bool Write(const std::string&, const char*, uint32_t) const = 0;
	};
}

#endif // IFILEWRITERSERVICE_H
