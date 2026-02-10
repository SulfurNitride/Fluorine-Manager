#include <bsapacker/ISettingsService.h>
#include <gmock/gmock.h>

using namespace BsaPacker;

class MockSettingsService : public ISettingsService
{
public:
	MOCK_METHOD(QVariant, GetPluginSetting, (const QString&), (const, override));
};
