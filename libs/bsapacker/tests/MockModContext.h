#include <bsapacker/IModContext.h>
#include <gmock/gmock.h>

using namespace BsaPacker;

class MockModContext : public IModContext
{
public:
	MOCK_METHOD(QString, GetAbsoluteModPath, (const QString& modName), (const, override));
	MOCK_METHOD(int, GetNexusId, (), (const, override));
	MOCK_METHOD(QStringList, GetPlugins, (const QDir& modDirectory), (const, override));
	MOCK_METHOD(QStringList, GetValidMods, (), (const, override));
};
