from __future__ import annotations

from collections.abc import Sequence
from pathlib import Path
from typing import Any, cast

from antlr4 import ParserRuleContext
from PyQt6 import QtWidgets
from PyQt6.QtCore import Qt, pyqtSignal
from PyQt6.QtGui import QFontDatabase, QKeySequence, QPixmap, QResizeEvent, QShortcut
from PyQt6.QtWidgets import QApplication

from wizard.contexts import (
    WizardInterpreterContext,
    WizardRequireVersionsContext,
    WizardSelectContext,
    WizardSelectManyContext,
    WizardSelectOneContext,
    WizardTerminationContext,
    WizardTopLevelContext,
)
from wizard.errors import WizardError
from wizard.interpreter import WizardInterpreter
from wizard.manager import SelectOption
from wizard.runner import WizardRunnerKeywordVisitor, WizardRunnerState
from wizard.tweaks import WizardINISetting
from wizard.value import Plugin

import mobase

from .ui.wizardinstallercomplete import Ui_WizardInstallerComplete
from .ui.wizardinstallerdialog import Ui_WizardInstallerDialog
from .ui.wizardinstallererror import Ui_WizardInstallerError
from .ui.wizardinstallerpage import Ui_WizardInstallerPage
from .ui.wizardinstallerrequires import Ui_WizardInstallerRequires
from .utils import make_ini_tweaks

WizardRunnerContext = WizardInterpreterContext[WizardRunnerState, Any]


def check_version(
    context: WizardRequireVersionsContext[WizardRunnerState],
    organizer: mobase.IOrganizer,
) -> tuple[bool, bool, bool, bool]:
    """
    Check if the requirements are ok.

    Args:
        context: The requires version context to check.
        organizer: The organizer to fetch actual versions from.

    Returns:
        A 4-tuple of boolean values, where each value is True if the installed
        version is ok, False otherwise. In order, checks are game, script extender
        graphics extender (True if there is no requirements, False otherwise since
        we cannot check in MO2), and wrye bash (always True).
    """
    game = organizer.managedGame()

    game_ok = True
    if context.game_version:
        game_ok = mobase.VersionInfo(context.game_version) <= game.version()

    # script extender
    se_ok = True
    if context.script_extender_version:
        se = organizer.gameFeatures().gameFeature(mobase.ScriptExtender)
        if not se or not se.isInstalled():
            se_ok = False
        else:
            if mobase.VersionInfo(
                context.script_extender_version
            ) <= mobase.VersionInfo(se.getExtenderVersion()):
                se_ok = True
            else:
                se_ok = False

    # cannot check these so...
    ge_ok = not context.graphics_extender_version

    return (game_ok, se_ok, ge_ok, True)


class WizardInstallerRequiresVersionPage(QtWidgets.QWidget):
    context: WizardRequireVersionsContext[WizardRunnerState]

    def __init__(
        self,
        context: WizardRequireVersionsContext[WizardRunnerState],
        organizer: mobase.IOrganizer,
        parent: QtWidgets.QWidget,
    ):
        super().__init__(parent)

        self.context = context

        # set the ui file
        self.ui = Ui_WizardInstallerRequires()
        self.ui.setupUi(self)  # pyright: ignore[reportUnknownMemberType]

        self.ui.groupBox.setStyleSheet(
            'QLabel[headercell="true"] { font-weight: bold; }'
        )

        game = organizer.managedGame()

        okIcon = QPixmap(":/MO/gui/checked-checkbox").scaled(
            16,
            16,
            Qt.AspectRatioMode.KeepAspectRatio,
            Qt.TransformationMode.SmoothTransformation,
        )
        noIcon = QPixmap(":/MO/gui/unchecked-checkbox").scaled(
            16,
            16,
            Qt.AspectRatioMode.KeepAspectRatio,
            Qt.TransformationMode.SmoothTransformation,
        )
        koIcon = QPixmap(":/MO/gui/indeterminate-checkbox").scaled(
            16,
            16,
            Qt.AspectRatioMode.KeepAspectRatio,
            Qt.TransformationMode.SmoothTransformation,
        )

        self.ui.labelGame.setText(game.gameName())

        # set the required version
        self.ui.labelGameNeed.setText(context.game_version)
        self.ui.labelScriptExtenderNeed.setText(context.script_extender_version)
        self.ui.labelGraphicsExtenderNeed.setText(context.graphics_extender_version)
        self.ui.labelWryeBashNeed.setText(context.wrye_bash_version)

        # set the current version
        self.ui.labelGameHave.setText(game.version().canonicalString())
        se = organizer.gameFeatures().gameFeature(mobase.ScriptExtender)
        if se and se.isInstalled():
            self.ui.labelScriptExtenderHave.setText(se.getExtenderVersion())

        # cannot check these so...
        game_ok, se_ok, _, _ = check_version(context, organizer)
        self.ui.labelGameIcon.setPixmap(okIcon if game_ok else koIcon)
        self.ui.labelScriptExtenderIcon.setPixmap(okIcon if se_ok else koIcon)
        self.ui.labelGraphicsExtenderIcon.setPixmap(noIcon)
        self.ui.labelWryeBashIcon.setPixmap(noIcon)


class WizardInstallerSelectPage(QtWidgets.QWidget):
    # signal emitted when an item is double-clicked, only for SelectOne context
    itemDoubleClicked = pyqtSignal()

    context: WizardSelectContext[WizardRunnerState, Any]
    _images: dict[Path, Path]
    _currentImage: QPixmap

    def __init__(
        self,
        context: WizardSelectContext[WizardRunnerState, Any],
        images: dict[Path, Path],
        options: Sequence[str] | None,
        parent: QtWidgets.QWidget,
    ):
        """
        Args:
            context: The context for this page.
            images: A mapping from path (in the archive) to extracted path.
            options: Potential list of options to select. Might not exactly match.
            parent: The parent widget.
        """
        super().__init__(parent)

        self._images = images

        # set the ui file
        self.ui = Ui_WizardInstallerPage()
        self.ui.setupUi(self)  # pyright: ignore[reportUnknownMemberType]

        self.ui.optionList.currentItemChanged.connect(  # pyright: ignore[reportUnknownMemberType]
            self.onCurrentItemChanged
        )

        # create list item widgets
        for _ in context.options:
            item = QtWidgets.QListWidgetItem()
            self.ui.optionList.addItem(item)

        self.update_context(context)

        # extract previous select options
        previous_options = []
        if options:
            previous_options = [
                option for option in context.options if option.name in options
            ]
        else:
            if isinstance(context, WizardSelectManyContext):
                previous_options = context.defaults
            elif isinstance(context, WizardSelectOneContext):
                previous_options = [context.default]

        # set default values
        for i, option in enumerate(context.options):
            item = self.ui.optionList.item(i)
            assert item is not None
            if isinstance(context, WizardSelectManyContext):
                item.setFlags(item.flags() | Qt.ItemFlag.ItemIsUserCheckable)
                if option in previous_options:
                    item.setCheckState(Qt.CheckState.Checked)
                else:
                    item.setCheckState(Qt.CheckState.Unchecked)
            elif (
                isinstance(context, WizardSelectOneContext)
                and option in previous_options
            ):
                item.setSelected(True)
                self.ui.optionList.setCurrentItem(item)

        if isinstance(context, WizardSelectOneContext):
            self.ui.optionList.doubleClicked.connect(  # pyright: ignore[reportUnknownMemberType]
                self.itemDoubleClicked.emit
            )

    def update_context(self, context: WizardSelectContext[WizardRunnerState, Any]):
        self.context = context

        options = self.context.options
        assert len(options) == self.ui.optionList.count()

        self.ui.selectDescriptionLabel.setText(context.description)
        self.ui.selectDescriptionLabel.setMargin(4)

        # update the content of the items
        for i, option in enumerate(options):
            item = self.ui.optionList.item(i)
            assert item is not None
            item.setText(option.name)
            item.setData(Qt.ItemDataRole.UserRole, option)

        # no item selected, select the first one
        if not self.ui.optionList.currentItem():
            self.ui.optionList.setCurrentRow(0)

    def onCurrentItemChanged(
        self, current: QtWidgets.QListWidgetItem, previous: QtWidgets.QListWidgetItem
    ):
        option: SelectOption = current.data(Qt.ItemDataRole.UserRole)
        self.ui.descriptionTextEdit.setText(option.description)
        image = option.image
        if image and Path(image) in self._images:
            target = self._images[Path(image)]
            self._currentImage = QPixmap(target.as_posix())
        else:
            self._currentImage = QPixmap()

        self.ui.imageLabel.setPixmap(self.getResizedImage())

    def getResizedImage(self) -> QPixmap:
        if self._currentImage.isNull():
            return self._currentImage
        return self._currentImage.scaled(
            self.ui.imageLabel.size(),
            Qt.AspectRatioMode.KeepAspectRatio,
            Qt.TransformationMode.SmoothTransformation,
        )

    def resizeEvent(self, a0: QResizeEvent | None) -> None:
        super().resizeEvent(a0)
        self.ui.imageLabel.setPixmap(self.getResizedImage())

    def selectedOptions(self) -> list[SelectOption]:
        options: list[SelectOption] = []
        if isinstance(self.context, WizardSelectOneContext):
            item = self.ui.optionList.currentItem()
            assert item is not None
            options.append(item.data(Qt.ItemDataRole.UserRole))
        else:
            for i in range(self.ui.optionList.count()):
                item = self.ui.optionList.item(i)
                assert item is not None
                if item.checkState() == Qt.CheckState.Checked:
                    options.append(item.data(Qt.ItemDataRole.UserRole))
        return options

    def selected(self) -> WizardSelectContext[WizardRunnerState, Any]:
        if isinstance(self.context, WizardSelectOneContext):
            return self.context.select(self.selectedOptions()[0])
        elif isinstance(self.context, WizardSelectManyContext):
            return self.context.select(self.selectedOptions())
        else:
            return self.context


class WizardInstallerCompletePage(QtWidgets.QWidget):
    def __init__(
        self,
        context: WizardTerminationContext[WizardRunnerState],
        parent: QtWidgets.QWidget,
    ):
        super().__init__(parent)

        # set the ui file
        self.ui = Ui_WizardInstallerComplete()
        self.ui.setupUi(self)  # pyright: ignore[reportUnknownMemberType]

        self.setStyleSheet('QLabel[heading="true"] { font-weight: bold; }')

        self.context = context
        self.state = context.state

        # retrieve the keyword visitor
        kvisitor = cast(WizardRunnerKeywordVisitor, context.factory.kvisitor)

        # the list of plugins in selected sub-packages
        plugins: set[Plugin] = set()

        # sub-packages
        for sp in kvisitor.subpackages:
            item = QtWidgets.QListWidgetItem()
            item.setText(sp.name)
            if sp.name in self.state.subpackages:
                item.setCheckState(Qt.CheckState.Checked)
            else:
                item.setCheckState(Qt.CheckState.Unchecked)
            item.setFlags(item.flags() & ~Qt.ItemFlag.ItemIsUserCheckable)
            plugins.update(kvisitor.plugins_for(sp))
            self.ui.subpackagesList.addItem(item)

        # switch the renamed plugins
        for plugin in list(plugins):
            if plugin in self.state.renames:
                plugins.remove(plugin)
                plugins.add(Plugin(self.state.renames[plugin]))

        # lugins
        for plugin in sorted(plugins):
            item = QtWidgets.QListWidgetItem()
            item.setText(plugin.name)
            if plugin in self.state.plugins:
                item.setCheckState(Qt.CheckState.Checked)
            else:
                item.setCheckState(Qt.CheckState.Unchecked)
            item.setFlags(item.flags() & ~Qt.ItemFlag.ItemIsUserCheckable)
            self.ui.pluginsList.addItem(item)

        # INI Tweaks
        self.ui.tweaksWidget.setVisible(bool(self.state.tweaks))
        self.ui.tweaksList.currentItemChanged.connect(  # pyright: ignore[reportUnknownMemberType]
            self.onCurrentTweakItemChanged
        )
        if self.state.tweaks:
            # group the tweaks per file
            tweaks = {
                file: self.state.tweaks.tweaks(file)
                for file in self.state.tweaks.files()
            }

            for file, ftweaks in tweaks.items():
                item = QtWidgets.QListWidgetItem()
                item.setText(file.replace("\\", "/"))
                item.setData(Qt.ItemDataRole.UserRole, ftweaks)
                self.ui.tweaksList.addItem(item)

            self.ui.tweaksTextEdit.setFont(
                QFontDatabase.systemFont(QFontDatabase.SystemFont.FixedFont)
            )
            self.ui.tweaksList.setCurrentRow(0)

        # notes
        md = ""
        for note in self.state.notes:
            md += f"- {note}\n"
        document = self.ui.notesTextEdit.document()
        assert document is not None
        document.setIndentWidth(10)
        self.ui.notesTextEdit.setMarkdown(md)

    def onCurrentTweakItemChanged(
        self, current: QtWidgets.QListWidgetItem, previous: QtWidgets.QListWidgetItem
    ):
        # clear text area and create the tweaks
        self.ui.tweaksTextEdit.clear()
        self.ui.tweaksTextEdit.appendPlainText(
            make_ini_tweaks(current.data(Qt.ItemDataRole.UserRole))
        )

    def subpackages(self) -> list[str]:
        """
        Returns:
            The list of subpackages selected in the UI (either automatically by the
            interpreter or by the user).
        """
        sp: list[str] = []
        for i in range(self.ui.subpackagesList.count()):
            item = self.ui.subpackagesList.item(i)
            assert item is not None
            if item.checkState() == Qt.CheckState.Checked:
                sp.append(item.text())
        return sp

    def plugins(self) -> dict[str, bool]:
        """
        Returns:
            The list of plugins selected in the UI (either automatically by the
            interpreter or by the user).
        """
        sp: dict[str, bool] = {}
        for i in range(self.ui.pluginsList.count()):
            item = self.ui.pluginsList.item(i)
            assert item is not None
            sp[item.text()] = item.checkState() == Qt.CheckState.Checked
        return sp

    def tweaks(self) -> dict[str, list[WizardINISetting]]:
        """
        Returns:
            The list of tweaks created by the wizard. The returned value maps filenames
            to INI tweaks.
        """
        rets: dict[str, list[WizardINISetting]] = {}
        for i in range(self.ui.tweaksList.count()):
            item = self.ui.tweaksList.item(i)
            assert item is not None
            rets[item.text()] = item.data(Qt.ItemDataRole.UserRole)
        return rets


class WizardInstallerCancelPage(QtWidgets.QWidget):
    def __init__(
        self,
        context: WizardTerminationContext[WizardRunnerState],
        parent: QtWidgets.QWidget,
    ):
        super().__init__(parent)

        # set the ui file (same UI file for both cancel and error)
        self.ui = Ui_WizardInstallerError()
        self.ui.setupUi(self)  # pyright: ignore[reportUnknownMemberType]

        self.ui.titleLabel.setText(
            "The installation was cancelled by the installer with the following reason."
        )
        style = self.style()
        assert style is not None
        self.ui.iconLabel.setPixmap(
            style.standardIcon(
                QtWidgets.QStyle.StandardPixmap.SP_MessageBoxWarning
            ).pixmap(24, 24)
        )
        self.ui.messageEdit.setText(context.message())


class WizardInstallerErrorPage(QtWidgets.QWidget):
    def __init__(
        self,
        error: WizardError,
        parent: QtWidgets.QWidget,
    ):
        super().__init__(parent)

        # set the ui file (same UI file for both cancel and error)
        self.ui = Ui_WizardInstallerError()
        self.ui.setupUi(self)  # pyright: ignore[reportUnknownMemberType]

        self.ui.titleLabel.setText(
            "An error occurred during the installation of the script, "
            "this is probably due to an incorrect script file (wizard.txt) in the "
            "archive."
        )
        style = self.style()
        assert style is not None
        self.ui.iconLabel.setPixmap(
            style.standardIcon(
                QtWidgets.QStyle.StandardPixmap.SP_MessageBoxCritical
            ).pixmap(24, 24)
        )
        self.ui.messageEdit.setText(str(error))


class WizardInstallerDialog(QtWidgets.QDialog):
    # flag to indicate if the user chose to do a manual installation
    _manual: bool = False

    # organizer
    _organizer: mobase.IOrganizer

    # interpreter
    _interpreter: WizardInterpreter[WizardRunnerState]
    _images: dict[Path, Path]
    _options: dict[str, list[str]]

    # the Wizard MO2 interface
    _start_context: WizardTopLevelContext[WizardRunnerState]

    # dict from context to selected options
    _pages: dict[ParserRuleContext, WizardInstallerSelectPage]

    def __init__(
        self,
        organizer: mobase.IOrganizer,
        interpreter: WizardInterpreter[WizardRunnerState],
        context: WizardTopLevelContext[WizardRunnerState],
        name: mobase.GuessedString,
        images: dict[Path, Path],
        options: dict[str, list[str]],
        parent: QtWidgets.QWidget,
    ):
        """
        Args:
            interpreter: The interpreter to use.
            context: The initial context of the script.
            name: The name of the mod.
            images: A mapping from path (in the archive) to extracted path.
            options: The previously selected options.
            parent: The parent widget.
        """
        super().__init__(parent)

        self._organizer = organizer
        self._interpreter = interpreter
        self._images = images
        self._options = options
        self._start_context = context
        self._pages = {}

        # set the ui file
        self.ui = Ui_WizardInstallerDialog()
        self.ui.setupUi(self)  # pyright: ignore[reportUnknownMemberType]

        self.setWindowFlag(Qt.WindowType.WindowContextHelpButtonHint, False)
        self.setWindowFlag(Qt.WindowType.WindowMaximizeButtonHint, True)

        # mobase.GuessedString contains multiple names with various level of
        # "guess", using.variants() returns the list of names, and doing str(name)
        # will return the most-likely value
        for value in name.variants():
            self.ui.nameCombo.addItem(value)
        completer = self.ui.nameCombo.completer()
        assert completer is not None
        completer.setCaseSensitivity(Qt.CaseSensitivity.CaseSensitive)
        self.ui.nameCombo.setCurrentIndex(self.ui.nameCombo.findText(str(name)))

        # we need to connect the Cancel / Manual buttons. We can of course use
        # PyQt6 signal/slot syntax
        self.ui.cancelBtn.clicked.connect(  # pyright: ignore[reportUnknownMemberType]
            self.reject
        )

        def manualClicked():
            self._manual = True
            self.reject()

        self.ui.manualBtn.clicked.connect(  # pyright: ignore[reportUnknownMemberType]
            manualClicked
        )

        self.ui.prevBtn.clicked.connect(  # pyright: ignore[reportUnknownMemberType]
            self.previousClicked
        )
        self.ui.nextBtn.clicked.connect(  # pyright: ignore[reportUnknownMemberType]
            self.nextClicked
        )

        backShortcut = QShortcut(QKeySequence(Qt.Key.Key_Backspace), self)
        backShortcut.activated.connect(  # pyright: ignore[reportUnknownMemberType]
            self.previousClicked
        )

    @property
    def scriptButtonClicked(self) -> pyqtSignal:
        return self.ui.scriptBtn.clicked  # pyright: ignore[reportReturnType]

    def name(self):
        return self.ui.nameCombo.currentText()

    def subpackages(self):
        """
        Returns:
            The list of subpackages to install. Only valid if exec() returned
            Accepted.
        """
        # we cannot fetch it from the state since the user can modify it in the UI
        widget = self.ui.stackedWidget.currentWidget()
        assert isinstance(widget, WizardInstallerCompletePage)
        return widget.subpackages()

    def plugins(self) -> dict[str, bool]:
        """
        Returns:
            The list of plugins to install and enable. Only valid if exec() returned
            Accepted.
        """
        # we cannot fetch it from the state since the user can modify it in the UI
        widget = self.ui.stackedWidget.currentWidget()
        assert isinstance(widget, WizardInstallerCompletePage)
        return widget.plugins()

    def renames(self) -> dict[str, str]:
        """
        Returns:
            The mapping of renames for plugins. Only valid if exec() returned Accepted.
        """
        widget = self.ui.stackedWidget.currentWidget()
        assert isinstance(widget, WizardInstallerCompletePage)
        return {
            plugin.name: new_name for plugin, new_name in widget.state.renames.items()
        }

    def tweaks(self) -> dict[str, list[WizardINISetting]]:
        """
        Returns:
            The list of tweaks per file. Only valid if exec() returned Accepted.
        """
        widget = self.ui.stackedWidget.currentWidget()
        assert isinstance(widget, WizardInstallerCompletePage)
        return widget.tweaks()

    def selectedOptions(self) -> dict[str, list[str]]:
        """
        Returns:
            The list of all currently selected options.
        """
        result: dict[str, list[str]] = {}
        for i in range(self.ui.stackedWidget.count()):
            page = self.ui.stackedWidget.widget(i)
            if isinstance(page, WizardInstallerSelectPage):
                result[page.context.description] = [
                    option.name for option in page.selectedOptions()
                ]
        return result

    def isManualRequested(self):
        return self._manual

    def previousClicked(self):
        index = self.ui.stackedWidget.currentIndex()
        if index > 0:
            self.ui.stackedWidget.removeWidget(self.ui.stackedWidget.widget(index))

        self._update_prev_button()
        self._update_next_button()
        self._update_focus()

    def nextClicked(self):
        widget = self.ui.stackedWidget.currentWidget()

        try:
            if isinstance(widget, WizardInstallerSelectPage):
                context = widget.selected().exec()
            elif isinstance(widget, WizardInstallerRequiresVersionPage):
                context = widget.context.exec()
            else:
                self.accept()
                return

            context = self._exec_until(context)

            if context.context in self._pages:
                page = self._pages[context.context]
                assert isinstance(context, WizardSelectContext)
                page.update_context(context)
            else:
                page = self._make_page(context)

        except WizardError as ex:
            page = WizardInstallerErrorPage(ex, self)

        index = self.ui.stackedWidget.addWidget(page)
        self.ui.stackedWidget.setCurrentIndex(index)
        self._update_prev_button()
        self._update_next_button()
        self._update_focus()

    def _update_focus(self):
        widget = self.ui.stackedWidget.currentWidget()
        if isinstance(widget, WizardInstallerSelectPage):
            widget.ui.optionList.setFocus()

    def _update_prev_button(self):
        self.ui.prevBtn.setDisabled(self.ui.stackedWidget.currentIndex() <= 0)

    def _update_next_button(self):
        widget = self.ui.stackedWidget.currentWidget()

        self.ui.nextBtn.setDisabled(False)

        name: str = self.ui.nextBtn.text()
        if isinstance(widget, WizardInstallerSelectPage):
            name = self.tr("Next")
        elif isinstance(widget, WizardInstallerRequiresVersionPage):
            name = self.tr("Install anyway")
        elif isinstance(widget, (WizardInstallerCancelPage, WizardInstallerErrorPage)):
            self.ui.nextBtn.setDisabled(True)
        else:
            name = self.tr("Install")

        self.ui.nextBtn.setText(name)

    def _exec_until(self, context: WizardRunnerContext) -> WizardRunnerContext:
        context = self._interpreter.exec_until(
            context,
            (
                WizardSelectContext,
                WizardRequireVersionsContext,
            ),
        )

        # if all requirements are ok, skip the context
        if isinstance(context, WizardRequireVersionsContext):
            if all(check_version(context, self._organizer)):
                return self._exec_until(context.exec())

        return context

    def _make_page(self, context: WizardRunnerContext) -> QtWidgets.QWidget:
        page: QtWidgets.QWidget
        if isinstance(context, WizardSelectContext):
            page = WizardInstallerSelectPage(
                context,
                self._images,
                self._options.get(context.description, None),
                self,
            )
            page.itemDoubleClicked.connect(  # pyright: ignore[reportUnknownMemberType]
                self.nextClicked
            )
            self._pages[context.context] = page  # type: ignore
        elif isinstance(context, WizardRequireVersionsContext):
            page = WizardInstallerRequiresVersionPage(context, self._organizer, self)
        elif isinstance(context, WizardTerminationContext):
            if context.is_cancel():
                page = WizardInstallerCancelPage(context, self)
            else:
                page = WizardInstallerCompletePage(context, self)
        else:
            raise NotImplementedError()  # for typing purpose

        return page

    def exec(self):
        try:
            context = self._exec_until(self._start_context)
            page = self._make_page(context)
        except WizardError as ex:
            page = WizardInstallerErrorPage(ex, self)
        self.ui.stackedWidget.addWidget(page)
        self._update_prev_button()
        self._update_next_button()
        self._update_focus()
        return super().exec()

    def tr(self, value: str):  # pyright: ignore[reportIncompatibleMethodOverride]
        return QApplication.translate("WizardInstallerDialog", value)
