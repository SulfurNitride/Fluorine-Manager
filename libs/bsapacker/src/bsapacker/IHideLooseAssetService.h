#ifndef IHIDELOOSEASSETSERVICE_H
#define IHIDELOOSEASSETSERVICE_H

#include <QDir>

namespace BsaPacker
{
	class IHideLooseAssetService
	{
	public:
		virtual ~IHideLooseAssetService() = default;
		virtual bool HideLooseAssets(const QDir& modDirectory) const = 0;
	};
}

#endif // IHIDELOOSEASSETSERVICE_H
