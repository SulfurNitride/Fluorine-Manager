#include <bsapacker/IModDtoFactory.h>
#include <gmock/gmock.h>

using namespace BsaPacker;

class MockModDtoFactory : public IModDtoFactory
{
public:
	MOCK_METHOD(std::unique_ptr<IModDto>, Create, (), (const));
};
