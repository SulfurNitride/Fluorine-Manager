#ifndef TEXTURELESSARCHIVEBUILDER_H
#define TEXTURELESSARCHIVEBUILDER_H

#include "bsapacker_global.h"
#include <bsapacker/IArchiveBuilder.h>
#include <bsapacker/IArchiveBuilderHelper.h>
#include <QDir>

namespace BsaPacker
{
	class BSAPACKER_EXPORT TexturelessArchiveBuilder : public IArchiveBuilder
	{
		Q_OBJECT
			Q_INTERFACES(BsaPacker::IEmitsValueChanged)

	public:
		TexturelessArchiveBuilder(const IArchiveBuilderHelper* archiveBuilderHelper, const QDir& rootDir, const bsa_archive_type_t& type);
		~TexturelessArchiveBuilder() override = default;

		uint32_t setFiles() override;
		void setShareData(bool) override;
		[[nodiscard]] std::vector<std::unique_ptr<libbsarch::bs_archive_auto>> getArchives() override;
		[[nodiscard]] uint32_t getFileCount() const override;
		[[nodiscard]] QString getRootPath() const override;

	public Q_SLOTS:
		void cancel() override;

	private:
		const IArchiveBuilderHelper* m_ArchiveBuilderHelper = nullptr;
		std::vector<std::unique_ptr<libbsarch::bs_archive_auto>> m_Archives;
		const bsa_archive_type_t m_ArchiveType;
		bool m_Cancelled;
		QDir m_RootDirectory;
		const static qint64 SIZE_LIMIT;
	};
} // namespace BsaPacker

#endif // TEXTURELESSARCHIVEBUILDER_H
