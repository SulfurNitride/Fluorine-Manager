#include <bsapacker/IArchiveBuilderFactory.h>
#include <gmock/gmock.h>

using namespace BsaPacker;

class MockArchiveBuilderFactory : public IArchiveBuilderFactory
{
public:
	MOCK_METHOD(std::vector<bsa_archive_type_e>, GetArchiveTypes, (const IModDto*), (const, override));
	MOCK_METHOD(std::unique_ptr<IArchiveBuilder>, Create, (bsa_archive_type_e, const IModDto*), (const, override));
};
