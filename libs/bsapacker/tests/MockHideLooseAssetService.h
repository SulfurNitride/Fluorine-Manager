#include <bsapacker/IHideLooseAssetService.h>
#include <gmock/gmock.h>

using namespace BsaPacker;

class MockHideLooseAssetService : public IHideLooseAssetService
{
public:
	MOCK_METHOD(bool, HideLooseAssets, (const QDir&), (const, override));
};
