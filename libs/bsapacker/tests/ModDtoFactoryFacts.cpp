#include "gtest/gtest.h"
#include "gmock/gmock.h"

#include <bsapacker/ModDtoFactory.h>
#include "MockModContext.h"
#include "MockPackerDialog.h"

using namespace BsaPacker;
using ::testing::NaggyMock;
using ::testing::Return;

namespace BsaPackerTests
{
	class ModDtoFactoryFacts : public ::testing::Test
	{
	protected:
		void SetUp() override {
			naggyMockModContext = new NaggyMock<MockModContext>();
			naggyMockPackerDialog = new NaggyMock<MockPackerDialog>();
		}

		void TearDown() override {
			delete naggyMockModContext;
			delete naggyMockPackerDialog;
		}

		NaggyMock<MockModContext>* naggyMockModContext;
		NaggyMock<MockPackerDialog>* naggyMockPackerDialog;
	};

	TEST_F(ModDtoFactoryFacts, Ctor_Always_Constructs)
	{
		EXPECT_NO_THROW(ModDtoFactory(
			naggyMockModContext,
			naggyMockPackerDialog));
	}

	TEST_F(ModDtoFactoryFacts, Create_WhenGameIsNewVegas_ReturnsNewVegasIdInDto)
	{
		// Arrange
		auto sut = ModDtoFactory(
			naggyMockModContext,
			naggyMockPackerDialog);
		ON_CALL(*naggyMockModContext, GetNexusId())
			.WillByDefault(Return(1));
		ON_CALL(*naggyMockPackerDialog, Exec())
			.WillByDefault(Return(QDialog::DialogCode::Accepted));

		auto result = sut.Create();

		EXPECT_EQ(1, result->NexusId());
	}
}
