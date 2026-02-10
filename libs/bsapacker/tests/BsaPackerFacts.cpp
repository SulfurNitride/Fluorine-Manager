#include "gtest/gtest.h"
#include "gmock/gmock.h"

#include "BsaPacker.h"
#include "MockOrganizer.h"

using namespace BsaPacker;
using ::testing::NaggyMock;
using ::testing::Return;
using ::testing::Eq;

namespace BsaPackerTests
{
	class BsaPackerFacts : public ::testing::Test
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

	TEST_F(BsaPackerFacts, CtorAndInit_Always_Constructs)
	{
		auto result = Bsa_Packer();
		EXPECT_NO_THROW(result.init(naggyMockOrganizer));
	}

}
