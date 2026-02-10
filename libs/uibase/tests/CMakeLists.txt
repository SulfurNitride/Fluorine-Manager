cmake_minimum_required(VERSION 3.16)

add_executable(uibase-tests EXCLUDE_FROM_ALL)
target_sources(uibase-tests
	PRIVATE
		test_main.cpp
		test_formatters.cpp
		test_ifiletree.cpp
		test_strings.cpp
		test_versioning.cpp
)
mo2_configure_tests(uibase-tests NO_SOURCES NO_MAIN NO_MOCK WARNINGS 4 AUTOMOC OFF)
target_link_libraries(uibase-tests PRIVATE uibase)
