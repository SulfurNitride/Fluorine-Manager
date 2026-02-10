#include <bsapacker/ArchiveAutoService.h>
#include <uibase/utility.h>
#include <QProgressDialog>
#include <QtConcurrent>
#include <QLabel>
#include <QDebug>

#include "NexusId.h"

#ifdef __linux__
#include <bsa_ffi.h>
#endif

namespace BsaPacker
{
	static const char* gameIdFromNexusId(const int nexusId)
	{
		switch (nexusId) {
		case NexusId::Morrowind: return "morrowind";
		case NexusId::Oblivion: return "oblivion";
		case NexusId::Fallout3: return "fo3";
		case NexusId::NewVegas: return "fonv";
		case NexusId::Skyrim:
		case NexusId::Enderal: return "skyrimle";
		case NexusId::SkyrimSE:
		case NexusId::EnderalSE: return "skyrimse";
		case NexusId::Fallout4: return "fo4-fo76";
		case NexusId::Starfield: return "starfield-v3";
		default: return "fo4-fo76";
		}
	}

	static int includeModeFromArchiveType(const bsa_archive_type_e type)
	{
		switch (type) {
		case bsa_archive_type_e::baFO4: return 1;     // non-dds
		case bsa_archive_type_e::baFO4dds: return 2;  // dds only
		case bsa_archive_type_e::baSF: return 1;      // non-dds
		case bsa_archive_type_e::baSFdds: return 2;   // dds only
		default: return 0;                            // all files
		}
	}

	bool ArchiveAutoService::CreateBSA(libbsarch::bs_archive_auto* archive,
									   const QString& archiveName,
									   const bsa_archive_type_e type,
									   const QString& sourceDir,
									   const int nexusId) const
	{
		const QString hostArchiveName = MOBase::normalizePathForHost(archiveName);
		const QString hostSourceDir = MOBase::normalizePathForHost(sourceDir);
		const char* gameId = gameIdFromNexusId(nexusId);
		const int includeMode = includeModeFromArchiveType(type);

		QProgressDialog savingDialog;
		savingDialog.setWindowFlags(savingDialog.windowFlags() & ~Qt::WindowCloseButtonHint);
		savingDialog.setWindowTitle(QObject::tr("Writing Archive"));
		savingDialog.setCancelButton(0);
		QLabel text;
		text.setText(QObject::tr("Writing %1").arg(hostArchiveName));
		savingDialog.setLabel(&text);
		savingDialog.setRange(0, 0);
		savingDialog.show();
		auto future = QtConcurrent::run([=]() -> bool {
#ifdef __linux__
			char* err = bsa_ffi_pack_dir_filtered(
				hostSourceDir.toUtf8().constData(),
				hostArchiveName.toUtf8().constData(),
				gameId,
				includeMode,
				nullptr,
				nullptr);
			if (err == nullptr) {
				qDebug() << "packed archive via bsa_ffi for" << hostArchiveName;
				return true;
			}

			qWarning() << "bsa_ffi primary pack failed for" << hostArchiveName
					   << ":" << QString::fromUtf8(err)
					   << "- falling back to libbsarch";
			bsa_ffi_string_free(err);
#endif
			try {
				archive->save_to_disk(hostArchiveName.toStdString());
			} catch (std::exception&) {
				return false;
			}
			return true;
			});
		while (!future.isFinished())
		{
			QCoreApplication::processEvents();
		}
		savingDialog.hide();

		if (future.result()) {
			return true;
		}

		return false;
	}
} // namespace BsaPacker
