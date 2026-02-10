#include <bsapacker/IPackerDialog.h>
#include <gmock/gmock.h>

using namespace BsaPacker;

class MockPackerDialog : public IPackerDialog
{
public:
	MOCK_METHOD(bool, IsNewFilename, (), (const, override));
	MOCK_METHOD(QString, SelectedMod, (), (const, override));
	MOCK_METHOD(void, RefreshModList, (), (override));
	MOCK_METHOD(QString, SelectedPluginItem, (), (const, override));
	MOCK_METHOD(void, UpdateNameList, (const QString&), (override));
	MOCK_METHOD(void, RefreshSelectedName, (), (override));
	MOCK_METHOD(int, Exec, (), (override));
	MOCK_METHOD(void, RefreshOkButton, (), (override));
};
