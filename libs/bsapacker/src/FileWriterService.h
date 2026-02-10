#ifndef FILEWRITERSERVICE_H
#define FILEWRITERSERVICE_H

#include "bsapacker_global.h"
#include <bsapacker/IFileWriterService.h>

namespace BsaPacker
{
	class BSAPACKER_EXPORT FileWriterService : public IFileWriterService
	{
	public:
		FileWriterService() = default;
		bool Write(const std::string& path, const char* payload, const uint32_t size) const override;
	};
}
#endif // FILEWRITERSERVICE_H
