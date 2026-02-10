import mobase


class DummyPlugin(mobase.IPlugin):
    def init(self, organizer: mobase.IOrganizer) -> bool:
        return True

    def author(self) -> str:
        return "The Author"

    def name(self) -> str:
        return "The Name"

    def description(self) -> str:
        return "The Description"

    def version(self) -> mobase.VersionInfo:
        return mobase.VersionInfo("1.3.0")

    def settings(self) -> list[mobase.PluginSetting]:
        return [
            mobase.PluginSetting(
                "a setting", "the setting description", default_value=12
            )
        ]


def createPlugin() -> mobase.IPlugin:
    return DummyPlugin()
