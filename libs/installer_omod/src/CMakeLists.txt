# 3.23 for CMAKE_VS_NUGET_PACKAGE_RESTORE
cmake_minimum_required(VERSION 3.23)

find_package(mo2-cmake CONFIG REQUIRED)
find_package(mo2-uibase CONFIG REQUIRED)

# Dummy .NET library as VS_PACKAGE_REFERENCES doesn't work on C++/CLI projects yet
# Needs to be declared before cmake_common stuff is included as that polutes the environment and makes C# get compiled as C++
add_library(dummy_cs_project SHARED DummyCSFile.cs)
set_target_properties(dummy_cs_project PROPERTIES
	LINKER_LANGUAGE CSharp
	VS_PACKAGE_REFERENCES "OMODFramework_2.2.2;OMODFramework.Scripting_2.2.2;RtfPipe_1.0.7388.1242"
)

add_library(installer_omod SHARED)
mo2_configure_plugin(installer_omod WARNINGS OFF CLI ON)
target_link_libraries(installer_omod PRIVATE mo2::uibase)

# I'd like to use get_target_property(source_files ${PROJECT_NAME} SOURCES) as
# globbing is naughty, but need to filter out the things that aren't relative to this directory.
file(GLOB_RECURSE source_files CONFIGURE_DEPENDS *.cpp;*.h;*.ui)
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} PREFIX src FILES ${source_files})

# Ideally we'd use "$<TARGET_FILE_DIR:dummy_cs_project>", but the relevant property
# doesn't support generator expressions.
set(omod_framework_prefix "${CMAKE_BINARY_DIR}/$(Configuration)"
	CACHE PATH "Path where OMODFramework.dll and OMODFramework.Scripting.dll can be found. The default value is where the nuget package artifacts will end up. Override if you want to use a custom build.")

set_target_properties(${PROJECT_NAME} PROPERTIES
	COMMON_LANGUAGE_RUNTIME ""
	# latest may not work with CLR
	CXX_STANDARD 20
	#DOTNET_TARGET_FRAMEWORK "netstandard2.0"
	VS_DOTNET_REFERENCE_OMODFramework "${omod_framework_prefix}/OMODFramework.dll"
	VS_DOTNET_REFERENCE_OMODFramework.Scripting "${omod_framework_prefix}/OMODFramework.Scripting.dll"
	VS_DOTNET_REFERENCE_RtfPipe "${CMAKE_BINARY_DIR}/$(Configuration)/RtfPipe.dll"
	VS_DOTNET_REFERENCES "System.dll;System.Core.dll"
)

target_compile_options(
	installer_omod
	PRIVATE
	# OMODFramework and OMODFramework.Scripting reference different .NET standard libraries. This generates warnings when using them together.
	"/wd4691")

# We don't want ERROR defined
target_compile_definitions(installer_omod PRIVATE "NOGDI")

# We don't need to actually link with dummy_cs_project, especially as its dependencies
# aren't pulled in. We do need it to build first, though.
add_dependencies(installer_omod dummy_cs_project)

mo2_install_plugin(installer_omod FOLDER)

install(
	FILES
	"$<TARGET_FILE_DIR:${PROJECT_NAME}>/OMODFramework.dll"
	"$<TARGET_FILE_DIR:${PROJECT_NAME}>/OMODFramework.Scripting.dll"
	"$<TARGET_FILE_DIR:${PROJECT_NAME}>/ICSharpCode.SharpZipLib.dll"
	"$<TARGET_FILE_DIR:${PROJECT_NAME}>/System.Drawing.Common.dll"
	"$<TARGET_FILE_DIR:${PROJECT_NAME}>/RtfPipe.dll"
	DESTINATION bin/plugins/installer_omod/
)
install(
	FILES "$<TARGET_PDB_FILE_DIR:${PROJECT_NAME}>/ICSharpCode.SharpZipLib.pdb"
	DESTINATION pdb)
# Other PDB files get downloaded by Visual Studio during debugging when https://symbols.nuget.org/download/symbols is in the symbol server list
