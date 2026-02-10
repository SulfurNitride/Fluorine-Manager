#include "gtest/gtest.h"
#include "gmock/gmock.h"

#include "DummyPluginServiceFactory.h"
#include "MockModContext.h"
#include "MockFileWriterService.h"
#include "MockDummyPluginLogic.h"

using namespace BsaPacker;
using ::testing::NaggyMock;

namespace BsaPackerTests
{
	class DummyPluginServiceFactoryFacts : public ::testing::Test
	{
	protected:
		void SetUp() override {
			naggyMockModContext = new NaggyMock<MockModContext>();
			naggyMockFileWriterService = new NaggyMock<MockFileWriterService>();
			naggyMockDummyPluginLogic = new NaggyMock<MockDummyPluginLogic>();
		}

		void TearDown() override {
			delete naggyMockModContext;
			delete naggyMockFileWriterService;
			delete naggyMockDummyPluginLogic;
		}

		NaggyMock<MockModContext>* naggyMockModContext;
		NaggyMock<MockFileWriterService>* naggyMockFileWriterService;
		NaggyMock<MockDummyPluginLogic>* naggyMockDummyPluginLogic;
	};

	TEST_F(DummyPluginServiceFactoryFacts, Ctor_Always_Constructs)
	{
		EXPECT_NO_THROW(
			auto result = DummyPluginServiceFactory(
				naggyMockModContext,
				naggyMockFileWriterService,
				naggyMockDummyPluginLogic
			);
		);
	}
}
