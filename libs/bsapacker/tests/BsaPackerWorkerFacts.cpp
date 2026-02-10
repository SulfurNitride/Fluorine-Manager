#include "gtest/gtest.h"
#include "gmock/gmock.h"

#include "BsaPackerWorker.h"
#include "MockSettingsService.h"
#include "MockModDtoFactory.h"
#include "MockArchiveBuilderFactory.h"
#include "MockDummyPluginServiceFactory.h"
#include "MockArchiveAutoService.h"
#include "MockHideLooseAssetService.h"
#include "MockArchiveNameService.h"
#include "MockOverrideFileService.h"

using namespace BsaPacker;
using ::testing::NaggyMock;

namespace BsaPackerTests
{
	class BsaPackerWorkerFacts : public ::testing::Test
	{
	protected:
		void SetUp() override
		{
			naggyMockSettingsService = new NaggyMock<MockSettingsService>();
			naggyMockModDtoFactory = new NaggyMock<MockModDtoFactory>();
			naggyMockArchiveBuilderFactory = new NaggyMock<MockArchiveBuilderFactory>();
			naggyMockArchiveAutoService = new NaggyMock<MockArchiveAutoService>();
			naggyMockDummyPluginServiceFactory = new NaggyMock<MockDummyPluginServiceFactory>();
			naggyMockHideLooseAssetService = new NaggyMock<MockHideLooseAssetService>();
			naggyMockArchiveNameService = new NaggyMock<MockArchiveNameService>();
			naggyOverrideFileService = new NaggyMock<MockOverrideFileService>();
		}

		void TearDown() override
		{
			delete naggyMockSettingsService;
			delete naggyMockModDtoFactory;
			delete naggyMockArchiveBuilderFactory;
			delete naggyMockArchiveAutoService;
			delete naggyMockDummyPluginServiceFactory;
			delete naggyMockHideLooseAssetService;
			delete naggyMockArchiveNameService;
		}

		NaggyMock<MockSettingsService> *naggyMockSettingsService;
		NaggyMock<MockModDtoFactory> *naggyMockModDtoFactory;
		NaggyMock<MockArchiveBuilderFactory> *naggyMockArchiveBuilderFactory;
		NaggyMock<MockArchiveAutoService> *naggyMockArchiveAutoService;
		NaggyMock<MockDummyPluginServiceFactory> *naggyMockDummyPluginServiceFactory;
		NaggyMock<MockHideLooseAssetService> *naggyMockHideLooseAssetService;
		NaggyMock<MockArchiveNameService> *naggyMockArchiveNameService;
		NaggyMock<MockOverrideFileService> *naggyOverrideFileService;
	};

	TEST_F(BsaPackerWorkerFacts, Ctor_Always_Constructs)
	{
		EXPECT_NO_THROW(BsaPackerWorker(
			naggyMockSettingsService,
			naggyMockModDtoFactory,
			naggyMockArchiveBuilderFactory,
			naggyMockArchiveAutoService,
			naggyMockDummyPluginServiceFactory,
			naggyMockHideLooseAssetService,
			naggyMockArchiveNameService,
			naggyOverrideFileService));
	}
}
