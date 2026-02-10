import mobase


class DummyFileMapper(mobase.IPluginFileMapper):
    def mappings(self) -> list[mobase.Mapping]:
        return [
            mobase.Mapping(
                source="the source", destination="the destination", is_directory=False
            ),
            mobase.Mapping(
                source="the other source",
                destination="the other destination",
                is_directory=True,
            ),
        ]


class DummyFileMapperAndGame(mobase.IPluginFileMapper, mobase.IPluginGame):
    def __init__(self):
        mobase.IPluginFileMapper.__init__(self)
        mobase.IPluginGame.__init__(self)

    def mappings(self) -> list[mobase.Mapping]:
        return [
            mobase.Mapping(
                source="the source", destination="the destination", is_directory=False
            ),
        ]


def createPlugins() -> list[mobase.IPlugin]:
    return [DummyFileMapper(), DummyFileMapperAndGame()]  # type: ignore
