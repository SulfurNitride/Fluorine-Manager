#include <bsapacker/IDummyPluginServiceFactory.h>
#include <gmock/gmock.h>

using namespace BsaPacker;

class MockDummyPluginServiceFactory : public IDummyPluginServiceFactory
{
public:
	MOCK_METHOD(std::unique_ptr<IDummyPluginService>, Create, (), (const, override));
};
