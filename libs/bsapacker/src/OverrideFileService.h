#ifndef OVERRIDEFILESERVICE_H
#define OVERRIDEFILESERVICE_H

#include "bsapacker_global.h"
#include <bsapacker/IFileWriterService.h>
#include <bsapacker/IOverrideFileService.h>

namespace BsaPacker {
	class BSAPACKER_EXPORT OverrideFileService : public IOverrideFileService {
	public:
		OverrideFileService(const IFileWriterService* fileWriterService);
		bool CreateOverrideFile(const int nexusId,
			const QString& modPath,
			const QStringList& archiveNames) const override;

	private:
		const IFileWriterService* m_FileWriterService = nullptr;
	};
} // namespace BsaPacker

#endif // OVERRIDEFILESERVICE_H
