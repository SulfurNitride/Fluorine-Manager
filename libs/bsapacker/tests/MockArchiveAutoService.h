#include <bsapacker/IArchiveAutoService.h>
#include <gmock/gmock.h>

using namespace BsaPacker;

class MockArchiveAutoService : public IArchiveAutoService
{
public:
	MOCK_METHOD(bool, CreateBSA, (libbsarch::bs_archive_auto *, const QString &, bsa_archive_type_e), (const, override));
};
