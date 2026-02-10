#include "gtest/gtest.h"
#include "gmock/gmock.h"

#include <bsapacker/TexturelessArchiveBuilder.h>
#include "MockArchiveBuilderHelper.h"

using namespace BsaPacker;
using ::testing::NaggyMock;

namespace BsaPackerTests
{
	class TexturelessArchiveBuilderFacts : public ::testing::Test
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

	TEST_F(TexturelessArchiveBuilderFacts, Ctor_Always_Constructs)
	{
		EXPECT_NO_THROW(
			auto result = TexturelessArchiveBuilder(
				naggyMockArchiveBuilderHelper,
				QDir(),
				baNone);
		);
	}
}
