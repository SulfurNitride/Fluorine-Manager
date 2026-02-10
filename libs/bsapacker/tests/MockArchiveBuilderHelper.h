#include <bsapacker/IArchiveBuilderHelper.h>
#include <gmock/gmock.h>

using namespace BsaPacker;

class MockArchiveBuilderHelper : public IArchiveBuilderHelper
{
public:
	MOCK_METHOD(bool, isFileIgnorable, (const std::filesystem::path&, const std::vector<std::filesystem::path::string_type>&), (const, override));
	MOCK_METHOD(bool, isIncompressible, (const std::filesystem::path&), (const, override));
	MOCK_METHOD(bool, isExtensionBlacklisted, (const std::filesystem::path&), (const, override));
	MOCK_METHOD(uint32_t, getFileCount, (const std::filesystem::path&), (const, override));
	MOCK_METHOD(std::vector<std::filesystem::path::string_type>, getRootDirectoryFilenames, (const std::filesystem::path&), (const, override));
	MOCK_METHOD(bool, doesPathContainFiles, (const std::filesystem::path&, const std::vector<std::filesystem::path::string_type>&), (const, override));
};
