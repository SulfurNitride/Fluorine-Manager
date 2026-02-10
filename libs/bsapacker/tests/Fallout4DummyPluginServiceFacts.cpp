#include "gtest/gtest.h"
#include "gmock/gmock.h"

#include "Fallout4DummyPluginService.h"
#include "MockFileWriterService.h"
#include "MockDummyPluginLogic.h"

using namespace BsaPacker;
using ::testing::NaggyMock;

namespace BsaPackerTests
{
	class Fallout4DummyPluginServiceFacts : public ::testing::Test
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

	TEST_F(Fallout4DummyPluginServiceFacts, Ctor_Always_Constructs)
	{
		EXPECT_NO_THROW(
			auto result = Fallout4DummyPluginService(
				naggyMockFileWriterService,
				naggyMockDummyPluginLogic
			);
		);
	}
}
