#include "gtest/gtest.h"
#include "gmock/gmock.h"

#include "SkyrimSEDummyPluginService.h"
#include "MockFileWriterService.h"
#include "MockDummyPluginLogic.h"

using namespace BsaPacker;
using ::testing::NaggyMock;

namespace BsaPackerTests
{
	class SkyrimSEDummyPluginServiceFacts : public ::testing::Test
	{
	protected:
		void SetUp() override {
			naggyMockFileWriterService = new NaggyMock<MockFileWriterService>();
			naggyMockDummyPluginLogic = new NaggyMock<MockDummyPluginLogic>();
		}

		void TearDown() override {
			delete naggyMockFileWriterService;
			delete naggyMockDummyPluginLogic;
		}

		NaggyMock<MockFileWriterService>* naggyMockFileWriterService;
		NaggyMock<MockDummyPluginLogic>* naggyMockDummyPluginLogic;
	};

	TEST_F(SkyrimSEDummyPluginServiceFacts, Ctor_Always_Constructs)
	{
		EXPECT_NO_THROW(
			auto result = SkyrimSEDummyPluginService(
				naggyMockFileWriterService,
				naggyMockDummyPluginLogic
			);
		);
	}
}
