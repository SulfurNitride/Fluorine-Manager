#include "gtest/gtest.h"
#include "gmock/gmock.h"

#include <bsapacker/ArchiveBuilderFactory.h>
#include "MockArchiveBuilderHelper.h"
#include "ModDto.h"
#include "NexusId.h"
#include <bsapacker/NullArchiveBuilder.h>
#include <bsapacker/GeneralArchiveBuilder.h>
#include <bsapacker/TexturelessArchiveBuilder.h>
#include <bsapacker/TextureArchiveBuilder.h>
#include <typeindex>

using namespace BsaPacker;
using ::testing::NaggyMock;

namespace BsaPackerTests
{
	class ArchiveBuilderFactoryFacts : public ::testing::Test
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

	TEST_F(ArchiveBuilderFactoryFacts, Ctor_Always_Constructs)
	{
		EXPECT_NO_THROW(
			auto sut = ArchiveBuilderFactory(naggyMockArchiveBuilderHelper);
		);
	}

	TEST_F(ArchiveBuilderFactoryFacts, GetArchiveTypes_WhenNexusIdIsMorrowind_ReturnsbaTES3)
	{
		// Arrange
		auto sut = ArchiveBuilderFactory(naggyMockArchiveBuilderHelper);
		auto dto = ModDto(NexusId::Morrowind, "", "", "");

		// Act
		auto result = sut.GetArchiveTypes(&dto);

		// Assert
		EXPECT_EQ(bsa_archive_type_e::baTES3, result.at(0));
	}

	TEST_F(ArchiveBuilderFactoryFacts, GetArchiveTypes_WhenNexusIdIsOblivion_ReturnsbaTES4)
	{
		// Arrange
		auto sut = ArchiveBuilderFactory(naggyMockArchiveBuilderHelper);
		auto dto = ModDto(NexusId::Oblivion, "", "", "");

		// Act
		auto result = sut.GetArchiveTypes(&dto);

		// Assert
		EXPECT_EQ(bsa_archive_type_e::baTES4, result.at(0));
	}

	TEST_F(ArchiveBuilderFactoryFacts, GetArchiveTypes_WhenNexusIdIsFallout3_ReturnsbaFO3)
	{
		// Arrange
		auto sut = ArchiveBuilderFactory(naggyMockArchiveBuilderHelper);
		auto dto = ModDto(NexusId::Fallout3, "", "", "");

		// Act
		auto result = sut.GetArchiveTypes(&dto);

		// Assert
		EXPECT_EQ(bsa_archive_type_e::baFO3, result.at(0));
	}

	TEST_F(ArchiveBuilderFactoryFacts, GetArchiveTypes_WhenNexusIdIsNewVegas_ReturnsbaFO3)
	{
		// Arrange
		auto sut = ArchiveBuilderFactory(naggyMockArchiveBuilderHelper);
		auto dto = ModDto(NexusId::NewVegas, "", "", "");

		// Act
		auto result = sut.GetArchiveTypes(&dto);

		// Assert
		EXPECT_EQ(bsa_archive_type_e::baFO3, result.at(0));
	}

	TEST_F(ArchiveBuilderFactoryFacts, GetArchiveTypes_WhenNexusIdIsSkyrim_ReturnsbaFO3)
	{
		// Arrange
		auto sut = ArchiveBuilderFactory(naggyMockArchiveBuilderHelper);
		auto dto = ModDto(NexusId::Skyrim, "", "", "");

		// Act
		auto result = sut.GetArchiveTypes(&dto);

		// Assert
		EXPECT_EQ(bsa_archive_type_e::baFO3, result.at(0));
	}

	TEST_F(ArchiveBuilderFactoryFacts, GetArchiveTypes_WhenNexusIdIsSkyrimSE_ReturnsbaSSE)
	{
		// Arrange
		auto sut = ArchiveBuilderFactory(naggyMockArchiveBuilderHelper);
		auto dto = ModDto(NexusId::SkyrimSE, "", "", "");

		// Act
		auto result = sut.GetArchiveTypes(&dto);

		// Assert
		EXPECT_EQ(bsa_archive_type_e::baSSE, result.at(0));
	}

	TEST_F(ArchiveBuilderFactoryFacts, GetArchiveTypes_WhenNexusIdIsFallout4_ReturnsbaFO4andbaFO4dds)
	{
		// Arrange
		auto sut = ArchiveBuilderFactory(naggyMockArchiveBuilderHelper);
		auto dto = ModDto(NexusId::Fallout4, "", "", "");

		// Act
		auto result = sut.GetArchiveTypes(&dto);

		// Assert
		EXPECT_EQ(bsa_archive_type_e::baFO4, result.at(0));
		EXPECT_EQ(bsa_archive_type_e::baFO4dds, result.at(1));
	}

	TEST_F(ArchiveBuilderFactoryFacts, Create_WhenArchiveTypeNone_ReturnsNullArchiveBuilder)
	{
		// Arrange
		auto sut = ArchiveBuilderFactory(naggyMockArchiveBuilderHelper);
		auto type = bsa_archive_type_e::baNone;

		// Act
		auto returned = sut.Create(type, nullptr);
		auto result = returned.get();

		// Assert
		auto resultType = dynamic_cast<NullArchiveBuilder*>(result);
		EXPECT_STREQ(typeid(NullArchiveBuilder).name(), typeid(*resultType).name());
	}

	TEST_F(ArchiveBuilderFactoryFacts, Create_WhenArchiveTypeTES3_ReturnsGeneralArchiveBuilder)
	{
		// Arrange
		auto sut = ArchiveBuilderFactory(naggyMockArchiveBuilderHelper);
		auto type = bsa_archive_type_e::baTES3;
		auto dto = ModDto(0, "", "", "");

		// Act
		auto returned = sut.Create(type, &dto);
		auto result = returned.get();

		// Assert
		auto resultType = dynamic_cast<GeneralArchiveBuilder*>(result);
		EXPECT_STREQ(typeid(GeneralArchiveBuilder).name(), typeid(*resultType).name());
	}

	TEST_F(ArchiveBuilderFactoryFacts, Create_WhenArchiveTypeTES4_ReturnsGeneralArchiveBuilder)
	{
		// Arrange
		auto sut = ArchiveBuilderFactory(naggyMockArchiveBuilderHelper);
		auto type = bsa_archive_type_e::baTES4;
		auto dto = ModDto(0, "", "", "");

		// Act
		auto returned = sut.Create(type, &dto);
		auto result = returned.get();

		// Assert
		auto resultType = dynamic_cast<GeneralArchiveBuilder*>(result);
		EXPECT_STREQ(typeid(GeneralArchiveBuilder).name(), typeid(*resultType).name());
	}

	TEST_F(ArchiveBuilderFactoryFacts, Create_WhenArchiveTypeFO3_ReturnsGeneralArchiveBuilder)
	{
		// Arrange
		auto sut = ArchiveBuilderFactory(naggyMockArchiveBuilderHelper);
		auto type = bsa_archive_type_e::baFO3;
		auto dto = ModDto(0, "", "", "");

		// Act
		auto returned = sut.Create(type, &dto);
		auto result = returned.get();

		// Assert
		auto resultType = dynamic_cast<GeneralArchiveBuilder*>(result);
		EXPECT_STREQ(typeid(GeneralArchiveBuilder).name(), typeid(*resultType).name());
	}

	TEST_F(ArchiveBuilderFactoryFacts, Create_WhenArchiveTypeSSE_ReturnsGeneralArchiveBuilder)
	{
		// Arrange
		auto sut = ArchiveBuilderFactory(naggyMockArchiveBuilderHelper);
		auto type = bsa_archive_type_e::baSSE;
		auto dto = ModDto(0, "", "", "");

		// Act
		auto returned = sut.Create(type, &dto);
		auto result = returned.get();

		// Assert
		auto resultType = dynamic_cast<GeneralArchiveBuilder*>(result);
		EXPECT_STREQ(typeid(GeneralArchiveBuilder).name(), typeid(*resultType).name());
	}

	TEST_F(ArchiveBuilderFactoryFacts, Create_WhenArchiveTypeFO4_ReturnsTexturelessArchiveBuilder)
	{
		// Arrange
		auto sut = ArchiveBuilderFactory(naggyMockArchiveBuilderHelper);
		auto type = bsa_archive_type_e::baFO4;
		auto dto = ModDto(0, "", "", "");

		// Act
		auto returned = sut.Create(type, &dto);
		auto result = returned.get();

		// Assert
		auto resultType = dynamic_cast<TexturelessArchiveBuilder*>(result);
		EXPECT_STREQ(typeid(TexturelessArchiveBuilder).name(), typeid(*resultType).name());
	}

	TEST_F(ArchiveBuilderFactoryFacts, Create_WhenArchiveTypeFO4dds_ReturnsTextureArchiveBuilder)
	{
		// Arrange
		auto sut = ArchiveBuilderFactory(naggyMockArchiveBuilderHelper);
		auto type = bsa_archive_type_e::baFO4dds;
		auto dto = ModDto(0, "", "", "");

		// Act
		auto returned = sut.Create(type, &dto);
		auto result = returned.get();

		// Assert
		auto resultType = dynamic_cast<TextureArchiveBuilder*>(result);
		EXPECT_STREQ(typeid(TextureArchiveBuilder).name(), typeid(*resultType).name());
	}
}
