import mobase

from legacy_dialog import PLUGIN_NAME


class ConfiguratorPlugin(mobase.IPlugin):
    def init(self, organizer: mobase.IOrganizer) -> bool:
        return True

    def author(self) -> str:
        return "Test"

    def name(self) -> str:
        return PLUGIN_NAME

    def description(self) -> str:
        return "Retired legacy search-path fixture"

    def version(self) -> mobase.VersionInfo:
        return mobase.VersionInfo("1.0.0")


def createPlugin() -> mobase.IPlugin:
    return ConfiguratorPlugin()
