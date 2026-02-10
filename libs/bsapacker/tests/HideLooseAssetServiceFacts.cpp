#include "gtest/gtest.h"
#include "gmock/gmock.h"

#include "HideLooseAssetService.h"
#include "MockSettingsService.h"

using namespace BsaPacker;
using ::testing::NaggyMock;

namespace BsaPackerTests
{
	class HideLooseAssetServiceFacts : public ::testing::Test
	{
	protected:
		void SetUp() override {
			naggyMockSettingsService = new NaggyMock<MockSettingsService>();
		}

		void TearDown() override {
			delete naggyMockSettingsService;
		}

		NaggyMock<MockSettingsService>* naggyMockSettingsService;
	};

	TEST_F(HideLooseAssetServiceFacts, Ctor_Always_Constructs)
	{
		EXPECT_NO_THROW(
			auto result = HideLooseAssetService(
				naggyMockSettingsService);
		);
	}
}
