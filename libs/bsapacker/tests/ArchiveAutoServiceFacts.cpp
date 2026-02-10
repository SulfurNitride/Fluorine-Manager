#include "gtest/gtest.h"
#include "gmock/gmock.h"

#include <bsapacker/ArchiveAutoService.h>

using namespace BsaPacker;

namespace BsaPackerTests
{
	class ArchiveAutoServiceFacts : public ::testing::Test
	{
	protected:
		void SetUp() override {
		}

		void TearDown() override {
		}
	};

	TEST_F(ArchiveAutoServiceFacts, Ctor_Always_Constructs)
	{
		EXPECT_NO_THROW(
			auto result = ArchiveAutoService();
		);
	}
}
