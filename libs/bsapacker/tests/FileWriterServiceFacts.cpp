#include "gtest/gtest.h"
#include "gmock/gmock.h"

#include "FileWriterService.h"

using namespace BsaPacker;

namespace BsaPackerTests
{
	class FileWriterServiceFacts : public ::testing::Test
	{
	protected:
		void SetUp() override {
		}

		void TearDown() override {
		}
	};

	TEST_F(FileWriterServiceFacts, Ctor_Always_Constructs)
	{
		EXPECT_NO_THROW(
			auto result = FileWriterService();
		);
	}
}
