#include "gtest/gtest.h"
#include "gmock/gmock.h"

#include <bsapacker/GeneralArchiveBuilder.h>
#include "MockArchiveBuilderHelper.h"

using namespace BsaPacker;
using ::testing::NaggyMock;

namespace BsaPackerTests
{
	class GeneralArchiveBuilderFacts : public ::testing::Test
	{
	protected:
		void SetUp() override {
			naggyMockArchiveBuilderHelper = new NaggyMock<MockArchiveBuilderHelper>();
		}

		void TearDown() override {
			delete naggyMockArchiveBuilderHelper;
		}

		NaggyMock<MockArchiveBuilderHelper>* naggyMockArchiveBuilderHelper;
	};

	TEST_F(GeneralArchiveBuilderFacts, Ctor_Always_Constructs)
	{
		EXPECT_NO_THROW(
			auto result = GeneralArchiveBuilder(
				naggyMockArchiveBuilderHelper,
				QDir(),
				baNone);
		);
	}
}
