#ifndef TEXTUREARCHIVEBUILDER_H
#define TEXTUREARCHIVEBUILDER_H

#include "bsapacker_global.h"
#include <bsapacker/IArchiveBuilder.h>
#include <bsapacker/IArchiveBuilderHelper.h>
#include <QDir>

namespace BsaPacker
{
	class BSAPACKER_EXPORT TextureArchiveBuilder : public IArchiveBuilder
	{
		Q_OBJECT
			Q_INTERFACES(BsaPacker::IEmitsValueChanged)

	public:
		TextureArchiveBuilder(const IArchiveBuilderHelper* archiveBuilderHelper, const QDir&, const bsa_archive_type_t& type);
		~TextureArchiveBuilder() override = default;

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

		static void DDSCallback(bsa_archive_t archive, const wchar_t* file_path, bsa_dds_info_t* dds_info, void* context);
	};
} // namespace BsaPacker

#endif // TEXTUREARCHIVEBUILDER_H
