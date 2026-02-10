#include "gtest/gtest.h"
#include "gmock/gmock.h"

#include "ModContext.h"
#include "MockOrganizer.h"

using namespace BsaPacker;
using ::testing::NaggyMock;

namespace BsaPackerTests
{
	class ModContextFacts : public ::testing::Test
	{
	protected:
		void SetUp() override {
			naggyMockOrganizer = new NaggyMock<MockOrganizer>();
		}

		void TearDown() override {
			delete naggyMockOrganizer;
		}

		NaggyMock<MockOrganizer>* naggyMockOrganizer;
	};

	TEST_F(ModContextFacts, Ctor_Always_Constructs)
	{
		EXPECT_NO_THROW(
			auto result = ModContext(
				naggyMockOrganizer);
		);
	}
}
