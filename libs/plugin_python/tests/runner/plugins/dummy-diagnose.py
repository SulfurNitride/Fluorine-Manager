import mobase


class DummyDiagnose(mobase.IPluginDiagnose):
    def activeProblems(self) -> list[int]:
        return [1, 2]

    def shortDescription(self, key: int) -> str:
        return f"short-{key}"

    def fullDescription(self, key: int) -> str:
        return f"long-{key}"

    def hasGuidedFix(self, key: int) -> bool:
        return key == 1


class DummyDiagnoseAndGame(mobase.IPluginDiagnose, mobase.IPluginGame):
    def __init__(self):
        mobase.IPluginDiagnose.__init__(self)
        mobase.IPluginGame.__init__(self)

    def activeProblems(self) -> list[int]:
        return [5, 7]

    def shortDescription(self, key: int) -> str:
        return f"short-{key}"

    def fullDescription(self, key: int) -> str:
        return f"long-{key}"

    def hasGuidedFix(self, key: int) -> bool:
        return key == 7


def createPlugins() -> list[mobase.IPlugin]:
    return [DummyDiagnose(), DummyDiagnoseAndGame()]  # type: ignore
