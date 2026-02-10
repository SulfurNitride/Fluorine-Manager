cmake_minimum_required(VERSION 3.16)

find_package(Qt6 CONFIG REQUIRED COMPONENTS Network Qml Quick QuickWidgets Widgets)
find_package(spdlog CONFIG REQUIRED)
qt_standard_project_setup()

set(root_headers
	../include/uibase/delayedfilewriter.h
	../include/uibase/diagnosisreport.h
	../include/uibase/dllimport.h
	../include/uibase/errorcodes.h
	../include/uibase/eventfilter.h
	../include/uibase/exceptions.h
	../include/uibase/executableinfo.h
	../include/uibase/filemapping.h
	../include/uibase/filesystemutilities.h
	../include/uibase/guessedvalue.h
	../include/uibase/idownloadmanager.h
	../include/uibase/json.h
	../include/uibase/log.h
	../include/uibase/memoizedlock.h
	../include/uibase/moassert.h
	../include/uibase/modrepositoryfileinfo.h
	../include/uibase/nxmurl.h
	../include/uibase/pluginrequirements.h
	../include/uibase/pluginsetting.h
	../include/uibase/registry.h
	../include/uibase/report.h
	../include/uibase/safewritefile.h
	../include/uibase/scopeguard.h
	../include/uibase/steamutility.h
	../include/uibase/strings.h
	../include/uibase/utility.h
	../include/uibase/versioning.h
	../include/uibase/versioninfo.h
)
set(interface_headers
    ../include/uibase/iexecutable.h
    ../include/uibase/iexecutableslist.h
	../include/uibase/ifiletree.h
	../include/uibase/ifiletree_utils.h
	../include/uibase/iinstallationmanager.h
	../include/uibase/iinstance.h
	../include/uibase/iinstancemanager.h
	../include/uibase/imodinterface.h
	../include/uibase/imodlist.h
	../include/uibase/imodrepositorybridge.h
	../include/uibase/imoinfo.h
	../include/uibase/iplugin.h
	../include/uibase/iplugindiagnose.h
	../include/uibase/ipluginfilemapper.h
	../include/uibase/iplugingame.h
	../include/uibase/iplugingamefeatures.h
	../include/uibase/iplugininstaller.h
	../include/uibase/iplugininstallercustom.h
	../include/uibase/iplugininstallersimple.h
	../include/uibase/ipluginlist.h
	../include/uibase/ipluginmodpage.h
	../include/uibase/ipluginpreview.h
	../include/uibase/ipluginproxy.h
	../include/uibase/iplugintool.h
	../include/uibase/iprofile.h
	../include/uibase/isavegame.h
	../include/uibase/isavegameinfowidget.h
)
set(tutorial_headers
	../include/uibase/tutorabledialog.h
	../include/uibase/tutorialcontrol.h
	../include/uibase/tutorialmanager.h
)
set(widget_headers
	../include/uibase/expanderwidget.h
	../include/uibase/filterwidget.h
	../include/uibase/finddialog.h
	../include/uibase/lineeditclear.h
	../include/uibase/linklabel.h
	../include/uibase/questionboxmemory.h
	../include/uibase/sortabletreewidget.h
	../include/uibase/taskprogressmanager.h
	../include/uibase/textviewer.h
	../include/uibase/widgetutility.h
)
set(game_features_header
	../include/uibase/game_features/bsainvalidation.h
	../include/uibase/game_features/dataarchives.h
	../include/uibase/game_features/game_feature.h
	../include/uibase/game_features/gameplugins.h
	../include/uibase/game_features/igamefeatures.h
	../include/uibase/game_features/localsavegames.h
	../include/uibase/game_features/moddatachecker.h
	../include/uibase/game_features/moddatacontent.h
	../include/uibase/game_features/savegameinfo.h
	../include/uibase/game_features/scriptextender.h
	../include/uibase/game_features/unmanagedmods.h
)
set(formatters_header
	../include/uibase/formatters/enums.h
	../include/uibase/formatters/qt.h
	../include/uibase/formatters/strings.h
	../include/uibase/formatters.h
)

# Collect all source files
set(UIBASE_SOURCES
	# root sources
	delayedfilewriter.cpp
	diagnosisreport.cpp
	errorcodes.cpp
	eventfilter.cpp
	executableinfo.cpp
	filesystemutilities.cpp
	guessedvalue.cpp
	json.cpp
	log.cpp
	modrepositoryfileinfo.cpp
	nxmurl.cpp
	pluginrequirements.cpp
	registry.cpp
	report.cpp
	safewritefile.cpp
	scopeguard.cpp
	steamutility.cpp
	strings.cpp
	utility.cpp
	versioning.cpp
	versioninfo.cpp
	# interfaces
	ifiletree.cpp
	imodrepositorybridge.cpp
	imoinfo.cpp
	# tutorials
	tutorabledialog.cpp
	tutorialcontrol.cpp
	tutorialmanager.cpp
	# widgets
	expanderwidget.cpp
	finddialog.cpp
	lineeditclear.cpp
	linklabel.cpp
	questionboxmemory.cpp
	sortabletreewidget.cpp
	taskprogressmanager.cpp
	textviewer.cpp
	widgetutility.cpp
	filterwidget.cpp
)

add_library(uibase SHARED
	${UIBASE_SOURCES}
	${root_headers}
	${interface_headers}
	${tutorial_headers}
	${widget_headers}
	${game_features_header}
	${formatters_header}
	pch.h
	finddialog.ui
	questionboxmemory.ui
	taskdialog.ui
	textviewer.ui
)

set_target_properties(uibase PROPERTIES
	DEBUG_POSTFIX d
	AUTOMOC ON
	AUTOUIC ON
	CXX_STANDARD 23
	CXX_STANDARD_REQUIRED ON
	CXX_VISIBILITY_PRESET hidden
	VISIBILITY_INLINES_HIDDEN ON
	POSITION_INDEPENDENT_CODE ON
)

target_include_directories(uibase
	PUBLIC
		$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
	PRIVATE
		$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
		$<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
)

add_library(mo2::uibase ALIAS uibase)

target_compile_definitions(uibase PRIVATE UIBASE_EXPORT SPDLOG_USE_STD_FORMAT)

target_link_libraries(uibase
	PUBLIC Qt6::Widgets Qt6::Network Qt6::QuickWidgets
	PRIVATE spdlog::spdlog_header_only Qt6::Qml Qt6::Quick)

# installation
install(TARGETS uibase EXPORT uibaseTargets
	LIBRARY DESTINATION lib
	ARCHIVE DESTINATION lib
	RUNTIME DESTINATION bin
)
install(DIRECTORY ${PROJECT_SOURCE_DIR}/include/uibase DESTINATION include)
install(EXPORT uibaseTargets
	FILE mo2-uibase-targets.cmake
	NAMESPACE mo2::
	DESTINATION lib/cmake/mo2-uibase
)
