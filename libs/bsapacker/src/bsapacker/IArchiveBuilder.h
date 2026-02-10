#ifndef IARCHIVEBUILDER_H
#define IARCHIVEBUILDER_H

#include <bsapacker/IEmitsValueChanged.h>
#include <libbsarch/bs_archive_auto.hpp>

namespace BsaPacker
{
	class IArchiveBuilder : public IEmitsValueChanged
	{
		Q_OBJECT
			Q_INTERFACES(BsaPacker::IEmitsValueChanged)

	public:
		virtual ~IArchiveBuilder() = default;
		virtual uint32_t setFiles() = 0;
		virtual void setShareData(bool) = 0;
		[[nodiscard]] virtual std::vector<std::unique_ptr<libbsarch::bs_archive_auto>> getArchives() = 0;
		[[nodiscard]] virtual uint32_t getFileCount() const = 0;
		[[nodiscard]] virtual QString getRootPath() const = 0;
	};
}  // namespace BsaPacker

#endif // IARCHIVEBUILDER_H
