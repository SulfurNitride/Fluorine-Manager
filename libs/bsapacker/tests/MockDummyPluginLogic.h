#include <bsapacker/IDummyPluginLogic.h>
#include "gmock/gmock.h"

using namespace BsaPacker;

class MockDummyPluginLogic : public IDummyPluginLogic
{
public:
	MOCK_METHOD(bool, canCreateDummyESP, (const QString &, const bsa_archive_type_e), (const, override));
	MOCK_METHOD(bool, canCreateDummyESL, (const QString &, const bsa_archive_type_e), (const, override));
};
