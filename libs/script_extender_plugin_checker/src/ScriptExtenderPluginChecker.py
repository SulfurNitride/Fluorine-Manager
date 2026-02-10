from __future__ import annotations

import re
from enum import Enum, auto
from pathlib import Path
from typing import Callable, NamedTuple

import mobase
from PyQt6.QtCore import QCoreApplication, qDebug


class PluginMessage:
    kUnknownOrigin = "<unknown>"

    def __init__(self, pluginPath: str | Path, organizer: mobase.IOrganizer):
        self._pluginPath = Path(pluginPath)
        try:
            self._pluginOrigin = organizer.getFileOrigins(
                str(self._pluginPath.relative_to(organizer.managedGame().dataDirectory().absolutePath())))[0]
        except:
            self._pluginOrigin = PluginMessage.kUnknownOrigin

    def successful(self):
        return not self.valid()

    def valid(self):
        return self._pluginOrigin != PluginMessage.kUnknownOrigin

    def asMessage(self):
        return self.tr("{0} ({1}) existed.").format(self._pluginPath.name, self._pluginOrigin)

    def tr(self, value: str):
        return QCoreApplication.translate("PluginMessage", value)

    def pluginPath(self):
        return self._pluginPath

    messageTypes: list[tuple[re.Pattern[str], Callable[[re.Match[str], mobase.IOrganizer], PluginMessage]]] = []

    @staticmethod
    def registerMessageType(
        messageType: tuple[
            re.Pattern[str], Callable[[re.Match[str], mobase.IOrganizer], PluginMessage]
        ],
    ):
        PluginMessage.messageTypes.append(messageType)

    @staticmethod
    def PluginMessageFactory(line: str, organizer: mobase.IOrganizer):
        for messageType in PluginMessage.messageTypes:
            match = messageType[0].fullmatch(line)
            if match:
                return messageType[1](match, organizer)
        return None


class NormalPluginMessage(PluginMessage):
    def __init__(self, match: re.Match[str], organizer: mobase.IOrganizer):
        super().__init__(match.group("pluginPath"), organizer)
        self.__infoVersion = int(match.group("infoVersion"), 16)
        self.__name = match.group("name")
        self.__version = int(match.group("version"), 16)
        self.__loadStatus = match.group("loadStatus")

    def successful(self):
        return not self.valid() or self.__loadStatus == "loaded correctly" or self.__loadStatus == "no version data"

    def asMessage(self):
        return self.tr("{0} version {1} ({2}, {4}) {3}.").format(self.__name, self.__version, self._pluginPath.name,
                                                                   self.__trLoadStatus(), self._pluginOrigin)

    def __trLoadStatus(self):
        # We need to list the possible options so they get detected as translatable strings.
        loadStatusTranslations = {
            "loaded correctly": self.tr("loaded correctly"),
            # Messages taken from SKSE64 2.1.3
            "disabled, address library needs to be updated": self.tr("disabled, address library needs to be updated"),
            "disabled, fatal error occurred while loading plugin": self.tr(
                "disabled, fatal error occurred while loading plugin"),
            "disabled, bad version data": self.tr("disabled, bad version data"),
            "disabled, no name specified": self.tr("disabled, no name specified"),
            "disabled, unsupported version independence method": self.tr(
                "disabled, unsupported version independence method"),
            "disabled, incompatible with current runtime version": self.tr(
                "disabled, incompatible with current runtime version"),
            "disabled, requires newer script extender": self.tr("disabled, requires newer script extender"),
            # Legacy messages
            "reported as incompatible during query": self.tr("reported as incompatible during query"),
            "reported as incompatible during load": self.tr("reported as incompatible during load"),
            "disabled, fatal error occurred while checking plugin compatibility": self.tr(
                "disabled, fatal error occurred while checking plugin compatibility"),
            "disabled, fatal error occurred while querying plugin": self.tr(
                "disabled, fatal error occurred while querying plugin"),
        }
        if self.__loadStatus in loadStatusTranslations:
            return loadStatusTranslations[self.__loadStatus]
        else:
            # There's a blacklist of known broken plugins that get excluded, each with their own reason.
            # We aren't translating that.
            return self.__loadStatus

    def tr(self, value: str):
        return QCoreApplication.translate("NormalPluginMessage", value)


PluginMessage.registerMessageType((re.compile(
    r"plugin (?P<pluginPath>.+) \((?P<infoVersion>[\dA-Fa-f]{8}) (?P<name>.*) (?P<version>[\dA-Fa-f]{8})\) (?P<loadStatus>.+?)(?P<errorCode> \d+)?( \(handle \d+\))?\s$"),
                                   NormalPluginMessage))


class CouldntLoadPluginMessage(PluginMessage):
    def __init__(self, match: re.Match[str], organizer: mobase.IOrganizer):
        super().__init__(match.group("pluginPath"), organizer)
        self.__lastError = int(match.group("lastError"))
        self.__scriptExtenderDetails = match.group("seDetails")
        if not self.__scriptExtenderDetails or self.__scriptExtenderDetails.isspace():
            self.__scriptExtenderDetails = ""

    def successful(self):
        return not self.valid()

    def asMessage(self):
        if self.__lastError == 126:
            message = self.tr("Couldn't load {0} ({2}). A dependency DLL could not be found (code {1}). {3}")
        elif self.__lastError == 193:
            message = self.tr("Couldn't load {0} ({2}). A DLL is invalid (code {1}).")
        else:
            message = self.tr("Couldn't load {0} ({2}). The last error code was {1}.")

        return message.format(self._pluginPath.name, self.__lastError, self._pluginOrigin, self.__scriptExtenderDetails)

    def tr(self, value: str):
        return QCoreApplication.translate("CouldntLoadPluginMessage", value)


PluginMessage.registerMessageType((re.compile(
    r"couldn't load plugin (?P<pluginPath>.+) \(Error (code )?(?P<lastError>[-+]?\d+)(:\s*(?P<seDetails>.*))?\)\s"),
                                   CouldntLoadPluginMessage))


class NotAPluginMessage(PluginMessage):
    def __init__(self, match: re.Match[str], organizer: mobase.IOrganizer):
        super().__init__(match.group("pluginPath"), organizer)

    def successful(self):
        return not self.valid()

    def asMessage(self):
        return self.tr("{0} ({1}) does not appear to be a script extender plugin.").format(self._pluginPath.name,
                                                                                             self._pluginOrigin)

    def tr(self, value: str):
        return QCoreApplication.translate("NotAPluginMessage", value)


PluginMessage.registerMessageType((re.compile(
    r"plugin (?P<pluginPath>.+) does not appear to be an (?:SK)|(?:F4)|(?:NV)|(?:FO)|(?:OB)SE plugin\s"),
                                   NotAPluginMessage))


class LogLocation(Enum):
    DOCS = auto()
    INSTALL = auto()


class ScriptExtenderPluginChecker(mobase.IPluginDiagnose):
    class GameType(NamedTuple):
        base: LogLocation
        gameSuffix: Path | None
        editorSuffix: Path | None

    supportedGames = {
        "Skyrim": GameType(LogLocation.DOCS, Path("SKSE") / "skse.log", Path("SKSE") / "skse_editor.log"),
        "Skyrim Special Edition": GameType(LogLocation.DOCS, Path("SKSE") / "skse64.log", None),
        # No editor log defined
        "Skyrim VR": GameType(LogLocation.DOCS, Path("SKSE") / "sksevr.log", None),  # No editor log defined
        # "Enderal" : GameType(LogLocation.DOCS, Path("")),
        "Fallout 4": GameType(LogLocation.DOCS, Path("F4SE") / "f4se.log", None),  # No editor log defined
        "Oblivion": GameType(LogLocation.INSTALL, Path("obse.log"), Path("obse_editor.log")),
        "New Vegas": GameType(LogLocation.INSTALL, Path("nvse.log"), Path("nvse_editor.log")),
        "TTW": GameType(LogLocation.INSTALL, Path("nvse.log"), Path("nvse_editor.log")),
        "Fallout 3": GameType(LogLocation.INSTALL, Path("fose.log"), Path("fose_editor.log"))
    }

    __organizer: mobase.IOrganizer

    def __init__(self):
        super(ScriptExtenderPluginChecker, self).__init__()

    def init(self, organizer: mobase.IOrganizer):
        self.__organizer = organizer

        organizer.onFinishedRun(lambda a, b: self._invalidate())

        return True

    def name(self):
        return "Script Extender Plugin Load Checker"

    def localizedName(self):
        return self.tr("Script Extender Plugin Load Checker")

    def author(self):
        return "AnyOldName3"

    def description(self):
        return self.tr("Checks script extender log to see if any plugins failed to load.")

    def version(self):
        return mobase.VersionInfo(1, 2, 0, 0)

    def requirements(self) -> list[mobase.IPluginRequirement]:
        return [
            mobase.PluginRequirementFactory.gameDependency(list(self.supportedGames))
        ]

    def settings(self) -> list[mobase.PluginSetting]:
        return []

    def activeProblems(self) -> list[int]:
        if self.__scanLog():
            return [0]
        else:
            return []

    def shortDescription(self, key: int):
        return self.tr("Script extender log reports incompatible plugins.")

    def fullDescription(self, key: int):
        pluginList = self.__listBadPluginMessagess()
        pluginListString = "\n  • " + ("\n  • ".join(pluginList))
        return self.tr("You have one or more script extender plugins which failed to load!\n\n "
                         "If you want this notification to go away, here are some steps you can take:\n"
                         "  • Look for updates to the mod or the specific plugin included in the mod.\n"
                         "  • Disable the mod containing the plugin.\n"
                         "  • Hide or delete the plugin from the mod.\n\n"
                         "To refresh the script extender logs, you will need to run the game and/or editor again!\n\n"
                         "The failed plugins are:{0}").format(pluginListString)

    def hasGuidedFix(self, key: int):
        return False

    def startGuidedFix(self, key: int):
        pass

    def tr(self, value: str):
        return QCoreApplication.translate("ScriptExtenderPluginChecker", value)

    def __scanLog(self):
        return len(self.__listBadPluginMessagess()) > 0

    def __listBadPluginMessagess(self) -> list[str]:
        base, gameSuffix, editorSuffix = self.supportedGames[self.__organizer.managedGame().gameName()]

        if base == LogLocation.DOCS:
            base = Path(self.__organizer.managedGame().documentsDirectory().absolutePath())
        elif base == LogLocation.INSTALL:
            base = Path(self.__organizer.managedGame().gameDirectory().absolutePath())

        editorMessageList: list[PluginMessage] = []
        gameMessageList: list[PluginMessage] = []

        if gameSuffix is not None:
            gameLogPath = base / gameSuffix
            try:
                if gameLogPath.exists():
                    with gameLogPath.open('r', encoding='cp1252') as logFile:
                        for line in logFile:
                            message = PluginMessage.PluginMessageFactory(line, self.__organizer)
                            if message:
                                gameMessageList.append(message)
            except Exception as e:
                qDebug(str(e))
                # There's almost certainly just no log yet
                pass

        if editorSuffix is not None:
            editorLogPath = base / editorSuffix
            try:
                if editorLogPath.exists():
                    with editorLogPath.open('r', encoding='cp1252') as logFile:
                        for line in logFile:
                            message = PluginMessage.PluginMessageFactory(line, self.__organizer)
                            if message:
                                editorMessageList.append(message)
            except Exception as e:
                qDebug(str(e))
                # There's almost certainly just no log yet
                pass

        messageList: list[str] = []

        # Search each list for plugins that are not successful in either list
        for gameMessage in gameMessageList:
            if not gameMessage.successful():
                for editorMessage in editorMessageList:
                    if gameMessage.pluginPath() == editorMessage.pluginPath() and editorMessage.successful():
                        break
                else:
                    message = gameMessage.asMessage()
                    if message not in messageList:
                        messageList.append(message)

        for editorMessage in editorMessageList:
            if not editorMessage.successful():
                for gameMessage in gameMessageList:
                    if editorMessage.pluginPath() == gameMessage.pluginPath() and gameMessage.successful():
                        break
                else:
                    message = editorMessage.asMessage()
                    if message not in messageList:
                        messageList.append(message)

        return messageList


def createPlugin():
    return ScriptExtenderPluginChecker()
